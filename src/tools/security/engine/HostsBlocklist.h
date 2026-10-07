#pragma once

#include <QString>
#include <QStringList>

/// Chặn truy cập tới tên miền độc hại bằng cách trỏ chúng về 0.0.0.0 trong hosts file của Windows
/// (C:\Windows\System32\drivers\etc\hosts) - lớp chặn cục bộ, thủ công, hoạt động với MỌI trình duyệt/
/// ứng dụng vì được phân giải ở tầng hệ điều hành trước khi có kết nối mạng nào tới tên miền đó.
/// Bổ sung cho DefenderController::setNetworkProtectionEnabled (chặn theo danh tiếng/thời gian thực của
/// Microsoft) chứ không thay thế - đây là danh sách CỐ ĐỊNH do người dùng tự khai báo.
///
/// Chỉ quản lý MỘT khối được đánh dấu rõ ràng bằng marker trong hosts file - không bao giờ động vào các
/// dòng khác người dùng/chương trình khác đã có sẵn trong tệp. Cần quyền Administrator vì hosts file là
/// tệp hệ thống được bảo vệ (xem WinElevation.h).
namespace HostsBlocklist
{

/// Đọc danh sách tên miền hiện đang bị chặn (chỉ trong khối do OneForAll quản lý).
QStringList listBlockedDomains(QString* error = nullptr);

/// Thêm một tên miền vào danh sách chặn (không phân biệt hoa/thường, tự bỏ qua nếu đã có).
bool addDomain(const QString& domain, QString* error = nullptr);

/// Gỡ một tên miền khỏi danh sách chặn.
bool removeDomain(const QString& domain, QString* error = nullptr);

namespace internal
{
inline const QString kMarkerStart = "# OneForAll-SecurityGateway-BLOCKLIST-START (dung cong cu de them/bot, khong tu sua tay)";
inline const QString kMarkerEnd = "# OneForAll-SecurityGateway-BLOCKLIST-END";

/// Phân tích danh sách tên miền hiện có trong khối được đánh dấu - thuần chuỗi, test được.
QStringList parseManagedDomains(const QString& hostsContent);

/// Dựng lại TOÀN BỘ nội dung hosts file mới: giữ nguyên mọi dòng ngoài khối đánh dấu, thay khối đánh
/// dấu (hoặc thêm vào cuối nếu chưa có) bằng danh sách domains mới. Thuần chuỗi, test được.
QString buildUpdatedHostsContent(const QString& existingContent, const QStringList& domains);

/// Đường dẫn hosts file thật trên máy (dò theo biến môi trường SystemRoot, mặc định C:\Windows nếu
/// không đọc được).
QString hostsFilePath();
} // namespace internal

} // namespace HostsBlocklist
