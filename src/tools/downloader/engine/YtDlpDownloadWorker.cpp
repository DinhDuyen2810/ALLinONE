#include "YtDlpDownloadWorker.h"

#include "YtDlpController.h"
#include "core/WinProcessTree.h"

#include <vector>

YtDlpDownloadWorker::YtDlpDownloadWorker(QObject* parent)
    : QObject(parent)
{
}

YtDlpDownloadWorker::~YtDlpDownloadWorker()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
    {
        // Đối tượng đang bị hủy - không để slot finished() bên dưới phát tín hiệu/quét tiến trình lần nữa.
        disconnect(m_process, nullptr, this, nullptr);

        // Liệt kê hậu duệ TRƯỚC khi buộc dừng - lúc này m_lastPid CHẮC CHẮN vẫn là chính yt-dlp.exe của
        // ta (chưa ai kịp tái sử dụng PID), tránh rủi ro quét SAU khi nó đã thoát có thể khớp nhầm hậu
        // duệ của một tiến trình không liên quan lỡ được Windows cấp trùng đúng PID đó (xem
        // core/WinProcessTree.h).
        const std::vector<qint64> descendants =
            m_lastPid > 0 ? WinProcessTree::findDescendants(m_lastPid) : std::vector<qint64>{};

        m_process->kill();
        m_process->waitForFinished(2000);

        // yt-dlp.exe tự sinh ffmpeg.exe làm tiến trình CON lúc ghép video+âm thanh (xem buildArguments())
        // - kill() ở trên chỉ đóng đúng một handle của chính yt-dlp.exe, Windows không đệ quy dừng luôn
        // ffmpeg.exe con của nó (cùng lỗi đã gặp với scrcpy.exe/adb.exe, xem core/WinProcessTree.h). Nếu
        // người dùng hủy/đóng đúng lúc đang ghép, ffmpeg.exe sẽ mồ côi và treo lại ngầm vô thời hạn.
        WinProcessTree::terminateProcessList(descendants);
    }
}

bool YtDlpDownloadWorker::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void YtDlpDownloadWorker::emitFinishedOnce(bool ok, const QString& error)
{
    if (m_finishedEmitted)
        return;
    m_finishedEmitted = true;
    emit finished(ok, error);
}

void YtDlpDownloadWorker::start(const QString& url, const QString& formatSelector, const QString& destDir)
{
    if (isRunning())
        return; // một worker chỉ chạy một lượt tải tại một thời điểm

    m_canceled = false;
    m_finishedEmitted = false;
    m_stdoutBuffer.clear();
    m_stderrAccum.clear();
    m_lastPid = 0;

    const QString exe = YtDlpController::ytDlpExecutablePath();
    if (exe.isEmpty())
    {
        emitFinishedOnce(false, "Không tìm thấy yt-dlp.exe (thư mục yt-dlp/ cạnh file chạy bị thiếu).");
        return;
    }
    if (!YtDlpController::isSupportedVideoUrl(url))
    {
        emitFinishedOnce(false, "Địa chỉ không hợp lệ - hãy dán URL video bắt đầu bằng http:// hoặc https://.");
        return;
    }

    if (m_process)
        m_process->deleteLater();
    m_process = new QProcess(this);
    m_process->setProgram(exe);
    m_process->setArguments(YtDlpDownloadWorkerInternal::buildArguments(
        url, formatSelector, destDir, YtDlpController::ffmpegDirectoryPath()));

    QProcess* process = m_process;
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
        m_stdoutBuffer += process->readAllStandardOutput();
        int nl;
        while ((nl = m_stdoutBuffer.indexOf('\n')) >= 0)
        {
            const QByteArray line = m_stdoutBuffer.left(nl);
            m_stdoutBuffer.remove(0, nl + 1);

            const auto p = YtDlpDownloadWorkerInternal::parseProgressLine(line);
            if (p.ok)
                emit progress(p.downloadedBytes, p.totalBytes, p.speedBytesPerSec, p.etaSeconds);
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [this, process]() {
        m_stderrAccum += process->readAllStandardError();
    });
    // Không khởi chạy được (tệp bị phần mềm diệt virus khóa/xóa, không có quyền chạy...) thì QProcess CHỈ
    // phát errorOccurred(FailedToStart), KHÔNG phát finished(). Các lỗi khác (Crashed...) luôn kèm
    // finished() nên để slot bên dưới xử lý.
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emitFinishedOnce(false, "Không khởi chạy được yt-dlp.exe (" + process->errorString() + ").");
    });
    connect(process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        if (m_canceled)
        {
            // Đường hủy CHỦ ĐỘNG: cancel() đã liệt kê + dừng hậu duệ lúc yt-dlp.exe còn sống. KHÔNG quét
            // lại theo m_lastPid ở đây - tiến trình đã thoát, PID có thể đã được cấp cho tiến trình khác
            // (đúng rủi ro core/WinProcessTree.h cảnh báo).
            emitFinishedOnce(false, "Đã hủy.");
            return;
        }

        // yt-dlp.exe thoát KHÔNG bình thường trên chính nó (crash, bị Task Manager kill, mất mạng giữa
        // chừng...) - có thể để sót ffmpeg.exe con nếu đang giữa lúc ghép. Quét dọn phòng hờ, cùng lý do
        // với ScrcpyLauncher (không tránh được rủi ro tái sử dụng PID ở core/WinProcessTree.h vì chỉ
        // biết SAU khi yt-dlp.exe đã thoát).
        if (status != QProcess::NormalExit && m_lastPid > 0)
            WinProcessTree::terminateDescendants(m_lastPid);

        if (status != QProcess::NormalExit || exitCode != 0)
        {
            QString msg = QString::fromUtf8(m_stderrAccum).trimmed();
            if (msg.isEmpty())
                msg = QString("yt-dlp thoát với mã lỗi %1").arg(exitCode);
            emitFinishedOnce(false, msg);
        }
        else
        {
            emitFinishedOnce(true, QString());
        }
    });

    process->start();
    m_lastPid = process->processId();
}

void YtDlpDownloadWorker::cancel()
{
    if (!isRunning())
        return;
    m_canceled = true;

    // Liệt kê hậu duệ TRƯỚC khi buộc dừng - cùng lý do với destructor ở trên (tránh rủi ro tái sử dụng
    // PID nếu quét sau khi yt-dlp.exe đã thoát, xem core/WinProcessTree.h).
    const std::vector<qint64> descendants =
        m_lastPid > 0 ? WinProcessTree::findDescendants(m_lastPid) : std::vector<qint64>{};

    m_process->kill(); // yt-dlp không xử lý gọn terminate() trên Windows - kill() trực tiếp an toàn vì chưa ghi tệp đích cuối cùng cho tới khi ghép xong

    // Cũng dọn ffmpeg.exe con nếu đang hủy ngay lúc yt-dlp đang ghép video+âm thanh - xem destructor ở
    // trên/core/WinProcessTree.h. KHÔNG chờ waitForFinished() ở đây (cancel() gọi từ luồng giao diện,
    // không được chặn) - tiến trình chính sẽ tự báo xong qua tín hiệu finished() như bình thường.
    WinProcessTree::terminateProcessList(descendants);
}

namespace YtDlpDownloadWorkerInternal
{

QStringList buildArguments(const QString& url, const QString& formatSelector, const QString& destDir,
                           const QString& ffmpegDir)
{
    QString destDirLiteral = destDir;
    destDirLiteral.replace('%', "%%"); // "%%" = một dấu % nguyên văn trong mẫu -o của yt-dlp

    QStringList args = {
        "--ignore-config",
        "--no-playlist",
        "--newline",
        "--ffmpeg-location", ffmpegDir,
        "--progress-template",
        "download:DLPROG|%(progress.downloaded_bytes)s|%(progress.total_bytes)s|"
        "%(progress.total_bytes_estimate)s|%(progress.speed)s|%(progress.eta)s",
        "-o", destDirLiteral + "/%(title)s.%(ext)s",
    };
    if (!formatSelector.isEmpty())
    {
        args << "-f" << formatSelector;
    }
    args << "--" << url; // sau "--" chỉ còn địa chỉ - không bao giờ bị hiểu là tùy chọn
    return args;
}

QString formatSelectorFor(const VideoFormatInfo& format)
{
    if (format.formatId.isEmpty())
        return {};
    if (format.hasVideo && !format.hasAudio)
        return format.formatId + "+bestaudio/best";
    return format.formatId;
}

namespace
{
qint64 toLongLongOrMinusOne(const QByteArray& field)
{
    bool ok = false;
    // yt-dlp trả số thực cho speed/eta đôi khi (vd "1234567.8") - toDouble rồi ép về qint64 cho an toàn.
    const double d = field.toDouble(&ok);
    return ok ? static_cast<qint64>(d) : -1;
}
} // namespace

ParsedProgress parseProgressLine(const QByteArray& line)
{
    ParsedProgress result;
    if (!line.startsWith("DLPROG|"))
        return result;

    const QList<QByteArray> fields = line.trimmed().split('|');
    if (fields.size() != 6) // "DLPROG" + 5 trường
        return result;

    result.ok = true;
    result.downloadedBytes = toLongLongOrMinusOne(fields[1]);
    result.totalBytes = toLongLongOrMinusOne(fields[2]);
    if (result.totalBytes < 0)
        result.totalBytes = toLongLongOrMinusOne(fields[3]); // total_bytes_estimate khi total_bytes chưa rõ
    result.speedBytesPerSec = toLongLongOrMinusOne(fields[4]);
    result.etaSeconds = toLongLongOrMinusOne(fields[5]);
    return result;
}

} // namespace YtDlpDownloadWorkerInternal
