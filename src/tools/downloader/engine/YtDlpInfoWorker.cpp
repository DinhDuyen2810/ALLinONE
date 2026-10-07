#include "YtDlpInfoWorker.h"

#include "YtDlpController.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

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

    QProcess proc;
    proc.setProgram(exe);
    // --no-playlist: nếu URL vừa là video vừa thuộc playlist (vd link watch?v=...&list=...), chỉ lấy
    // thông tin VIDEO ĐÓ - tránh vô tình kéo theo metadata cả playlist khi người dùng chỉ muốn 1 video.
    proc.setArguments({"--no-playlist", "--simulate", "-j", m_url});
    proc.start();
    if (!proc.waitForStarted(5000))
    {
        emit infoFetched(false, {}, "Không khởi chạy được yt-dlp.exe.");
        return;
    }
    if (!proc.waitForFinished(45000))
    {
        proc.kill();
        proc.waitForFinished(2000);
        emit infoFetched(false, {}, "Hết thời gian chờ yt-dlp (trang web phản hồi quá chậm).");
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
