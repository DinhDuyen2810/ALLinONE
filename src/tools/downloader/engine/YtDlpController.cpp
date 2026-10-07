#include "YtDlpController.h"

#include <QCoreApplication>
#include <QFileInfo>

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
