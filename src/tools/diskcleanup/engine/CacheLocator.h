#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

/**
 * @brief Tìm thư mục BỘ NHỚ ĐỆM theo cấu trúc thật trên đĩa (v1.32.0, tham khảo các bản roomy: krezio
 * CacheFinder.cs, WiseeCarrot scan.cpp) - không đoán theo tên chung chung "cache" (có thể trùng dữ liệu thật
 * của một ứng dụng), mà theo BỐ CỤC đặc trưng của Chromium/Electron. Chỉ đọc.
 */
namespace CacheLocator
{
/// Tên thư mục bộ nhớ đệm trong MỘT hồ sơ Chromium (Default, Profile 1...) hoặc thư mục dữ liệu Electron.
QStringList chromiumProfileCacheNames();
/// Tên thư mục bộ nhớ đệm đồ họa nằm ngay trong "User Data" (dùng chung mọi hồ sơ).
QStringList chromiumUserDataCacheNames();

/// Thư mục con của "User Data" là một HỒ SƠ thật: Default / Profile N / Guest Profile, hoặc có tệp Preferences.
bool isChromiumProfileDir(const QString& dir);

/// Mọi thư mục bộ nhớ đệm ĐANG TỒN TẠI của một trình duyệt Chromium: <userData>/{ShaderCache,GrShaderCache,...}
/// + <mỗi hồ sơ>/{Cache,Code Cache,GPUCache,Dawn*}. Trả về rỗng nếu userData không tồn tại.
QStringList chromiumBrowserCaches(const QString& userData);

/// Thư mục bộ nhớ đệm ĐANG TỒN TẠI ngay trong một thư mục dữ liệu Electron/CEF (vd %APPDATA%/discord).
/// extraNames: thêm tên riêng của ứng dụng (vd VS Code: CachedData, CachedExtensionVSIXs).
QStringList electronAppCaches(const QString& appDataDir, const QStringList& extraNames = {});

/// "Cache"/"Code Cache"/"GPUCache" nằm sâu tới maxDepth cấp bên dưới root mà CÓ bố cục Chromium (cạnh nhau, hoặc
/// "Cache" chứa "Cache_Data") - cho trình khởi chạy game nhúng Chromium (Battle.net, EA app...). Không lấy
/// thư mục nằm trong một thư mục đã lấy.
QStringList chromiumCachesUnder(const QString& root, int maxDepth = 6);

/// Một ứng dụng Electron/CEF khác (không có hạng mục riêng) tìm thấy trong AppData.
struct ChromiumApp
{
    QString name; // tên thư mục (vd "Notion", "ZaloPC")
    QString dir;
    QStringList caches; // các thư mục bộ nhớ đệm đang tồn tại
};
/// Thư mục con (1-2 cấp) của 'parents' có tệp "Local State" hoặc "Preferences" (dấu hiệu thư mục dữ liệu
/// Chromium/Electron) và có ít nhất một thư mục bộ nhớ đệm. Bỏ qua tên trong skipNames (không phân biệt hoa
/// thường) - các trình duyệt/ứng dụng đã có hạng mục riêng, thư mục của Windows.
QList<ChromiumApp> otherChromiumApps(const QStringList& parents, const QSet<QString>& skipNamesLower);

/// Các đường dẫn thư viện Steam trong nội dung libraryfolders.vdf (khóa "path"), đã bỏ thoát "\\".
QStringList parseSteamLibraryFolders(const QString& vdfText);
/// Thư viện Steam của máy: steamRoot + các thư viện khai trong steamapps/libraryfolders.vdf (đang tồn tại).
QStringList steamLibraries(const QString& steamRoot);
} // namespace CacheLocator
