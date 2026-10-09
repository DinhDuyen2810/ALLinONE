#include "YtDlpInfoWorker.h"

#include "YtDlpController.h"
#include "core/WinProcessTree.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <vector>

YtDlpInfoWorker::YtDlpInfoWorker(QObject* parent)
    : QThread(parent)
{
}

void YtDlpInfoWorker::run()
{
    const QString exe = YtDlpController::ytDlpExecutablePath();
    if (exe.isEmpty())
    {
        emit infoFetched(false, {}, "Không tìm thấy yt-dlp.exe (thư mục yt-dlp/ cạnh file chạy bị thiếu).");
        return;
    }

    if (!YtDlpController::isSupportedVideoUrl(m_url))
    {
        emit infoFetched(false, {}, "Địa chỉ không hợp lệ - hãy dán URL video bắt đầu bằng http:// hoặc https://.");
        return;
    }

    QProcess proc;
    proc.setProgram(exe);
    proc.setArguments(YtDlpInfoWorkerInternal::buildArguments(m_url));
    proc.start();
    if (!proc.waitForStarted(5000))
    {
        emit infoFetched(false, {}, "Không khởi chạy được yt-dlp.exe (" + proc.errorString() + ").");
        return;
    }

    // Chờ theo từng bước 200ms (tối đa 45 giây) để kiểm tra cờ hủy định kỳ - cùng mẫu với
    // PowerShellRunner::runCancelable. QProcess chỉ được đụng tới trên chính luồng này.
    bool finished = false;
    bool canceled = false;
    for (int waited = 0; waited < 45000; waited += 200)
    {
        finished = proc.waitForFinished(200);
        if (finished)
            break;
        if (m_cancelRequested.load())
        {
            canceled = true;
            break;
        }
    }
    if (!finished)
    {
        // yt-dlp.exe (bản đóng gói PyInstaller) tự sinh tiến trình con - kill() chỉ dừng đúng tiến trình
        // cha, Windows không đệ quy dừng con của nó. Liệt kê hậu duệ TRƯỚC khi dừng (lúc PID chắc chắn còn
        // là của ta) rồi dừng nốt - xem core/WinProcessTree.h.
        const std::vector<qint64> descendants = WinProcessTree::findDescendants(proc.processId());
        proc.kill();
        proc.waitForFinished(2000);
        WinProcessTree::terminateProcessList(descendants);
        emit infoFetched(false, {}, canceled ? "Đã hủy." : "Hết thời gian chờ yt-dlp (trang web phản hồi quá chậm).");
        return;
    }

    const QByteArray out = proc.readAllStandardOutput();
    const QByteArray err = proc.readAllStandardError();

    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
    {
        QString msg = QString::fromUtf8(err).trimmed();
        if (msg.isEmpty())
            msg = QString("yt-dlp thoát với mã lỗi %1 - có thể trang này chưa được hỗ trợ.").arg(proc.exitCode());
        emit infoFetched(false, {}, msg);
        return;
    }

    QString parseErr;
    const VideoInfo info = YtDlpInfoWorkerInternal::parseInfoJson(out, &parseErr);
    if (!parseErr.isEmpty())
    {
        emit infoFetched(false, {}, parseErr);
        return;
    }
    emit infoFetched(true, info, QString());
}

namespace YtDlpInfoWorkerInternal
{

QStringList buildArguments(const QString& url)
{
    // --ignore-config: không nạp tệp cấu hình yt-dlp của người dùng/cạnh exe (có thể chứa --exec...).
    // --no-playlist: nếu URL vừa là video vừa thuộc playlist (vd link watch?v=...&list=...), chỉ lấy
    // thông tin VIDEO ĐÓ - tránh vô tình kéo theo metadata cả playlist khi người dùng chỉ muốn 1 video.
    // "--": mọi thứ sau đó là địa chỉ, không bao giờ là tùy chọn.
    return {"--ignore-config", "--no-playlist", "--simulate", "-j", "--", url};
}

VideoInfo parseInfoJson(const QByteArray& json, QString* error)
{
    VideoInfo info;

    // --dump-json in MỘT dòng JSON (hoặc nhiều dòng nếu lỡ dính playlist) - lấy dòng cuối cùng khác
    // rỗng, phòng trường hợp có cảnh báo lạc vào stdout.
    QByteArray line;
    const auto lines = json.split('\n');
    for (auto it = lines.rbegin(); it != lines.rend(); ++it)
    {
        if (!it->trimmed().isEmpty())
        {
            line = *it;
            break;
        }
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
    {
        if (error) *error = "Không đọc được thông tin video (phản hồi không hợp lệ).";
        return info;
    }

    const QJsonObject o = doc.object();
    info.title = o.value("title").toString();
    info.durationSeconds = o.value("duration").toDouble();
    info.thumbnailUrl = o.value("thumbnail").toString();

    for (const QJsonValue& v : o.value("formats").toArray())
    {
        if (!v.isObject())
            continue;
        const QJsonObject f = v.toObject();

        VideoFormatInfo fmt;
        fmt.formatId = f.value("format_id").toString();
        fmt.ext = f.value("ext").toString();
        fmt.resolution = f.value("resolution").toString();
        fmt.note = f.value("format_note").toString();
        fmt.fps = f.value("fps").toDouble();

        const QString vcodec = f.value("vcodec").toString();
        const QString acodec = f.value("acodec").toString();
        fmt.hasVideo = !vcodec.isEmpty() && vcodec != "none";
        fmt.hasAudio = !acodec.isEmpty() && acodec != "none";

        if (f.value("filesize").isDouble())
            fmt.approxSizeBytes = static_cast<qint64>(f.value("filesize").toDouble());
        else if (f.value("filesize_approx").isDouble())
            fmt.approxSizeBytes = static_cast<qint64>(f.value("filesize_approx").toDouble());

        if (!fmt.formatId.isEmpty())
            info.formats << fmt;
    }

    return info;
}

} // namespace YtDlpInfoWorkerInternal
