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
///
/// An toàn khi ghi (hosts file hỏng = cả máy phân giải tên miền sai):
///  - Trước lần ghi ĐẦU TIÊN, chép nguyên bản hosts hiện có ra `hosts.oneforall.bak` cạnh nó (chỉ một
///    lần - không ghi đè bản sao lưu đã có, để luôn giữ được trạng thái TRƯỚC khi công cụ này đụng vào).
///  - Ghi nguyên tử qua QSaveFile (ghi ra tệp tạm rồi đổi tên đè lên) - mất điện/đầy đĩa giữa chừng không
///    để lại hosts cụt; kết quả ghi/commit được kiểm tra và trả lỗi thật.
///  - Giữ nguyên từng byte các dòng không thuộc khối của mình, kể cả khi tệp không phải UTF-8. Kiểu xuống
///    dòng: tệp thuần "\r\n" hoặc thuần "\n" được ghi lại đúng kiểu đó; tệp LẪN LỘN hai kiểu được đưa cả
///    về "\r\n". Các dòng trắng ở cuối tệp được gom lại còn một.
namespace HostsBlocklist
{

/// Đọc danh sách tên miền hiện đang bị chặn (chỉ trong khối do OneForAll quản lý).
QStringList listBlockedDomains(QString* error = nullptr);

/// Thêm một tên miền vào danh sách chặn. Nội dung nhập được chuẩn hóa trước (xem normalizeDomain) -
/// dán cả URL "https://vi-du.com/trang" vẫn ra đúng "vi-du.com"; nội dung không phải tên miền bị từ chối
/// kèm lý do trong *error. Tự bỏ qua nếu đã có.
bool addDomain(const QString& domain, QString* error = nullptr);

/// Gỡ một tên miền khỏi danh sách chặn.
bool removeDomain(const QString& domain, QString* error = nullptr);

namespace internal
{
inline const QString kMarkerStart = "# OneForAll-SecurityGateway-BLOCKLIST-START (dung cong cu de them/bot, khong tu sua tay)";
inline const QString kMarkerEnd = "# OneForAll-SecurityGateway-BLOCKLIST-END";

/// Chuẩn hóa + kiểm tra một tên miền người dùng nhập: bỏ scheme ("https://"), thông tin đăng nhập, cổng,
/// đường dẫn/query, dấu chấm cuối; đổi chữ thường; tên miền có dấu (IDN) đổi sang dạng punycode "xn--".
/// Trả chuỗi RỖNG (kèm lý do trong *error) nếu có khoảng trắng (nhiều tên miền trên một dòng), ký tự
/// không hợp lệ, là địa chỉ IP, hoặc là "localhost" (chặn localhost làm hỏng nhiều phần mềm trên máy).
/// Thuần chuỗi, test được.
QString normalizeDomain(const QString& input, QString* error = nullptr);

/// Phân tích danh sách tên miền hiện có trong khối được đánh dấu - thuần chuỗi, test được.
QStringList parseManagedDomains(const QString& hostsContent);

/// Dựng lại TOÀN BỘ nội dung hosts file mới: giữ nguyên mọi dòng ngoài khối đánh dấu, thay khối đánh
/// dấu (hoặc thêm vào cuối nếu chưa có) bằng danh sách domains mới. Thuần chuỗi, test được.
///
/// Khối HỎNG (có marker START nhưng thiếu marker END - vd người dùng lỡ tay xóa): KHÔNG coi mọi thứ
/// tới cuối tệp là của mình (bản trước làm vậy và xóa sạch phần còn lại của hosts ở lần ghi kế tiếp) -
/// chỉ nhận các dòng LIỀN NGAY sau START có đúng dạng "0.0.0.0 <tên miền hợp lệ>" do chính công cụ ghi,
/// mọi dòng từ dòng đầu tiên không khớp trở đi được giữ nguyên. Trong một khối lành lặn, dòng không
/// phải "0.0.0.0 ..." (người dùng tự thêm tay) cũng được giữ lại (đưa ra ngoài khối) thay vì bị xóa.
QString buildUpdatedHostsContent(const QString& existingContent, const QStringList& domains);

/// Đường dẫn hosts file thật trên máy (dò theo biến môi trường SystemRoot, mặc định C:\Windows nếu
/// không đọc được).
QString hostsFilePath();

/// `<hostsPath>.oneforall.bak` - bản sao lưu một lần (xem ghi chú đầu tệp).
QString backupFilePath(const QString& hostsPath);

/// Các biến thể nhận ĐƯỜNG DẪN tệp - phần đọc/ghi thật, tách riêng để kiểm thử trên tệp tạm (test tự
/// động không bao giờ được đụng hosts file thật). Hàm công khai ở trên chỉ gọi lại với hostsFilePath().
QStringList listBlockedDomainsInFile(const QString& hostsPath, QString* error = nullptr);
bool addDomainToFile(const QString& hostsPath, const QString& domain, QString* error = nullptr);
bool removeDomainFromFile(const QString& hostsPath, const QString& domain, QString* error = nullptr);
} // namespace internal

} // namespace HostsBlocklist
