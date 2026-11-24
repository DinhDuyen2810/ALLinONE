#include "CategoryRegistry.h"

#include "CacheLocator.h"
#include "CleanupSystem.h"
#include "FsSafety.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

#include <functional>

namespace
{
/// "X:/..." hoặc UNC - dạng DUY NHẤT được chấp nhận làm gốc để ghép đường dẫn hạng mục.
bool isDriveAbsolute(const QString& path)
{
    static const QRegularExpression driveForm(QStringLiteral("^[A-Za-z]:/"));
    return path.startsWith(QLatin1String("//")) || driveForm.match(path + "/").hasMatch();
}

/// Chuẩn hóa một đường dẫn thư mục: rỗng/không tuyệt đối -> chuỗi RỖNG (không dùng được).
QString absoluteDir(const QString& raw)
{
    const QString value = QDir::cleanPath(QDir::fromNativeSeparators(raw.trimmed()));
    return (!value.isEmpty() && value != "." && isDriveAbsolute(value)) ? value : QString();
}

/// Biến môi trường thiếu, rỗng hoặc không phải đường dẫn tuyệt đối -> chuỗi RỖNG (không dùng được).
QString absoluteEnvDir(const char* name)
{
    return absoluteDir(qEnvironmentVariable(name));
}

/// Thư mục cài Steam: HKCU\Software\Valve\Steam\SteamPath, dự phòng "Program Files (x86)/Steam".
QString detectSteamDir()
{
#ifdef Q_OS_WIN
    QSettings reg(QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"), QSettings::NativeFormat);
    const QString fromRegistry = absoluteDir(reg.value("SteamPath").toString());
    if (!fromRegistry.isEmpty() && QFileInfo(fromRegistry).isDir())
        return fromRegistry;
#endif
    const QString pf86 = absoluteEnvDir("ProgramFiles(x86)");
    if (!pf86.isEmpty() && QFileInfo(pf86 + "/Steam").isDir())
        return pf86 + "/Steam";
    return {};
}
} // namespace

CleanupEnvironment CleanupEnvironment::current()
{
    CleanupEnvironment env;
    env.tempDir = absoluteEnvDir("TEMP");
    // Hỏi thẳng Windows (GetWindowsDirectoryW) TRƯỚC, biến môi trường WINDIR chỉ là phương án dự phòng:
    // biến môi trường do tiến trình cha quyết định - WINDIR trỏ sang "D:/DuLieu" sẽ biến "D:/DuLieu/Temp",
    // "D:/DuLieu/Logs/*.log"... thành "tệp tạm hệ thống" được tick sẵn. Không đoán "C:/Windows".
    env.windowsDir = FsSafety::windowsDirectory();
    if (env.windowsDir.isEmpty())
        env.windowsDir = absoluteEnvDir("WINDIR");
    env.localAppData = absoluteEnvDir("LOCALAPPDATA");
    env.roamingAppData = absoluteEnvDir("APPDATA");
    env.programData = absoluteEnvDir("ProgramData");
    env.userProfile = absoluteEnvDir("USERPROFILE");
    env.downloadsDir = absoluteDir(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    env.steamDir = detectSteamDir();
    static const QRegularExpression driveLetterRoot(QStringLiteral("^[A-Za-z]:/"));
    if (driveLetterRoot.match(env.windowsDir + "/").hasMatch())
        env.systemDrive = env.windowsDir.left(3);
    env.liveSystemQueries = true;
    env.protectedDirs = FsSafety::systemProtectedDirs();
    return env;
}

namespace
{
/// Chỉ giữ các đường dẫn thực sự tồn tại trên máy VÀ đủ an toàn để làm thư mục gốc dọn dẹp (xem
/// FsSafety::unsafeCleanupRootReason) - hạng mục không áp dụng (vd: chưa cài trình duyệt đó) hoặc có
/// đường dẫn đáng ngờ (TEMP trỏ vào gốc ổ đĩa/thư mục hồ sơ người dùng...) sẽ có rootPaths rỗng, tức
/// không hiện trong giao diện, thay vì đưa cả một thư mục dữ liệu thật vào danh sách "rác an toàn".
QStringList safeExistingOnly(const QStringList& candidates, const QStringList& protectedDirs)
{
    QStringList out;
    for (const QString& p : candidates)
    {
        if (p.isEmpty() || !FsSafety::unsafeCleanupRootReason(p, protectedDirs).isEmpty())
            continue;
        if (FsSafety::existsNoFollow(p) && !out.contains(p, Qt::CaseInsensitive))
            out << p;
    }
    return out;
}

/// Ghép đường dẫn con - trả về RỖNG nếu thư mục gốc rỗng, để không bao giờ sinh ra "/Temp", "/Logs"...
QString under(const QString& base, const QString& relative)
{
    return base.isEmpty() ? QString() : (base + relative);
}

QStringList realSubdirsNamed(const QString& dir, const std::function<bool(const QString&)>& accept)
{
    QStringList out;
    if (dir.isEmpty())
        return out;
    for (const QString& name : QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden))
        if (accept(name))
            out << dir + QLatin1Char('/') + name;
    return out;
}

/// Firefox đặt cache đĩa trong %LOCALAPPDATA%/Mozilla/Firefox/Profiles/<mã>.<tên>/cache2 (không phải
/// %APPDATA% - nơi chứa dữ liệu hồ sơ). Bản cũ/hồ sơ di chuyển tay có thể có cache2 ở %APPDATA%: lấy cả hai.
QStringList firefoxCacheDirs(const QString& localAppData, const QString& roamingAppData)
{
    QStringList out;
    auto collect = [&](const QString& base, const QStringList& names) {
        if (base.isEmpty())
            return;
        const QString profilesRoot = base + "/Mozilla/Firefox/Profiles";
        for (const QString& profile : realSubdirsNamed(profilesRoot, [](const QString&) { return true; }))
            for (const QString& n : names)
                if (QFileInfo::exists(profile + QLatin1Char('/') + n))
                    out << profile + QLatin1Char('/') + n;
    };
    collect(localAppData, {"cache2", "startupCache", "jumpListCache"});
    collect(roamingAppData, {"cache2"});
    return out;
}

/// Tệp tạm vừa sửa trong 5 phút gần đây nhiều khả năng thuộc một chương trình/trình cài đặt ĐANG chạy.
constexpr int kTempMinAgeSeconds = 5 * 60;
/// Gói cập nhật được ghi trong 24 giờ qua có thể thuộc một bản cập nhật Windows đang tải/cài dở.
constexpr int kUpdateMinAgeSeconds = 24 * 3600;

const QString kGroupWindows = QStringLiteral("Windows");
const QString kGroupBrowsers = QStringLiteral("Trình duyệt");
const QString kGroupApps = QStringLiteral("Ứng dụng & trò chơi");
const QString kGroupShaders = QStringLiteral("Bộ nhớ đệm đồ họa (shader)");
const QString kGroupDev = QStringLiteral("Công cụ lập trình");
const QString kGroupReview = QStringLiteral("Tệp của bạn - xem kỹ từng mục");
} // namespace

QStringList CategoryRegistry::groupOrder()
{
    return {kGroupWindows, kGroupBrowsers, kGroupApps, kGroupShaders, kGroupDev, kGroupReview};
}

bool CategoryRegistry::isAvailable(const CleanupCategory& category)
{
    return category.kind == CleanupKind::FolderContents ? !category.rootPaths.isEmpty() : !category.searchRoots.isEmpty();
}

QList<CleanupCategory> CategoryRegistry::buildCategories(const CleanupEnvironment& env)
{
    QList<CleanupCategory> result;

    // Thư mục Windows luôn được bảo vệ (không bao giờ là gốc dọn dẹp) dù env do ai dựng - các thư mục
    // CON của nó (Temp, Prefetch...) thì vẫn hợp lệ.
    QStringList protectedDirs = env.protectedDirs;
    if (!env.windowsDir.isEmpty())
        protectedDirs << env.windowsDir;

    // Thư mục gốc để GHÉP đường dẫn hạng mục cũng phải hợp lý: windowsDir là gốc ổ đĩa ("D:/") sẽ sinh ra
    // "D:/Temp", "D:/Logs" - thư mục dữ liệu bình thường của người dùng chứ không phải của Windows.
    auto usableBase = [](const QString& dir) {
        return FsSafety::unsafeCleanupRootReason(dir, {}).isEmpty() ? dir : QString();
    };
    const QString windowsDir = usableBase(env.windowsDir);
    const QString localAppData = usableBase(env.localAppData);
    const QString roamingAppData = usableBase(env.roamingAppData);
    const QString programData = usableBase(env.programData);
    const QString userProfile = usableBase(env.userProfile);
    // LocalLow nằm cạnh Local (không có biến môi trường riêng).
    const QString localLow = localAppData.isEmpty() ? QString() : usableBase(QFileInfo(localAppData).path() + "/LocalLow");

    auto add = [&](CleanupCategoryId id, const QString& group, const QString& name, const QString& desc, CleanupRisk risk,
                   const QStringList& roots, const QStringList& patterns = {}, bool scanSub = true,
                   bool keepRoot = true) -> CleanupCategory& {
        CleanupCategory c;
        c.id = id;
        c.group = group;
        c.name = name;
        c.description = desc;
        c.risk = risk;
        c.rootPaths = safeExistingOnly(roots, protectedDirs);
        c.namePatterns = patterns;
        c.scanSubdirectories = scanSub;
        c.keepRootFolder = keepRoot;
        result.push_back(c);
        return result.last();
    };

    // ======================= Windows =======================
    add(CleanupCategoryId::UserTemp, kGroupWindows, "Tệp tạm của người dùng",
        "Tệp tạm do Windows và các ứng dụng tạo ra trong quá trình chạy, không còn cần thiết sau khi đóng ứng dụng. "
        "Mục vừa được ghi trong 5 phút gần đây được giữ lại (có thể thuộc một chương trình đang chạy).",
        CleanupRisk::Safe, {env.tempDir})
        .minAgeSeconds = kTempMinAgeSeconds;

    {
        CleanupCategory& c = add(CleanupCategoryId::WindowsTemp, kGroupWindows, "Tệp tạm hệ thống Windows",
                                 "Tệp tạm do chính Windows tạo ra (cần quyền Administrator để xóa hết).", CleanupRisk::Safe,
                                 {under(windowsDir, "/Temp")});
        c.minAgeSeconds = kTempMinAgeSeconds;
        c.needsAdmin = true;
    }
    {
        CleanupCategory& c = add(CleanupCategoryId::WindowsUpdateCache, kGroupWindows, "Bộ nhớ đệm Windows Update",
                                 "Các gói cập nhật đã tải về và cài đặt xong, Windows Update sẽ tải lại nếu cần. Gói vừa "
                                 "được ghi trong 24 giờ qua được giữ lại (có thể đang cài dở).",
                                 CleanupRisk::Safe, {under(windowsDir, "/SoftwareDistribution/Download")});
        c.minAgeSeconds = kUpdateMinAgeSeconds;
        c.needsAdmin = true;
    }
    {
        // Windows 10/11 để bộ nhớ đệm Delivery Optimization trong hồ sơ dịch vụ NetworkService - đường dẫn cũ
        // SoftwareDistribution/DeliveryOptimization (Windows 10 bản đầu) chỉ còn là phương án dự phòng.
        CleanupCategory& c = add(
            CleanupCategoryId::DeliveryOptimization, kGroupWindows, "Bộ nhớ đệm Delivery Optimization",
            "Bản sao cục bộ của các gói cập nhật dùng để chia sẻ qua mạng LAN/Internet (Windows Update).",
            CleanupRisk::Safe,
            {under(windowsDir, "/ServiceProfiles/NetworkService/AppData/Local/Microsoft/Windows/DeliveryOptimization/Cache"),
             under(windowsDir, "/SoftwareDistribution/DeliveryOptimization")});
        c.minAgeSeconds = kUpdateMinAgeSeconds;
        c.needsAdmin = true;
    }

    add(CleanupCategoryId::ThumbnailCache, kGroupWindows, "Bộ nhớ đệm hình thu nhỏ",
        "Ảnh thu nhỏ Windows Explorer lưu lại để hiển thị nhanh - sẽ tự tạo lại khi cần. Tệp Explorer đang mở được bỏ qua.",
        CleanupRisk::Safe, {under(localAppData, "/Microsoft/Windows/Explorer")}, {"thumbcache_*.db", "iconcache_*.db"},
        false, true);

    add(CleanupCategoryId::WindowsErrorReports, kGroupWindows, "Báo cáo lỗi Windows (WER)",
        "Báo cáo lỗi và tệp kết xuất (dump) Windows thu thập khi ứng dụng của bạn gặp sự cố.", CleanupRisk::Safe,
        {under(localAppData, "/Microsoft/Windows/WER/ReportArchive"), under(localAppData, "/Microsoft/Windows/WER/ReportQueue"),
         under(localAppData, "/Microsoft/Windows/WER/Temp")});
    add(CleanupCategoryId::SystemErrorReports, kGroupWindows, "Báo cáo lỗi Windows toàn máy (WER)",
        "Báo cáo lỗi của dịch vụ/ứng dụng chạy cho toàn máy (ProgramData) - cần quyền Administrator để xóa.",
        CleanupRisk::Safe,
        {under(programData, "/Microsoft/Windows/WER/ReportArchive"), under(programData, "/Microsoft/Windows/WER/ReportQueue"),
         under(programData, "/Microsoft/Windows/WER/Temp")})
        .needsAdmin = true;

    add(CleanupCategoryId::AppCrashDumps, kGroupWindows, "Tệp kết xuất khi ứng dụng bị lỗi (CrashDumps)",
        "Tệp .dmp Windows ghi lại khi một ứng dụng bị đóng do lỗi - chỉ hữu ích khi cần gửi cho nhà phát triển.",
        CleanupRisk::Safe, {under(localAppData, "/CrashDumps")});

    {
        // MEMORY.DMP là TỆP: chính tệp đó là một mục (xem CleanupKind::FolderContents).
        CleanupCategory& c =
            add(CleanupCategoryId::MemoryDumps, kGroupWindows, "Tệp kết xuất bộ nhớ (memory dump)",
                "Tệp dump khi Windows gặp lỗi nghiêm trọng (màn hình xanh, treo trình điều khiển) - chỉ hữu ích khi cần "
                "chẩn đoán lỗi đó. Gồm Minidump, LiveKernelReports và MEMORY.DMP (có thể nhiều GB).",
                CleanupRisk::Safe,
                {under(windowsDir, "/Minidump"), under(windowsDir, "/LiveKernelReports"), under(windowsDir, "/MEMORY.DMP")});
        c.needsAdmin = true;
    }

    add(CleanupCategoryId::Prefetch, kGroupWindows, "Dữ liệu Prefetch",
        "Windows dùng để tăng tốc khởi động ứng dụng - xóa an toàn nhưng lần mở đầu tiên sau đó có thể chậm hơn một chút.",
        CleanupRisk::Caution, {under(windowsDir, "/Prefetch")}, {"*.pf"})
        .needsAdmin = true;

    add(CleanupCategoryId::RecentItems, kGroupWindows, "Danh sách tệp gần đây",
        "Danh sách lối tắt tới các tệp/thư mục đã mở gần đây (hiện trong menu Start, thanh địa chỉ).",
        CleanupRisk::Caution, {under(roamingAppData, "/Microsoft/Windows/Recent")})
        // Hai thư mục con này giữ các mục người dùng tự GHIM (jump list trên thanh tác vụ, Quick Access) -
        // không phải danh sách "gần đây" Windows tự dựng lại được, xóa đi là mất hẳn các mục ghim.
        .excludeNames = {"AutomaticDestinations", "CustomDestinations"};

    add(CleanupCategoryId::SystemLogs, kGroupWindows, "Tệp nhật ký hệ thống (.log)",
        "Tệp nhật ký văn bản do Windows và trình cài đặt tạo ra.", CleanupRisk::Caution,
        {under(windowsDir, "/Logs"), under(windowsDir, "/Temp")}, {"*.log"})
        .needsAdmin = true;

    {
        CleanupCategory& c = add(CleanupCategoryId::RecycleBin, kGroupWindows, "Thùng rác",
                                 "Các tệp đã xóa vào Thùng rác trên mọi ổ đĩa - làm trống là XÓA HẲN, không khôi phục được.",
                                 CleanupRisk::Caution, {});
        c.kind = CleanupKind::RecycleBin;
        c.regenerable = false;
        if (env.liveSystemQueries)
            c.searchRoots = {QStringLiteral("::RecycleBin")};
    }
    {
        CleanupCategory& c = add(
            CleanupCategoryId::Hibernation, kGroupWindows, "Tệp ngủ đông (hiberfil.sys)",
            "Tệp ẩn Windows dùng cho chế độ Ngủ đông và Khởi động nhanh, thường 40% dung lượng RAM. Dọn = TẮT ngủ đông "
            "bằng chính lệnh của Windows (powercfg /hibernate off, cần xác nhận quyền Quản trị): mất chế độ Ngủ đông và "
            "Khởi động nhanh. Bật lại: mở PowerShell (Admin) gõ powercfg /hibernate on.",
            CleanupRisk::High, {});
        c.kind = CleanupKind::Hibernation;
        c.regenerable = false;
        // Không cần ỨNG DỤNG chạy với quyền Quản trị: chính powercfg được chạy nâng quyền (Windows hỏi UAC).
        c.note = "Dọn = tắt Ngủ đông + Khởi động nhanh (Windows hỏi quyền UAC)";
        const QString hiber = CleanupSystem::hibernationFilePath(env.systemDrive);
        if (!hiber.isEmpty() && CleanupSystem::hibernationFileSize(env.systemDrive) > 0)
            c.searchRoots = {hiber};
    }

    {
        CleanupCategory& c = add(CleanupCategoryId::WindowsOld, kGroupWindows, "Windows.old (bản Windows cũ)",
                                 "Toàn bộ hệ điều hành Windows phiên bản trước, Windows giữ lại ~10 ngày sau khi nâng cấp để có "
                                 "thể khôi phục. XÓA KHÔNG THỂ HOÀN TÁC - chỉ xóa khi chắc chắn không cần quay lại phiên bản "
                                 "cũ. Nếu xóa không hết (tệp thuộc TrustedInstaller), dùng Cài đặt > Hệ thống > Lưu trữ > "
                                 "Tệp tạm thời > \"Bản cài đặt Windows trước đó\".",
                                 CleanupRisk::High,
                                 {isDriveAbsolute(windowsDir) && windowsDir.at(1) == QLatin1Char(':')
                                      ? windowsDir.left(3) + "Windows.old"
                                      : QString()},
                                 {}, true, false);
        c.regenerable = false;
        c.needsAdmin = true;
    }

    // ======================= Trình duyệt =======================
    // Mọi hồ sơ (Default, Profile N, Guest Profile) + bộ nhớ đệm đồ họa dùng chung của "User Data". Bản cũ chỉ
    // dọn "Default" - Chrome nhiều hồ sơ (Profile 2/4/5...) bị bỏ sót toàn bộ.
    auto browser = [&](CleanupCategoryId id, const QString& name, const QString& userData, const QStringList& exes,
                       const QString& label) {
        CleanupCategory& c = add(id, kGroupBrowsers, "Bộ nhớ đệm " + name,
                                 "Tệp cache của " + name + " ở MỌI hồ sơ (trang web, mã JavaScript đã biên dịch, đồ họa) - "
                                 "trang web sẽ tải lại từ đầu lần truy cập sau. Không đụng tới lịch sử, mật khẩu, cookie.",
                                 CleanupRisk::Safe, CacheLocator::chromiumBrowserCaches(userData));
        c.processNames = exes;
        c.appLabel = label;
    };
    browser(CleanupCategoryId::BrowserCacheChrome, "Google Chrome", under(localAppData, "/Google/Chrome/User Data"),
            {"chrome.exe"}, "Chrome");
    browser(CleanupCategoryId::BrowserCacheEdge, "Microsoft Edge", under(localAppData, "/Microsoft/Edge/User Data"),
            {"msedge.exe"}, "Edge");
    browser(CleanupCategoryId::BrowserCacheBrave, "Brave", under(localAppData, "/BraveSoftware/Brave-Browser/User Data"),
            {"brave.exe"}, "Brave");
    browser(CleanupCategoryId::BrowserCacheVivaldi, "Vivaldi", under(localAppData, "/Vivaldi/User Data"), {"vivaldi.exe"},
            "Vivaldi");
    browser(CleanupCategoryId::BrowserCacheChromium, "Chromium", under(localAppData, "/Chromium/User Data"), {},
            "Chromium");
    {
        QStringList roots;
        for (const QString& dir : {under(localAppData, "/Opera Software/Opera Stable"),
                                   under(localAppData, "/Opera Software/Opera GX Stable")})
        {
            roots += CacheLocator::electronAppCaches(dir);
            roots += CacheLocator::electronAppCaches(dir.isEmpty() ? QString() : dir + "/Default");
        }
        CleanupCategory& c = add(CleanupCategoryId::BrowserCacheOpera, kGroupBrowsers, "Bộ nhớ đệm Opera / Opera GX",
                                 "Tệp cache trình duyệt Opera và Opera GX - trang web sẽ tải lại từ đầu lần truy cập sau.",
                                 CleanupRisk::Safe, roots);
        c.processNames = {"opera.exe"};
        c.appLabel = "Opera";
    }
    {
        CleanupCategory& c = add(CleanupCategoryId::BrowserCacheFirefox, kGroupBrowsers, "Bộ nhớ đệm Firefox",
                                 "Tệp cache trình duyệt Firefox ở mọi hồ sơ - trang web sẽ tải lại từ đầu lần truy cập sau.",
                                 CleanupRisk::Safe, firefoxCacheDirs(localAppData, roamingAppData));
        c.processNames = {"firefox.exe"};
        c.appLabel = "Firefox";
    }

    // ======================= Ứng dụng & trò chơi =======================
    auto app = [&](CleanupCategoryId id, const QString& name, const QString& desc, const QStringList& roots,
                   const QStringList& exes, const QString& label, CleanupRisk risk = CleanupRisk::Safe) -> CleanupCategory& {
        CleanupCategory& c = add(id, kGroupApps, name, desc, risk, roots);
        c.processNames = exes;
        c.appLabel = label;
        return c;
    };
    const QString electronDesc = " - chỉ thư mục bộ nhớ đệm kiểu Chromium (Cache, Code Cache, GPUCache...), không đụng tới "
                                 "đăng nhập, tin nhắn hay cài đặt.";
    {
        QStringList roots;
        for (const char* d : {"/discord", "/discordptb", "/discordcanary"})
            roots += CacheLocator::electronAppCaches(under(roamingAppData, d));
        app(CleanupCategoryId::AppCacheDiscord, "Bộ nhớ đệm Discord", "Ảnh, video, mã đã tải của Discord" + electronDesc, roots,
            {"discord.exe", "discordptb.exe", "discordcanary.exe"}, "Discord");
    }
    app(CleanupCategoryId::AppCacheSlack, "Bộ nhớ đệm Slack", "Tệp đã tải của Slack" + electronDesc,
        CacheLocator::electronAppCaches(under(roamingAppData, "/Slack")), {"slack.exe"}, "Slack");
    {
        // Teams cổ điển (Electron) + Teams mới (WebView2 trong gói Store: hồ sơ Default / WV2Profile*).
        QStringList roots = CacheLocator::electronAppCaches(under(roamingAppData, "/Microsoft/Teams"));
        const QString ebweb =
            under(localAppData, "/Packages/MSTeams_8wekyb3d8bbwe/LocalCache/Microsoft/MSTeams/EBWebView");
        for (const QString& profile : realSubdirsNamed(ebweb, [](const QString& n) {
                 return n == QLatin1String("Default") || n.startsWith(QLatin1String("WV2Profile"));
             }))
            for (const QString& n : CacheLocator::chromiumProfileCacheNames())
                if (QFileInfo(profile + QLatin1Char('/') + n).isDir())
                    roots << profile + QLatin1Char('/') + n;
        app(CleanupCategoryId::AppCacheTeams, "Bộ nhớ đệm Microsoft Teams", "Bộ nhớ đệm web của Teams (bản cổ điển và bản mới)" +
            electronDesc, roots, {"teams.exe", "ms-teams.exe"}, "Teams");
    }
    {
        QStringList roots = CacheLocator::electronAppCaches(under(localAppData, "/Spotify/Browser"));
        roots << under(localAppData, "/Spotify/Data")
              << under(localAppData, "/Packages/SpotifyAB.SpotifyMusic_zpdnekdrzrea0/LocalCache/Spotify/Data");
        app(CleanupCategoryId::AppCacheSpotify, "Bộ nhớ đệm Spotify",
            "Nhạc Spotify lưu tạm khi nghe trực tuyến + bộ nhớ đệm trình duyệt bên trong. KHÔNG đụng tới nhạc tải về nghe "
            "ngoại tuyến (thư mục Storage).",
            roots, {"spotify.exe"}, "Spotify");
    }
    {
        QStringList roots;
        for (const char* d : {"/Code", "/Code - Insiders", "/Cursor"})
            roots += CacheLocator::electronAppCaches(under(roamingAppData, d), {"CachedData", "CachedExtensionVSIXs"});
        app(CleanupCategoryId::AppCacheVsCode, "Bộ nhớ đệm VS Code / Cursor",
            "Mã đã biên dịch sẵn (CachedData), gói tiện ích mở rộng đã tải (CachedExtensionVSIXs) và bộ nhớ đệm web của VS "
            "Code/Cursor - tự tạo lại. Không đụng tới tiện ích đã cài, cài đặt hay dự án.",
            roots, {"code.exe", "code - insiders.exe", "cursor.exe"}, "VS Code");
    }
    app(CleanupCategoryId::AppCacheSteam, "Bộ nhớ đệm trình duyệt Steam",
        "Bộ nhớ đệm trang cửa hàng/cộng đồng bên trong Steam (htmlcache) - không đụng tới trò chơi.",
        {under(localAppData, "/Steam/htmlcache")}, {"steam.exe", "steamwebhelper.exe"}, "Steam");
    app(CleanupCategoryId::AppCacheEpic, "Bộ nhớ đệm Epic Games Launcher",
        "Bộ nhớ đệm web của Epic Games Launcher (webcache) - không đụng tới trò chơi.",
        realSubdirsNamed(under(localAppData, "/EpicGamesLauncher/Saved"),
                         [](const QString& n) { return n.startsWith(QLatin1String("webcache"), Qt::CaseInsensitive); }),
        {"epicgameslauncher.exe"}, "Epic Games Launcher");
    {
        QStringList roots;
        for (const char* d : {"/Battle.net", "/Electronic Arts/EA Desktop", "/Ubisoft Game Launcher", "/Riot Games/Riot Client",
                              "/GOG.com/Galaxy"})
            roots += CacheLocator::chromiumCachesUnder(under(localAppData, d));
        app(CleanupCategoryId::AppCacheGameLaunchers, "Bộ nhớ đệm trình khởi chạy game",
            "Battle.net, EA app, Ubisoft Connect, Riot Client, GOG Galaxy nhúng một trình duyệt Chromium - chỉ dọn thư mục "
            "bộ nhớ đệm của trình duyệt đó, không bao giờ đụng tới đăng nhập hay dữ liệu trò chơi.",
            roots, {"battle.net.exe", "eadesktop.exe", "upc.exe", "riotclientservices.exe", "galaxyclient.exe"},
            "trình khởi chạy game");
    }
    {
        // Ứng dụng Electron/CEF khác (Zalo, Notion, Postman, Obsidian, WhatsApp...): nhận ra theo tệp "Local State"/
        // "Preferences" của Chromium trong thư mục dữ liệu - bỏ qua các mục đã có hạng mục riêng ở trên.
        static const QSet<QString> skip{"google", "microsoft", "bravesoftware", "opera software", "vivaldi", "mozilla",
                                        "chromium", "packages", "programs", "temp", "discord", "discordptb",
                                        "discordcanary", "slack", "code", "code - insiders", "cursor", "spotify", "steam",
                                        "epicgameslauncher", "battle.net", "electronic arts", "ubisoft game launcher",
                                        "riot games", "gog.com", "oneforall", "crashdumps", "d3dscache", "nvidia", "amd",
                                        "intel", "comms", "connecteddevicesplatform"};
        QStringList roots;
        QStringList names;
        for (const CacheLocator::ChromiumApp& a : CacheLocator::otherChromiumApps({roamingAppData, localAppData}, skip))
        {
            roots += a.caches;
            if (!names.contains(a.name, Qt::CaseInsensitive))
                names << a.name;
        }
        CleanupCategory& c = app(CleanupCategoryId::AppCacheOtherChromium, "Bộ nhớ đệm ứng dụng khác (Electron/Chromium)",
                                 "Ứng dụng máy tính dựng trên Chromium/Electron (vd Zalo, Notion, Postman, Obsidian...) - nhận "
                                 "ra theo tệp \"Local State\"/\"Preferences\" của Chromium" + electronDesc +
                                 " Nên đóng các ứng dụng này trước để dọn hết.",
                                 roots, {}, QString());
        if (!c.rootPaths.isEmpty())
            c.note = "Tìm thấy: " + names.join(", ");
    }

    // ======================= Bộ nhớ đệm đồ họa (shader) =======================
    // An toàn (tự tạo lại) nhưng KHÔNG tick sẵn: trò chơi phải dựng lại shader, lần chạy đầu có thể giật nhẹ.
    const QString shaderNote = " Tự tạo lại; lần chạy đầu sau khi dọn trò chơi có thể hơi giật trong lúc dựng lại.";
    add(CleanupCategoryId::ShaderCacheDirectX, kGroupShaders, "Bộ nhớ đệm shader DirectX",
        "Shader DirectX Windows đã biên dịch sẵn cho trò chơi/ứng dụng." + shaderNote, CleanupRisk::Caution,
        {under(localAppData, "/D3DSCache")});
    add(CleanupCategoryId::ShaderCacheGpuVendor, kGroupShaders, "Bộ nhớ đệm shader NVIDIA / AMD / Intel",
        "Shader trình điều khiển card đồ họa đã biên dịch sẵn (DXCache, GLCache, NV_Cache, VkCache...)." + shaderNote +
            " Phần của toàn máy (ProgramData) cần quyền Administrator.",
        CleanupRisk::Caution,
        {under(localAppData, "/NVIDIA/DXCache"), under(localAppData, "/NVIDIA/GLCache"),
         under(localLow, "/NVIDIA/PerDriverVersion/DXCache"), under(localAppData, "/NVIDIA Corporation/NV_Cache"),
         under(programData, "/NVIDIA Corporation/NV_Cache"), under(localAppData, "/AMD/DxCache"),
         under(localAppData, "/AMD/DxcCache"), under(localAppData, "/AMD/GLCache"), under(localAppData, "/AMD/VkCache"),
         under(localAppData, "/Intel/ShaderCache"), under(localLow, "/Intel/ShaderCache")});
    {
        QStringList roots;
        for (const QString& lib : CacheLocator::steamLibraries(env.steamDir))
            roots << lib + "/steamapps/shadercache";
        CleanupCategory& c = add(CleanupCategoryId::ShaderCacheSteamGames, kGroupShaders, "Bộ nhớ đệm shader trò chơi Steam",
                                 "Shader Steam tải/biên dịch sẵn cho từng trò chơi (steamapps/shadercache) ở mọi thư viện "
                                 "Steam." + shaderNote,
                                 CleanupRisk::Caution, roots);
        c.processNames = {"steam.exe"};
        c.appLabel = "Steam";
    }

    // ======================= Công cụ lập trình =======================
    add(CleanupCategoryId::DevCacheNode, kGroupDev, "Bộ nhớ đệm npm / Yarn / pnpm / Electron",
        "Gói đã tải của npm, Yarn, pnpm (siêu dữ liệu), tệp Electron/node-gyp đã tải - lần cài sau tự tải lại. KHÔNG đụng "
        "tới node_modules của dự án hay kho pnpm (store, dùng liên kết cứng).",
        CleanupRisk::Safe,
        {under(localAppData, "/npm-cache/_cacache"), under(localAppData, "/npm-cache/_logs"), under(localAppData, "/Yarn/Cache"),
         under(localAppData, "/Yarn/Berry/cache"), under(localAppData, "/pnpm-cache"), under(localAppData, "/electron/Cache"),
         under(localAppData, "/electron-builder/Cache"), under(localAppData, "/node-gyp/Cache")})
        .processNames = {"node.exe"};
    result.last().appLabel = "Node.js";
    add(CleanupCategoryId::DevCachePython, kGroupDev, "Bộ nhớ đệm pip / uv (Python)",
        "Gói Python đã tải của pip và uv - lần cài sau tự tải lại. Không đụng tới môi trường ảo hay gói đã cài.",
        CleanupRisk::Safe, {under(localAppData, "/pip/Cache"), under(localAppData, "/uv/cache")});
    add(CleanupCategoryId::DevCacheNuGet, kGroupDev, "Bộ nhớ đệm NuGet (.NET)",
        "Bộ nhớ đệm HTTP của NuGet (v3-cache, plugins-cache) - không đụng tới thư mục gói ~/.nuget/packages.",
        CleanupRisk::Safe, {under(localAppData, "/NuGet/v3-cache"), under(localAppData, "/NuGet/plugins-cache")});
    add(CleanupCategoryId::DevCacheGo, kGroupDev, "Bộ nhớ đệm biên dịch Go", "Kết quả biên dịch Go lưu sẵn (go-build) - tự tạo lại.",
        CleanupRisk::Safe, {under(localAppData, "/go-build")});
    add(CleanupCategoryId::DevCacheGradle, kGroupDev, "Bộ nhớ đệm Gradle",
        "Thư viện và kết quả dựng Gradle đã tải (~/.gradle/caches) - lần dựng sau phải tải lại (có thể lâu). Nên chạy "
        "\"gradle --stop\" trước để dừng tiến trình nền đang giữ tệp.",
        CleanupRisk::Caution, {under(userProfile, "/.gradle/caches")});
    add(CleanupCategoryId::DevCacheCargo, kGroupDev, "Bộ nhớ đệm Cargo (Rust)",
        "Mã nguồn và gói crate đã tải (~/.cargo/registry, git/checkouts) - lần dựng sau phải tải lại.", CleanupRisk::Caution,
        {under(userProfile, "/.cargo/registry/cache"), under(userProfile, "/.cargo/registry/src"),
         under(userProfile, "/.cargo/git/checkouts")});

    // ======================= Tệp của bạn - xem kỹ từng mục =======================
    // Dữ liệu của NGƯỜI DÙNG: không bao giờ tick sẵn, hiện từng mục để tự chọn, mặc định vào Thùng rác.
    const QString downloads = absoluteDir(env.downloadsDir);
    const bool downloadsOk = !downloads.isEmpty() && FsSafety::unsafeCleanupRootReason(downloads, {}).isEmpty() &&
                             QFileInfo(downloads).isDir();
    {
        CleanupCategory& c = add(CleanupCategoryId::OldInstallers, kGroupReview, "Bộ cài đặt cũ trong Downloads",
                                 "Tệp cài đặt (.exe, .msi, .msix, .appx, .iso) nằm ngay trong thư mục Downloads, tải về hơn 7 "
                                 "ngày - thường đã cài xong. Tệp .exe trong thư mục con (ứng dụng chạy liền) không tính.",
                                 CleanupRisk::Caution, {});
        c.kind = CleanupKind::OldInstallers;
        c.regenerable = false;
        c.showItems = true;
        if (downloadsOk)
            c.searchRoots = {downloads};
    }
    {
        CleanupCategory& c = add(CleanupCategoryId::BigOldDownloads, kGroupReview, "Tệp tải về lớn lâu không dùng",
                                 "Tệp từ 250 MB trong Downloads (cả thư mục con) không được tạo/sửa/mở trong hơn 30 ngày. Tệp "
                                 "chỉ có trên mây (OneDrive) không tính.",
                                 CleanupRisk::Caution, {});
        c.kind = CleanupKind::BigOldDownloads;
        c.regenerable = false;
        c.showItems = true;
        if (downloadsOk)
            c.searchRoots = {downloads};
    }
    {
        CleanupCategory& c = add(CleanupCategoryId::AppDataLeftovers, kGroupReview, "Thư mục còn sót của ứng dụng đã gỡ",
                                 "Thư mục trong AppData (Local, Roaming, LocalLow) từ 50 MB, không thay đổi hơn 90 ngày và tên "
                                 "không khớp ứng dụng nào đang cài - thường là phần còn sót sau khi gỡ. Đây là SUY ĐOÁN theo "
                                 "tên: xem kỹ từng thư mục trước khi chọn.",
                                 CleanupRisk::High, {});
        c.kind = CleanupKind::AppDataLeftovers;
        c.regenerable = false;
        c.showItems = true;
        if (env.liveSystemQueries)
            for (const QString& r : {localAppData, roamingAppData, localLow})
                if (!r.isEmpty() && QFileInfo(r).isDir())
                    c.searchRoots << r;
    }

    return result;
}

QString CategoryRegistry::riskName(CleanupRisk risk)
{
    switch (risk)
    {
        case CleanupRisk::Safe:    return "An toàn";
        case CleanupRisk::Caution: return "Lưu ý";
        case CleanupRisk::High:    return "Rủi ro cao";
    }
    return "An toàn";
}

QString CategoryRegistry::categoryIdKey(CleanupCategoryId id)
{
    switch (id)
    {
        case CleanupCategoryId::UserTemp:              return "UserTemp";
        case CleanupCategoryId::WindowsTemp:           return "WindowsTemp";
        case CleanupCategoryId::RecycleBin:            return "RecycleBin";
        case CleanupCategoryId::WindowsUpdateCache:    return "WindowsUpdateCache";
        case CleanupCategoryId::DeliveryOptimization:  return "DeliveryOptimization";
        case CleanupCategoryId::ThumbnailCache:        return "ThumbnailCache";
        case CleanupCategoryId::WindowsErrorReports:   return "WindowsErrorReports";
        case CleanupCategoryId::MemoryDumps:           return "MemoryDumps";
        case CleanupCategoryId::Prefetch:              return "Prefetch";
        case CleanupCategoryId::RecentItems:           return "RecentItems";
        case CleanupCategoryId::SystemLogs:            return "SystemLogs";
        case CleanupCategoryId::BrowserCacheChrome:    return "BrowserCacheChrome";
        case CleanupCategoryId::BrowserCacheEdge:      return "BrowserCacheEdge";
        case CleanupCategoryId::BrowserCacheFirefox:   return "BrowserCacheFirefox";
        case CleanupCategoryId::WindowsOld:            return "WindowsOld";
        case CleanupCategoryId::AppCrashDumps:         return "AppCrashDumps";
        case CleanupCategoryId::BrowserCacheBrave:     return "BrowserCacheBrave";
        case CleanupCategoryId::BrowserCacheVivaldi:   return "BrowserCacheVivaldi";
        case CleanupCategoryId::BrowserCacheOpera:     return "BrowserCacheOpera";
        case CleanupCategoryId::BrowserCacheChromium:  return "BrowserCacheChromium";
        case CleanupCategoryId::AppCacheDiscord:       return "AppCacheDiscord";
        case CleanupCategoryId::AppCacheSlack:         return "AppCacheSlack";
        case CleanupCategoryId::AppCacheTeams:         return "AppCacheTeams";
        case CleanupCategoryId::AppCacheSpotify:       return "AppCacheSpotify";
        case CleanupCategoryId::AppCacheVsCode:        return "AppCacheVsCode";
        case CleanupCategoryId::AppCacheSteam:         return "AppCacheSteam";
        case CleanupCategoryId::AppCacheEpic:          return "AppCacheEpic";
        case CleanupCategoryId::AppCacheGameLaunchers: return "AppCacheGameLaunchers";
        case CleanupCategoryId::AppCacheOtherChromium: return "AppCacheOtherChromium";
        case CleanupCategoryId::ShaderCacheDirectX:    return "ShaderCacheDirectX";
        case CleanupCategoryId::ShaderCacheGpuVendor:  return "ShaderCacheGpuVendor";
        case CleanupCategoryId::ShaderCacheSteamGames: return "ShaderCacheSteamGames";
        case CleanupCategoryId::DevCacheNode:          return "DevCacheNode";
        case CleanupCategoryId::DevCachePython:        return "DevCachePython";
        case CleanupCategoryId::DevCacheNuGet:         return "DevCacheNuGet";
        case CleanupCategoryId::DevCacheGo:            return "DevCacheGo";
        case CleanupCategoryId::DevCacheGradle:        return "DevCacheGradle";
        case CleanupCategoryId::DevCacheCargo:         return "DevCacheCargo";
        case CleanupCategoryId::OldInstallers:         return "OldInstallers";
        case CleanupCategoryId::BigOldDownloads:       return "BigOldDownloads";
        case CleanupCategoryId::AppDataLeftovers:      return "AppDataLeftovers";
        case CleanupCategoryId::Hibernation:           return "Hibernation";
        case CleanupCategoryId::SystemErrorReports:    return "SystemErrorReports";
    }
    return "Unknown";
}
