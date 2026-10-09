#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
QMutex g_mutex;
QString g_override;

QString ensureDir(const QString& path)
{
    QDir().mkpath(path);
    return path;
}

QString systemDirectory()
{
#ifdef Q_OS_WIN
    wchar_t buf[MAX_PATH + 1] = {};
    const UINT len = GetSystemDirectoryW(buf, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
        return QDir::fromNativeSeparators(QString::fromWCharArray(buf, static_cast<int>(len)));
#endif
    return QStringLiteral("C:/Windows/System32");
}
} // namespace

namespace AppPaths
{

QString dataDir()
{
    QString overrideDir;
    {
        QMutexLocker locker(&g_mutex);
        overrideDir = g_override;
    }
    if (!overrideDir.isEmpty())
        return ensureDir(overrideDir);

    const QString fromEnv = qEnvironmentVariable("ONEFORALL_DATA_DIR");
    if (!fromEnv.trimmed().isEmpty())
        return ensureDir(QDir::fromNativeSeparators(fromEnv.trimmed()));

    // GenericDataLocation (không phải AppLocalDataLocation): không phụ thuộc tên ứng dụng/tổ chức đặt
    // qua QCoreApplication - các bộ test dựng QApplication riêng không đặt hai giá trị đó.
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath();
    return ensureDir(base + "/OneForAll");
}

QString profilesDir()
{
    return ensureDir(dataDir() + "/profiles");
}

QString logsDir()
{
    return ensureDir(dataDir() + "/logs");
}

namespace
{
/// Các vị trí CŨ có thể còn một bản của tệp hồ sơ (không gồm vị trí hiện tại).
QStringList legacyProfileCandidates(const QString& fileName)
{
    QStringList candidates;
    if (QCoreApplication::instance())
        candidates << QCoreApplication::applicationDirPath() + "/profiles/" + fileName;
    candidates << QDir::current().absoluteFilePath("profiles/" + fileName);

    const QString currentDir = QDir::cleanPath(profilesDir()).toLower();
    QStringList result;
    for (const QString& candidate : std::as_const(candidates))
    {
        const QFileInfo info(candidate);
        const QString dir = QDir::cleanPath(info.absolutePath()).toLower();
        const QString path = QDir::cleanPath(info.absoluteFilePath());
        if (info.isFile() && dir != currentDir && !result.contains(path, Qt::CaseInsensitive))
            result << path;
    }
    return result;
}

bool overrideActive()
{
    QMutexLocker locker(&g_mutex);
    return !g_override.isEmpty();
}
} // namespace

QString profileFile(const QString& fileName)
{
    const QString target = profilesDir() + "/" + fileName;
    if (QFileInfo::exists(target))
        return target;

    // Khi bộ test đã ép thư mục dữ liệu, KHÔNG di trú: nếu không, test sẽ nạp dữ liệu thật của người
    // phát triển (vd profiles\qr_history.json cạnh exe test) vào thư mục tạm và không còn độc lập.
    if (overrideActive())
        return target;

    const QStringList legacy = legacyProfileCandidates(fileName);
    if (!legacy.isEmpty())
    {
        // QFile::copy không ghi đè tệp đã có - nếu một luồng/tiến trình khác vừa tạo target thì giữ
        // nguyên bản đó.
        QFile::copy(legacy.first(), target);
    }
    return target;
}

void removeLegacyProfileCopies(const QString& fileName)
{
    if (overrideActive())
        return;
    for (const QString& path : legacyProfileCandidates(fileName))
        QFile::remove(path);
}

QString logFile(const QString& fileName)
{
    return logsDir() + "/" + fileName;
}

void setDataDirOverride(const QString& dir)
{
    QMutexLocker locker(&g_mutex);
    g_override = dir.isEmpty() ? QString() : QDir(dir).absolutePath();
}

QString systemExecutable(const QString& fileName)
{
    return systemDirectory() + "/" + fileName;
}

QString powershellExecutable()
{
    return systemDirectory() + "/WindowsPowerShell/v1.0/powershell.exe";
}

} // namespace AppPaths
