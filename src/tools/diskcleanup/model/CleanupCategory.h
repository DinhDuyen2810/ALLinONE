#pragma once

#include <QString>
#include <QStringList>

enum class CleanupCategoryId
{
    UserTemp,
    WindowsTemp,
    RecycleBin,
    WindowsUpdateCache,
    DeliveryOptimization,
    ThumbnailCache,
    WindowsErrorReports,
    MemoryDumps,
    Prefetch,
    RecentItems,
    SystemLogs,
    BrowserCacheChrome,
    BrowserCacheEdge,
    BrowserCacheFirefox,
    WindowsOld,
    // ---- v1.32.0 (tham khảo roomy - xem PROJECT_OVERVIEW.md mục 4zzk) ----
    AppCrashDumps,
    BrowserCacheBrave,
    BrowserCacheVivaldi,
    BrowserCacheOpera,
    BrowserCacheChromium,
    AppCacheDiscord,
    AppCacheSlack,
    AppCacheTeams,
    AppCacheSpotify,
    AppCacheVsCode,
    AppCacheSteam,
    AppCacheEpic,
    AppCacheGameLaunchers,
    AppCacheOtherChromium,
    ShaderCacheDirectX,
    ShaderCacheGpuVendor,
    ShaderCacheSteamGames,
    DevCacheNode,
    DevCachePython,
    DevCacheNuGet,
    DevCacheGo,
    DevCacheGradle,
    DevCacheCargo,
    OldInstallers,
    BigOldDownloads,
    AppDataLeftovers,
    Hibernation,
    SystemErrorReports,
};

/// Mức độ rủi ro khi xóa - quyết định có mặc định tick sẵn trong UI hay không.
enum class CleanupRisk
{
    Safe,    // an toàn tuyệt đối, mặc định tick sẵn (cache/temp thường trực, hệ thống tự tạo lại)
    Caution, // an toàn nhưng có tác dụng phụ nhỏ (vd: Prefetch làm chậm lần mở ứng dụng đầu tiên)
    High,    // rủi ro cao / không thể hoàn tác dễ dàng (vd: Windows.old) - không bao giờ tick sẵn
};

/// Cách TÌM các mục của một hạng mục.
enum class CleanupKind
{
    /// Mặc định: mỗi mục cấp 1 bên trong rootPaths là một mục (keepRootFolder), hoặc cả rootPath là một mục.
    /// Một rootPath là TỆP (vd %WINDIR%\MEMORY.DMP) thì chính tệp đó là một mục.
    FolderContents,
    /// Thùng rác: đo bằng SHQueryRecycleBinW, dọn bằng SHEmptyRecycleBinW (không liệt kê từng tệp).
    RecycleBin,
    /// Bộ cài (.exe/.msi/.msix/.appx/.iso...) nằm NGAY trong Downloads, tải về hơn 7 ngày.
    OldInstallers,
    /// Tệp >= 250 MB trong Downloads (cả thư mục con), không đụng tới hơn 30 ngày.
    BigOldDownloads,
    /// Thư mục AppData của ứng dụng đã gỡ (LeftoverFinder).
    AppDataLeftovers,
    /// Tệp ngủ đông hiberfil.sys - chỉ Windows (powercfg /hibernate off) gỡ được.
    Hibernation,
};

/// Một hạng mục dọn dẹp: tên hiển thị + danh sách thư mục gốc đã được phân giải (đường dẫn thật trên
/// máy này) để quét bên trong. CategoryRegistry là nơi tạo ra danh sách này.
struct CleanupCategory
{
    CleanupCategoryId id;
    QString name;
    QString description;
    CleanupRisk risk{CleanupRisk::Safe};

    QStringList rootPaths;     // thư mục gốc để quét (có thể rỗng nếu hạng mục không khả dụng trên máy này)
    QStringList namePatterns;  // lọc theo tên file, ví dụ {"*.log"} - rỗng nghĩa là lấy mọi thứ trong rootPaths
    bool scanSubdirectories{true};
    bool keepRootFolder{true}; // true: xóa NỘI DUNG bên trong rootPaths (giữ lại thư mục gốc); false: xóa cả thư mục gốc

    /// Tên mục cấp 1 KHÔNG BAO GIỜ được liệt kê/xóa (so không phân biệt hoa thường) - vd thư mục
    /// AutomaticDestinations/CustomDestinations trong Recent chứa các mục người dùng đã GHIM (jump list,
    /// Quick Access), không phải "danh sách gần đây" tự sinh lại được.
    QStringList excludeNames;
    /// Bỏ qua mục vừa được sửa trong N giây gần đây (0 = không lọc) - tệp tạm của một trình cài đặt/ứng
    /// dụng ĐANG chạy thường vừa được ghi xong, xóa đi có thể làm hỏng thao tác đang dở của chương trình đó.
    int minAgeSeconds{0};

    // ---- v1.32.0 ----
    /// Nhóm hiển thị trong cây hạng mục ("Windows", "Trình duyệt", ...).
    QString group;
    CleanupKind kind{CleanupKind::FolderContents};
    /// Dữ liệu TỰ TẠO LẠI (bộ nhớ đệm, tệp tạm, nhật ký, dump): xóa HẲN từng tệp (SafePurge) - đưa bộ nhớ đệm
    /// vào Thùng rác KHÔNG giải phóng byte nào (Thùng rác nằm trên chính ổ đó). false = dữ liệu của người
    /// dùng (bộ cài cũ, tệp tải về, Windows.old, thư mục mồ côi): theo ô "Xóa vĩnh viễn", mặc định Thùng rác.
    bool regenerable{true};
    /// Cần quyền Administrator để xóa (thư mục của Windows) - chưa nâng quyền thì không tick sẵn + ghi chú.
    bool needsAdmin{false};
    /// Tên tệp chạy (chữ thường, vd "chrome.exe") của ứng dụng sở hữu bộ nhớ đệm - đang chạy thì cảnh báo:
    /// tệp ứng dụng đang mở sẽ được bỏ qua, nên đóng ứng dụng để dọn hết.
    QStringList processNames;
    /// Tên ngắn của ứng dụng dùng trong cảnh báo "đang chạy" (rỗng = dùng name).
    QString appLabel;
    /// Hiện TỪNG MỤC để người dùng tự tích chọn (hạng mục "xem kỹ": bộ cài cũ, tệp tải về, thư mục mồ côi).
    bool showItems{false};
    /// Thư mục để TÌM (không phải thư mục gốc dọn dẹp) cho các kind đặc biệt: Downloads, AppData...
    QStringList searchRoots;
    /// Ghi chú hiển thị cố định (vd "Tìm thấy: Zalo, Notion").
    QString note;

    QString displayName() const { return name; }
};
