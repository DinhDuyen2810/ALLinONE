#include "CategoryRegistry.h"

#include "FsSafety.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace
{
/// "X:/..." hoặc UNC - dạng DUY NHẤT được chấp nhận làm gốc để ghép đường dẫn hạng mục.
bool isDriveAbsolute(const QString& path)
{
    static const QRegularExpression driveForm(QStringLiteral("^[A-Za-z]:/"));
    return path.startsWith(QLatin1String("//")) || driveForm.match(path + "/").hasMatch();
}

/// Biến môi trường thiếu, rỗng hoặc không phải đường dẫn tuyệt đối -> chuỗi RỖNG (không dùng được).
QString absoluteEnvDir(const char* name)
{
    const QString value = QDir::cleanPath(QDir::fromNativeSeparators(qEnvironmentVariable(name).trimmed()));
    return (!value.isEmpty() && value != "." && isDriveAbsolute(value)) ? value : QString();
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

/// Firefox đặt cache trong %APPDATA%/Mozilla/Firefox/Profiles/<ký tự ngẫu nhiên>.<tên>/cache2 - cần
/// liệt kê thư mục con thật để tìm đúng từng profile.
QStringList firefoxCacheDirs(const QString& roamingAppData)
{
    QStringList out;
    if (roamingAppData.isEmpty())
        return out;
    const QString profilesRoot = roamingAppData + "/Mozilla/Firefox/Profiles";
    QDir dir(profilesRoot);
    if (!dir.exists())
        return out;

    for (const QFileInfo& entry : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
    {
        const QString cache2 = entry.absoluteFilePath() + "/cache2";
        if (QFileInfo::exists(cache2))
            out << cache2;
    }
    return out;
}
} // namespace

namespace
{
/// Tệp tạm vừa sửa trong 5 phút gần đây nhiều khả năng thuộc một chương trình/trình cài đặt ĐANG chạy.
constexpr int kTempMinAgeSeconds = 5 * 60;
} // namespace

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

    auto add = [&](CleanupCategoryId id, const QString& name, const QString& desc, CleanupRisk risk,
                   const QStringList& roots, const QStringList& patterns = {}, bool scanSub = true,
                   bool keepRoot = true) {
        CleanupCategory c;
        c.id = id;
        c.name = name;
        c.description = desc;
        c.risk = risk;
        c.rootPaths = safeExistingOnly(roots, protectedDirs);
        c.namePatterns = patterns;
        c.scanSubdirectories = scanSub;
        c.keepRootFolder = keepRoot;
        result.push_back(c);
    };

    add(CleanupCategoryId::UserTemp, "Tệp tạm của người dùng",
       "Tệp tạm do Windows và các ứng dụng tạo ra trong quá trình chạy, không còn cần thiết sau khi đóng ứng dụng.",
       CleanupRisk::Safe, {env.tempDir});
    result.last().minAgeSeconds = kTempMinAgeSeconds;

    add(CleanupCategoryId::WindowsTemp, "Tệp tạm hệ thống Windows",
       "Tệp tạm do chính Windows tạo ra (cần quyền Administrator để xóa hết).", CleanupRisk::Safe,
       {under(windowsDir, "/Temp")});
    result.last().minAgeSeconds = kTempMinAgeSeconds;

    add(CleanupCategoryId::WindowsUpdateCache, "Bộ nhớ đệm Windows Update",
       "Các gói cập nhật đã tải về và cài đặt xong, Windows Update sẽ tải lại nếu cần.", CleanupRisk::Safe,
       {under(windowsDir, "/SoftwareDistribution/Download")});

    add(CleanupCategoryId::DeliveryOptimization, "Bộ nhớ đệm Delivery Optimization",
       "Bản sao cục bộ của các gói cập nhật dùng để chia sẻ qua mạng LAN/Internet (Windows Update).",
       CleanupRisk::Safe, {under(windowsDir, "/SoftwareDistribution/DeliveryOptimization")});

    add(CleanupCategoryId::ThumbnailCache, "Bộ nhớ đệm hình thu nhỏ",
       "Ảnh thu nhỏ Windows Explorer lưu lại để hiển thị nhanh - sẽ tự tạo lại khi cần.", CleanupRisk::Safe,
       {under(localAppData, "/Microsoft/Windows/Explorer")}, {"thumbcache_*.db", "iconcache_*.db"}, false, true);

    add(CleanupCategoryId::WindowsErrorReports, "Báo cáo lỗi Windows (WER)",
       "Báo cáo lỗi và tệp kết xuất (dump) Windows thu thập khi ứng dụng/hệ thống gặp sự cố.",
       CleanupRisk::Safe, {under(localAppData, "/Microsoft/Windows/WER/ReportArchive"),
                          under(localAppData, "/Microsoft/Windows/WER/ReportQueue")});

    add(CleanupCategoryId::MemoryDumps, "Tệp kết xuất bộ nhớ (memory dump)",
       "Tệp dump khi Windows gặp lỗi nghiêm trọng (màn hình xanh) - chỉ hữu ích khi cần chẩn đoán lỗi đó.",
       CleanupRisk::Safe, {under(windowsDir, "/Minidump")}, {"*.dmp"});

    add(CleanupCategoryId::Prefetch, "Dữ liệu Prefetch",
       "Windows dùng để tăng tốc khởi động ứng dụng - xóa an toàn nhưng lần mở đầu tiên sau đó có thể chậm hơn một chút.",
       CleanupRisk::Caution, {under(windowsDir, "/Prefetch")}, {"*.pf"});

    add(CleanupCategoryId::RecentItems, "Danh sách tệp gần đây",
       "Danh sách lối tắt tới các tệp/thư mục đã mở gần đây (hiện trong menu Start, thanh địa chỉ).",
       CleanupRisk::Caution, {under(roamingAppData, "/Microsoft/Windows/Recent")});
    // Hai thư mục con này giữ các mục người dùng tự GHIM (jump list trên thanh tác vụ, Quick Access) -
    // không phải danh sách "gần đây" Windows tự dựng lại được, xóa đi là mất hẳn các mục ghim.
    result.last().excludeNames = {"AutomaticDestinations", "CustomDestinations"};

    add(CleanupCategoryId::SystemLogs, "Tệp nhật ký hệ thống (.log)",
       "Tệp nhật ký văn bản do Windows và trình cài đặt tạo ra.", CleanupRisk::Caution,
       {under(windowsDir, "/Logs"), under(windowsDir, "/Temp")}, {"*.log"});

    add(CleanupCategoryId::BrowserCacheChrome, "Bộ nhớ đệm Google Chrome",
       "Tệp cache trình duyệt Chrome - trang web sẽ tải lại từ đầu lần truy cập sau.", CleanupRisk::Safe,
       {under(localAppData, "/Google/Chrome/User Data/Default/Cache"),
        under(localAppData, "/Google/Chrome/User Data/Default/Code Cache"),
        under(localAppData, "/Google/Chrome/User Data/Default/GPUCache")});

    add(CleanupCategoryId::BrowserCacheEdge, "Bộ nhớ đệm Microsoft Edge",
       "Tệp cache trình duyệt Edge - trang web sẽ tải lại từ đầu lần truy cập sau.", CleanupRisk::Safe,
       {under(localAppData, "/Microsoft/Edge/User Data/Default/Cache"),
        under(localAppData, "/Microsoft/Edge/User Data/Default/Code Cache"),
        under(localAppData, "/Microsoft/Edge/User Data/Default/GPUCache")});

    add(CleanupCategoryId::BrowserCacheFirefox, "Bộ nhớ đệm Firefox",
       "Tệp cache trình duyệt Firefox - trang web sẽ tải lại từ đầu lần truy cập sau.", CleanupRisk::Safe,
       firefoxCacheDirs(roamingAppData));

    add(CleanupCategoryId::WindowsOld, "Windows.old (bản Windows cũ)",
       "Toàn bộ hệ điều hành Windows phiên bản trước, Windows giữ lại ~10 ngày sau khi nâng cấp để có thể "
       "khôi phục. XÓA KHÔNG THỂ HOÀN TÁC - chỉ xóa khi chắc chắn không cần quay lại phiên bản cũ.",
       CleanupRisk::High, {isDriveAbsolute(windowsDir) && windowsDir.at(1) == QLatin1Char(':')
                               ? windowsDir.left(3) + "Windows.old"
                               : QString()},
       {}, true, false);

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
        case CleanupCategoryId::RecentItems:            return "RecentItems";
        case CleanupCategoryId::SystemLogs:            return "SystemLogs";
        case CleanupCategoryId::BrowserCacheChrome:    return "BrowserCacheChrome";
        case CleanupCategoryId::BrowserCacheEdge:      return "BrowserCacheEdge";
        case CleanupCategoryId::BrowserCacheFirefox:   return "BrowserCacheFirefox";
        case CleanupCategoryId::WindowsOld:            return "WindowsOld";
    }
    return "Unknown";
}
