#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include "../model/ThirdPartyVpnProfile.h"

/// Phát hiện các hồ sơ VPN đã cấu hình sẵn bởi phần mềm VPN bên thứ ba trên máy (FortiClient,
/// GlobalProtect, OpenVPN GUI, Cisco Secure Client/AnyConnect) và cho phép MỞ chính phần mềm đó để kết nối.
/// CHỈ ĐỌC thông tin không bí mật (tên + địa chỉ máy chủ) từ registry/tệp cấu hình của chính phần mềm đó -
/// KHÔNG đụng tới mật khẩu đã lưu. Đánh dấu hồ sơ nào ứng dụng tự kết nối trực tiếp được
/// (ThirdPartyVpnProfile::direct - việc kết nối nằm ở direct/, không ở đây). Xem model/ThirdPartyVpnProfile.h.
namespace ThirdPartyVpnScanner
{
/// Quét toàn bộ (registry + thư mục cấu hình) - trả về mọi hồ sơ tìm được (có thể rỗng nếu máy không cài
/// phần mềm VPN bên thứ ba nào). Chỉ đọc, an toàn, không cần quyền Administrator cho phần lớn vị trí.
QList<ThirdPartyVpnProfile> scan();

/// Mở phần mềm client tương ứng để người dùng kết nối (ShellExecute - cùng cơ chế mở ứng dụng của Windows).
/// Trả về false + *error nếu không tìm thấy/không mở được chương trình.
bool launch(const ThirdPartyVpnProfile& profile, QString* error = nullptr);

// ---- Lõi THUẦN (không registry/tệp/tiến trình) - tách để kiểm thử bằng dữ liệu mẫu ----
namespace internal
{
/// Tách "host:port" (hoặc chỉ "host") của FortiClient thành chuỗi hiển thị gọn; trả nguyên trạng nếu không
/// phân tích được. (Chỉ chuẩn hóa hiển thị - không dùng để kết nối.)
QString normalizeServer(const QString& raw);

/// Đọc dòng `remote <host> <port>` ĐẦU TIÊN trong nội dung tệp .ovpn -> "host:port" (rỗng nếu không có).
QString parseOvpnRemote(const QByteArray& ovpn);

/// Phân tích tệp hồ sơ XML của Cisco (AnyConnect/Secure Client) -> danh sách (HostName, HostAddress) trong
/// <ServerList><HostEntry>. Thuần XML (QXmlStreamReader).
QList<QPair<QString, QString>> parseCiscoProfileXml(const QByteArray& xml);

/// Đường dẫn ĐẦU TIÊN tồn tại trong danh sách ứng viên (rỗng nếu không cái nào tồn tại).
QString firstExistingPath(const QStringList& candidates);
} // namespace internal
} // namespace ThirdPartyVpnScanner
