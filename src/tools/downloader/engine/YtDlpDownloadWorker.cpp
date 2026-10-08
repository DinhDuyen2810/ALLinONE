#include "YtDlpDownloadWorker.h"

#include "YtDlpController.h"
#include "core/WinProcessTree.h"

YtDlpDownloadWorker::YtDlpDownloadWorker(QObject* parent)
    : QObject(parent)
{
}

YtDlpDownloadWorker::~YtDlpDownloadWorker()
{
    if (m_process && m_process->state() != QProcess::NotRunning)
    {
        m_process->kill();
        m_process->waitForFinished(2000);

        // yt-dlp.exe tự sinh ffmpeg.exe làm tiến trình CON lúc ghép video+âm thanh (xem buildArguments())
        // - kill() ở trên chỉ đóng đúng một handle của chính yt-dlp.exe, Windows không đệ quy dừng luôn
        // ffmpeg.exe con của nó (cùng lỗi đã gặp với scrcpy.exe/adb.exe, xem core/WinProcessTree.h). Nếu
        // người dùng hủy/đóng đúng lúc đang ghép, ffmpeg.exe sẽ mồ côi và treo lại ngầm vô thời hạn.
        if (m_lastPid > 0)
            WinProcessTree::terminateDescendants(m_lastPid);
    }
}

void YtDlpDownloadWorker::start(const QString& url, const QString& formatId, const QString& destDir)
{
    const QString exe = YtDlpController::ytDlpExecutablePath();
    if (exe.isEmpty())
    {
        emit finished(false, "Không tìm thấy yt-dlp.exe (thư mục yt-dlp/ cạnh file chạy bị thiếu).");
        return;
    }

    m_process = new QProcess(this);
    m_process->setProgram(exe);
    m_process->setArguments(YtDlpDownloadWorkerInternal::buildArguments(
        url, formatId, destDir, YtDlpController::ffmpegDirectoryPath()));

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        m_stdoutBuffer += m_process->readAllStandardOutput();
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
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        m_stderrAccum += m_process->readAllStandardError();
    });
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        if (status != QProcess::NormalExit || exitCode != 0)
        {
            QString msg = QString::fromUtf8(m_stderrAccum).trimmed();
            if (msg.isEmpty())
                msg = QString("yt-dlp thoát với mã lỗi %1").arg(exitCode);
            emit finished(false, msg);
        }
        else
        {
            emit finished(true, QString());
        }
    });

    m_process->start();
    m_lastPid = m_process->processId();
}

void YtDlpDownloadWorker::cancel()
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;

    m_process->kill(); // yt-dlp không xử lý gọn terminate() trên Windows - kill() trực tiếp an toàn vì chưa ghi tệp đích cuối cùng cho tới khi ghép xong

    // Cũng dọn ffmpeg.exe con nếu đang hủy ngay lúc yt-dlp đang ghép video+âm thanh - xem destructor ở
    // trên/core/WinProcessTree.h. KHÔNG chờ waitForFinished() ở đây (cancel() gọi từ luồng giao diện,
    // không được chặn) - tiến trình chính sẽ tự báo xong qua tín hiệu finished() như bình thường.
    if (m_lastPid > 0)
        WinProcessTree::terminateDescendants(m_lastPid);
}

namespace YtDlpDownloadWorkerInternal
{

QStringList buildArguments(const QString& url, const QString& formatId, const QString& destDir,
                           const QString& ffmpegDir)
{
    QStringList args = {
        "--no-playlist",
        "--newline",
        "--ffmpeg-location", ffmpegDir,
        "--progress-template",
        "download:DLPROG|%(progress.downloaded_bytes)s|%(progress.total_bytes)s|"
        "%(progress.total_bytes_estimate)s|%(progress.speed)s|%(progress.eta)s",
        "-o", destDir + "/%(title)s.%(ext)s",
    };
    if (!formatId.isEmpty())
    {
        args << "-f" << formatId;
    }
    args << url;
    return args;
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

    const QList<QByteArray> fields = line.split('|');
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
