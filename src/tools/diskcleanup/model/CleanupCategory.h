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
};

/// Mức độ rủi ro khi xóa - quyết định có mặc định tick sẵn trong UI hay không.
enum class CleanupRisk
{
    Safe,    // an toàn tuyệt đối, mặc định tick sẵn (cache/temp thường trực, hệ thống tự tạo lại)
    Caution, // an toàn nhưng có tác dụng phụ nhỏ (vd: Prefetch làm chậm lần mở ứng dụng đầu tiên)
    High,    // rủi ro cao / không thể hoàn tác dễ dàng (vd: Windows.old) - không bao giờ tick sẵn
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

    QString displayName() const { return name; }
};
