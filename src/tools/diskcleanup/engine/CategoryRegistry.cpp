#include "CategoryRegistry.h"

#include <QDir>
#include <QFileInfo>

CleanupEnvironment CleanupEnvironment::current()
{
    CleanupEnvironment env;
    env.tempDir = QDir::fromNativeSeparators(qEnvironmentVariable("TEMP"));
    env.windowsDir = QDir::fromNativeSeparators(qEnvironmentVariable("WINDIR", "C:/Windows"));
    env.localAppData = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA"));
    env.roamingAppData = QDir::fromNativeSeparators(qEnvironmentVariable("APPDATA"));
    return env;
}

namespace
{
/// Chỉ giữ các đường dẫn thực sự tồn tại trên máy - hạng mục không áp dụng (vd: chưa cài trình duyệt
/// đó) sẽ có rootPaths rỗng thay vì một đường dẫn không tồn tại gây lỗi khi quét.
QStringList existingOnly(const QStringList& candidates)
{
    QStringList out;
    for (const QString& p : candidates)
        if (!p.isEmpty() && QFileInfo::exists(p))
            out << p;
    return out;
}

/// Firefox đặt cache trong %APPDATA%/Mozilla/Firefox/Profiles/<ký tự ngẫu nhiên>.<tên>/cache2 - cần
/// liệt kê thư mục con thật để tìm đúng từng profile.
QStringList firefoxCacheDirs(const QString& roamingAppData)
{
    QStringList out;
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

QList<CleanupCategory> CategoryRegistry::buildCategories(const CleanupEnvironment& env)
{
    QList<CleanupCategory> result;

    auto add = [&](CleanupCategoryId id, const QString& name, const QString& desc, CleanupRisk risk,
                   const QStringList& roots, const QStringList& patterns = {}, bool scanSub = true,
                   bool keepRoot = true) {
        CleanupCategory c;
        c.id = id;
        c.name = name;
        c.description = desc;
        c.risk = risk;
        c.rootPaths = existingOnly(roots);
        c.namePatterns = patterns;
        c.scanSubdirectories = scanSub;
        c.keepRootFolder = keepRoot;
        result.push_back(c);
    };

    add(CleanupCategoryId::UserTemp, "Tệp tạm của người dùng",
       "Tệp tạm do Windows và các ứng dụng tạo ra trong quá trình chạy, không còn cần thiết sau khi đóng ứng dụng.",
       CleanupRisk::Safe, {env.tempDir});

    add(CleanupCategoryId::WindowsTemp, "Tệp tạm hệ thống Windows",
       "Tệp tạm do chính Windows tạo ra (cần quyền Administrator để xóa hết).", CleanupRisk::Safe,
       {env.windowsDir + "/Temp"});

    add(CleanupCategoryId::WindowsUpdateCache, "Bộ nhớ đệm Windows Update",
       "Các gói cập nhật đã tải về và cài đặt xong, Windows Update sẽ tải lại nếu cần.", CleanupRisk::Safe,
       {env.windowsDir + "/SoftwareDistribution/Download"});

    add(CleanupCategoryId::DeliveryOptimization, "Bộ nhớ đệm Delivery Optimization",
       "Bản sao cục bộ của các gói cập nhật dùng để chia sẻ qua mạng LAN/Internet (Windows Update).",
       CleanupRisk::Safe, {env.windowsDir + "/SoftwareDistribution/DeliveryOptimization"});

    add(CleanupCategoryId::ThumbnailCache, "Bộ nhớ đệm hình thu nhỏ",
       "Ảnh thu nhỏ Windows Explorer lưu lại để hiển thị nhanh - sẽ tự tạo lại khi cần.", CleanupRisk::Safe,
       {env.localAppData + "/Microsoft/Windows/Explorer"}, {"thumbcache_*.db", "iconcache_*.db"}, false, true);

    add(CleanupCategoryId::WindowsErrorReports, "Báo cáo lỗi Windows (WER)",
       "Báo cáo lỗi và tệp kết xuất (dump) Windows thu thập khi ứng dụng/hệ thống gặp sự cố.",
       CleanupRisk::Safe, {env.localAppData + "/Microsoft/Windows/WER/ReportArchive",
                          env.localAppData + "/Microsoft/Windows/WER/ReportQueue"});

    add(CleanupCategoryId::MemoryDumps, "Tệp kết xuất bộ nhớ (memory dump)",
       "Tệp dump khi Windows gặp lỗi nghiêm trọng (màn hình xanh) - chỉ hữu ích khi cần chẩn đoán lỗi đó.",
       CleanupRisk::Safe, {env.windowsDir + "/Minidump"}, {"*.dmp"});

    add(CleanupCategoryId::Prefetch, "Dữ liệu Prefetch",
       "Windows dùng để tăng tốc khởi động ứng dụng - xóa an toàn nhưng lần mở đầu tiên sau đó có thể chậm hơn một chút.",
       CleanupRisk::Caution, {env.windowsDir + "/Prefetch"}, {"*.pf"});

    add(CleanupCategoryId::RecentItems, "Danh sách tệp gần đây",
       "Danh sách lối tắt tới các tệp/thư mục đã mở gần đây (hiện trong menu Start, thanh địa chỉ).",
       CleanupRisk::Caution, {env.roamingAppData + "/Microsoft/Windows/Recent"});

    add(CleanupCategoryId::SystemLogs, "Tệp nhật ký hệ thống (.log)",
       "Tệp nhật ký văn bản do Windows và trình cài đặt tạo ra.", CleanupRisk::Caution,
       {env.windowsDir + "/Logs", env.windowsDir + "/Temp"}, {"*.log"});

    add(CleanupCategoryId::BrowserCacheChrome, "Bộ nhớ đệm Google Chrome",
       "Tệp cache trình duyệt Chrome - trang web sẽ tải lại từ đầu lần truy cập sau.", CleanupRisk::Safe,
       {env.localAppData + "/Google/Chrome/User Data/Default/Cache",
        env.localAppData + "/Google/Chrome/User Data/Default/Code Cache",
        env.localAppData + "/Google/Chrome/User Data/Default/GPUCache"});

    add(CleanupCategoryId::BrowserCacheEdge, "Bộ nhớ đệm Microsoft Edge",
       "Tệp cache trình duyệt Edge - trang web sẽ tải lại từ đầu lần truy cập sau.", CleanupRisk::Safe,
       {env.localAppData + "/Microsoft/Edge/User Data/Default/Cache",
        env.localAppData + "/Microsoft/Edge/User Data/Default/Code Cache",
        env.localAppData + "/Microsoft/Edge/User Data/Default/GPUCache"});

    add(CleanupCategoryId::BrowserCacheFirefox, "Bộ nhớ đệm Firefox",
       "Tệp cache trình duyệt Firefox - trang web sẽ tải lại từ đầu lần truy cập sau.", CleanupRisk::Safe,
       firefoxCacheDirs(env.roamingAppData));

    add(CleanupCategoryId::WindowsOld, "Windows.old (bản Windows cũ)",
       "Toàn bộ hệ điều hành Windows phiên bản trước, Windows giữ lại ~10 ngày sau khi nâng cấp để có thể "
       "khôi phục. XÓA KHÔNG THỂ HOÀN TÁC - chỉ xóa khi chắc chắn không cần quay lại phiên bản cũ.",
       CleanupRisk::High, {env.windowsDir.left(3) + "Windows.old"}, {}, true, false);

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
