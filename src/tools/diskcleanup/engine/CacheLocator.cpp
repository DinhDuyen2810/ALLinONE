#include "CacheLocator.h"

#include "FsSafety.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace
{
/// Thư mục con THẬT (không phải junction/symlink) của 'dir'.
QStringList realSubdirs(const QString& dir)
{
    QStringList out;
    if (dir.isEmpty()) // QDir("") là thư mục làm việc hiện hành - KHÔNG BAO GIỜ được liệt kê nhầm nó
        return out;
    QDir d(dir);
    if (!d.exists())
        return out;
    for (const QString& name : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
    {
        const QString path = dir + QLatin1Char('/') + name;
        const FsSafety::RawInfo info = FsSafety::rawInfo(path);
        if (info.exists && info.isDir && !info.isLink)
            out << path;
    }
    return out;
}

bool isRealDir(const QString& path)
{
    const FsSafety::RawInfo info = FsSafety::rawInfo(path);
    return info.exists && info.isDir && !info.isLink;
}

bool isRealFile(const QString& path)
{
    const FsSafety::RawInfo info = FsSafety::rawInfo(path);
    return info.exists && !info.isDir && !info.isLink;
}

void addExisting(QStringList* out, const QString& path)
{
    if (isRealDir(path) && !out->contains(path, Qt::CaseInsensitive))
        *out << path;
}
} // namespace

namespace CacheLocator
{

QStringList chromiumProfileCacheNames()
{
    return {"Cache", "Code Cache", "GPUCache", "DawnCache", "DawnGraphiteCache", "DawnWebGPUCache"};
}

QStringList chromiumUserDataCacheNames()
{
    return {"ShaderCache", "GrShaderCache", "GraphiteDawnCache", "GPUPersistentCache", "DawnCache", "DawnGraphiteCache",
            "DawnWebGPUCache"};
}

bool isChromiumProfileDir(const QString& dir)
{
    const QString name = QFileInfo(dir).fileName();
    if (name == QLatin1String("Default") || name == QLatin1String("Guest Profile") ||
        name.startsWith(QLatin1String("Profile ")))
        return true;
    return isRealFile(dir + "/Preferences");
}

QStringList chromiumBrowserCaches(const QString& userData)
{
    QStringList out;
    if (userData.isEmpty() || !isRealDir(userData))
        return out;
    for (const QString& n : chromiumUserDataCacheNames())
        addExisting(&out, userData + QLatin1Char('/') + n);
    for (const QString& sub : realSubdirs(userData))
    {
        if (!isChromiumProfileDir(sub))
            continue;
        for (const QString& n : chromiumProfileCacheNames())
            addExisting(&out, sub + QLatin1Char('/') + n);
    }
    return out;
}

QStringList electronAppCaches(const QString& appDataDir, const QStringList& extraNames)
{
    QStringList out;
    if (appDataDir.isEmpty() || !isRealDir(appDataDir))
        return out;
    for (const QString& n : chromiumProfileCacheNames() + chromiumUserDataCacheNames() + extraNames)
        addExisting(&out, appDataDir + QLatin1Char('/') + n);
    return out;
}

QStringList chromiumCachesUnder(const QString& root, int maxDepth)
{
    QStringList found;
    if (root.isEmpty() || !isRealDir(root))
        return found;
    static const QStringList names{"Cache", "Code Cache", "GPUCache"};
    // Duyệt theo chiều rộng tới maxDepth cấp; không đi vào junction/symlink.
    QList<QPair<QString, int>> queue{{root, 0}};
    int visited = 0;
    while (!queue.isEmpty() && visited < 20000)
    {
        const auto [dir, depth] = queue.takeFirst();
        ++visited;
        for (const QString& sub : realSubdirs(dir))
        {
            const QString name = QFileInfo(sub).fileName();
            bool inside = false;
            for (const QString& f : found)
                if (sub.startsWith(f + QLatin1Char('/'), Qt::CaseInsensitive))
                    inside = true;
            if (inside)
                continue;
            if (names.contains(name))
            {
                const QString parent = dir;
                const bool chromiumLayout = isRealDir(parent + "/Code Cache") || isRealDir(parent + "/GPUCache") ||
                                            isRealDir(parent + "/Cache/Cache_Data");
                if (chromiumLayout)
                {
                    found << sub;
                    continue;
                }
            }
            if (depth + 1 < maxDepth)
                queue.append({sub, depth + 1});
        }
    }
    return found;
}

QList<ChromiumApp> otherChromiumApps(const QStringList& parents, const QSet<QString>& skipNamesLower)
{
    QList<ChromiumApp> apps;
    auto consider = [&](const QString& dir, const QString& displayName) -> bool {
        if (!isRealFile(dir + "/Local State") && !isRealFile(dir + "/Preferences"))
            return false;
        ChromiumApp app;
        app.name = displayName;
        app.dir = dir;
        // "Cache" CHỈ lấy khi đúng bố cục Chromium (có Cache_Data hoặc cạnh Code Cache/GPUCache) - một ứng dụng
        // có thể tự đặt tên "Cache" cho dữ liệu riêng của nó.
        // Electron đặt bộ nhớ đệm ngay trong thư mục dữ liệu; WebView2/CEF ("EBWebView", "User Data") đặt trong các
        // HỒ SƠ con (Default/...) - lấy cả hai kiểu.
        QStringList candidates = electronAppCaches(dir);
        for (const QString& c : chromiumBrowserCaches(dir))
            if (!candidates.contains(c, Qt::CaseInsensitive))
                candidates << c;
        for (const QString& c : candidates)
        {
            const QString parent = QFileInfo(c).path();
            if (QFileInfo(c).fileName() == QLatin1String("Cache") && !isRealDir(c + "/Cache_Data") &&
                !isRealDir(parent + "/Code Cache") && !isRealDir(parent + "/GPUCache"))
                continue;
            app.caches << c;
        }
        if (!app.caches.isEmpty())
            apps << app;
        return true;
    };
    for (const QString& parent : parents)
    {
        if (parent.isEmpty())
            continue;
        for (const QString& sub : realSubdirs(parent))
        {
            const QString name = QFileInfo(sub).fileName();
            if (skipNamesLower.contains(name.toLower()) || name.startsWith(QLatin1Char('.')))
                continue;
            if (consider(sub, name))
                continue;
            // Một số ứng dụng đặt dữ liệu Chromium sâu thêm một cấp (vd "<Hãng>/<Ứng dụng>").
            for (const QString& sub2 : realSubdirs(sub))
            {
                const QString name2 = QFileInfo(sub2).fileName();
                if (skipNamesLower.contains(name2.toLower()))
                    continue;
                // "<Ứng dụng>/EBWebView" (WebView2), "<Ứng dụng>/User Data" (CEF): tên ứng dụng là thư mục cha.
                const bool genericName = name2.compare(QLatin1String("EBWebView"), Qt::CaseInsensitive) == 0 ||
                                         name2.compare(QLatin1String("User Data"), Qt::CaseInsensitive) == 0;
                consider(sub2, genericName ? name : name + QLatin1Char('/') + name2);
            }
        }
    }
    return apps;
}

QStringList parseSteamLibraryFolders(const QString& vdfText)
{
    QStringList out;
    static const QRegularExpression re(QStringLiteral("\"path\"\\s+\"((?:[^\"\\\\]|\\\\.)*)\""),
                                       QRegularExpression::CaseInsensitiveOption);
    auto it = re.globalMatch(vdfText);
    while (it.hasNext())
    {
        QString p = it.next().captured(1);
        p.replace(QStringLiteral("\\\\"), QStringLiteral("\\"));
        p = QDir::cleanPath(QDir::fromNativeSeparators(p));
        if (!p.isEmpty() && !out.contains(p, Qt::CaseInsensitive))
            out << p;
    }
    return out;
}

QStringList steamLibraries(const QString& steamRoot)
{
    QStringList libs;
    if (steamRoot.isEmpty())
        return libs;
    const QString root = QDir::cleanPath(QDir::fromNativeSeparators(steamRoot));
    if (isRealDir(root))
        libs << root;
    QFile vdf(root + "/steamapps/libraryfolders.vdf");
    if (vdf.open(QIODevice::ReadOnly))
    {
        const QString text = QString::fromUtf8(vdf.read(1 << 20));
        for (const QString& p : parseSteamLibraryFolders(text))
            if (isRealDir(p) && !libs.contains(p, Qt::CaseInsensitive))
                libs << p;
    }
    return libs;
}
} // namespace CacheLocator
