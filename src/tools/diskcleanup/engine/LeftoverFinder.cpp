#include "LeftoverFinder.h"

#include "FsSafety.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

#include <algorithm>

namespace LeftoverFinder
{

QString normalizeName(const QString& name)
{
    QString out;
    out.reserve(name.size());
    for (const QChar c : name.toLower())
        if (c.isLetterOrNumber())
            out += c;
    return out;
}

QSet<QString> nameKeys(const QStringList& rawNames)
{
    QSet<QString> keys;
    static const QRegularExpression separators(QStringLiteral("[\\s._\\-(),]+"));
    for (const QString& raw : rawNames)
    {
        if (raw.trimmed().isEmpty())
            continue;
        const QString whole = normalizeName(raw);
        if (whole.size() >= 3)
            keys.insert(whole);
        for (const QString& word : raw.split(separators, Qt::SkipEmptyParts))
        {
            const QString w = normalizeName(word);
            if (w.size() >= 4)
                keys.insert(w);
        }
    }
    return keys;
}

bool belongsToInstalled(const QString& folderName, const QSet<QString>& keys)
{
    const QString n = normalizeName(folderName);
    if (n.size() < 4)
        return true; // quá ngắn để kết luận - không bao giờ đề xuất
    for (const QString& key : keys)
        if ((key.size() >= 4 && n.contains(key)) || key.contains(n))
            return true;
    return false;
}

bool isNeverCandidate(const QString& folderName)
{
    static const QSet<QString> never = [] {
        const QStringList names{
            "Microsoft", "Windows", "Packages", "Temp", "Programs", "Publishers", "Comms", "ConnectedDevicesPlatform",
            "D3DSCache", "CrashDumps", "IsolatedStorage", "VirtualStore", "PlaceholderTileLogoFolder", "Package Cache",
            "Microsoft Help", "Microsoft_Corporation", "MicrosoftEdge", "History", "Application Data", "Local Settings",
            "NVIDIA", "NVIDIA Corporation", "AMD", "ATI", "Intel", "Realtek", "Apple", "Apple Computer",
            "npm", "npm-cache", "pip", "uv", "Yarn", "NuGet", "pnpm", "pnpm-cache", "node-gyp", "go-build", "electron",
            "electron-builder", "Python", "Java", "Sun", "Oracle", "Adobe", "Google", "Mozilla", "BraveSoftware",
            "Opera Software", "Vivaldi", "Chromium", "Steam", "Discord", "discordptb", "discordcanary", "Spotify",
            "Slack", "Code", "Code - Insiders", "Cursor", "Battle.net", "Blizzard Entertainment", "EpicGamesLauncher",
            "Electronic Arts", "Ubisoft Game Launcher", "Riot Games", "GOG.com", "OneForAll", "SquirrelTemp",
            "Downloaded Installations", "ElevatedDiagnostics", "CEF", "fontconfig", "Comms", "DBG", "Diagnostics",
            "PeerDistRepub", "ToastNotificationManagerCompat", "Unity", "UnityHub", "JetBrains", "Zalo", "ZaloPC",
            "ZaloData", "Package Management", "SolidDocuments", "Sentry", "Squirrel"};
        QSet<QString> s;
        for (const QString& n : names)
            s.insert(n.toLower());
        return s;
    }();
    return never.contains(folderName.toLower());
}

QStringList installedRawNames(const QString& localAppData)
{
    QStringList names;
#ifdef Q_OS_WIN
    struct Hive
    {
        const char* path;
        QSettings::Format format;
    };
    const Hive hives[] = {
        {"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", QSettings::Registry64Format},
        {"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", QSettings::Registry32Format},
        {"HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall", QSettings::NativeFormat},
    };
    for (const Hive& hive : hives)
    {
        QSettings reg(QString::fromLatin1(hive.path), hive.format);
        for (const QString& key : reg.childGroups())
        {
            names << key;
            reg.beginGroup(key);
            names << reg.value("DisplayName").toString() << reg.value("Publisher").toString();
            const QString location = QDir::fromNativeSeparators(reg.value("InstallLocation").toString().trimmed());
            if (location.size() > 3)
                names << QFileInfo(QDir::cleanPath(location)).fileName();
            reg.endGroup();
        }
    }
#endif
    // Ứng dụng Store: thư mục gói dạng "Hãng.TênỨngDụng_mãbăm".
    if (!localAppData.isEmpty())
        for (const QString& pkg : QDir(localAppData + "/Packages").entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            names << pkg.section(QLatin1Char('_'), 0, 0);
    // Cài thẳng vào Program Files / Local\Programs cũng tính là đang cài.
    QStringList installRoots;
    for (const char* var : {"ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"})
    {
        const QString v = qEnvironmentVariable(var);
        if (!v.isEmpty())
            installRoots << QDir::fromNativeSeparators(v);
    }
    if (!localAppData.isEmpty())
        installRoots << localAppData + "/Programs";
    for (const QString& root : installRoots)
        names << QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    return names;
}

QList<Leftover> find(const QStringList& roots, const QSet<QString>& installedKeys, qint64 nowFileTime,
                     const std::atomic_bool& stopFlag, const QStringList& protectedDirs, qint64 minSizeBytes, int minIdleDays)
{
    QList<Leftover> found;
    const qint64 idleLimit = nowFileTime - static_cast<qint64>(minIdleDays) * 24LL * 3600LL * 10000000LL;
    for (const QString& root : roots)
    {
        if (root.isEmpty())
            continue;
        const QStringList entries = QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
        for (const QString& name : entries)
        {
            if (stopFlag.load())
                return found;
            if (name.startsWith(QLatin1Char('.')) || name.startsWith(QLatin1Char('{')) || isNeverCandidate(name))
                continue;
            const QString dir = QDir::cleanPath(root + QLatin1Char('/') + name);
            const FsSafety::RawInfo info = FsSafety::rawInfo(dir);
            // Liên kết, thư mục hệ thống (FILE_ATTRIBUTE_SYSTEM 0x4) -> không bao giờ.
            if (!info.exists || !info.isDir || info.isLink || (info.attributes & 0x4u))
                continue;
            if (belongsToInstalled(name, installedKeys))
                continue;
            if (!FsSafety::unsafeCleanupRootReason(dir, protectedDirs).isEmpty())
                continue;

            Leftover l;
            l.path = dir;
            l.newestWriteTime = info.lastWriteTime;
            bool cloud = false;
            FsSafety::walkFiles(dir, stopFlag, [&](const FsSafety::WalkEntry& e) {
                if (e.cloudOnly())
                {
                    cloud = true;
                    return;
                }
                l.sizeBytes += e.sizeBytes;
                ++l.fileCount;
                if (e.lastWriteTime > l.newestWriteTime)
                    l.newestWriteTime = e.lastWriteTime;
            });
            if (stopFlag.load())
                return found;
            if (cloud || l.sizeBytes < minSizeBytes || l.newestWriteTime > idleLimit)
                continue;
            found << l;
        }
    }
    std::sort(found.begin(), found.end(), [](const Leftover& a, const Leftover& b) { return a.sizeBytes > b.sizeBytes; });
    return found;
}
} // namespace LeftoverFinder
