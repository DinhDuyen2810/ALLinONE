#include "YtDlpController.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QUrl>

namespace
{
QString bundleDir()
{
    return QCoreApplication::applicationDirPath() + "/yt-dlp";
}
} // namespace

namespace YtDlpController
{

QString ytDlpExecutablePath()
{
    const QString p = bundleDir() + "/yt-dlp.exe";
    return QFileInfo::exists(p) ? p : QString();
}

QString ffmpegDirectoryPath()
{
    return bundleDir();
}

bool isSupportedVideoUrl(const QString& url)
{
    if (!url.startsWith("http://", Qt::CaseInsensitive) && !url.startsWith("https://", Qt::CaseInsensitive))
        return false;
    for (const QChar c : url)
    {
        if (c.isSpace() || c.category() == QChar::Other_Control)
            return false;
    }
    const QUrl parsed(url);
    return parsed.isValid() && !parsed.host().isEmpty();
}

bool isBundleAvailable(QString* missingWhat)
{
    const bool hasYtDlp = !ytDlpExecutablePath().isEmpty();
    const bool hasFfmpeg = QFileInfo::exists(bundleDir() + "/ffmpeg.exe");
    if (missingWhat)
    {
        if (!hasYtDlp && !hasFfmpeg) *missingWhat = "yt-dlp.exe và ffmpeg.exe";
        else if (!hasYtDlp) *missingWhat = "yt-dlp.exe";
        else if (!hasFfmpeg) *missingWhat = "ffmpeg.exe";
    }
    return hasYtDlp && hasFfmpeg;
}

} // namespace YtDlpController
