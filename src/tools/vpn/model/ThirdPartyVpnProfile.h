#pragma once

#include <QString>

/// Một hồ sơ/kết nối VPN ĐÃ ĐƯỢC CẤU HÌNH SẴN trên máy bởi một phần mềm VPN BÊN THỨ BA (FortiClient,
/// GlobalProtect, OpenVPN GUI, Cisco Secure Client/AnyConnect). Ứng dụng chỉ ĐỌC các thông tin KHÔNG bí
/// mật (tên hồ sơ, địa chỉ máy chủ/cổng) từ registry/tệp cấu hình của chính phần mềm đó trên máy người
/// dùng, rồi cho phép MỞ chính phần mềm đó để kết nối - KHÔNG giải mã/đọc mật khẩu đã lưu, KHÔNG tự kết nối
/// thẳng (các giao thức SSL-VPN này là độc quyền của nhà cung cấp). Giống cách WiFi Connection đọc hồ sơ
/// WiFi đã lưu của Windows.
struct ThirdPartyVpnProfile
{
    QString clientName;  // "FortiClient" | "GlobalProtect" | "OpenVPN" | "Cisco Secure Client"
    QString name;        // tên hồ sơ/tunnel/portal do người dùng/quản trị đặt
    QString server;      // địa chỉ máy chủ (host hoặc host:port / portal URL) - chỉ để hiển thị
    QString source;      // nơi đọc được (khóa registry / đường dẫn tệp) - để người dùng tra cứu
    QString exePath;     // đường dẫn tới chương trình client để MỞ (rỗng = không tìm thấy chương trình)
    QString launchArg;   // tham số khi mở (vd OpenVPN: "--connect <tên>.ovpn"); rỗng = chỉ mở client

    bool canLaunch() const { return !exePath.isEmpty(); }
};
