#pragma once

#include <QString>

#include "../direct/DirectVpnTypes.h"

/// Một hồ sơ/kết nối VPN ĐÃ ĐƯỢC CẤU HÌNH SẴN trên máy bởi một phần mềm VPN BÊN THỨ BA (FortiClient,
/// GlobalProtect, OpenVPN GUI, Cisco Secure Client/AnyConnect). Ứng dụng chỉ ĐỌC các thông tin KHÔNG bí
/// mật (tên hồ sơ, địa chỉ máy chủ/cổng) từ registry/tệp cấu hình của chính phần mềm đó trên máy người
/// dùng - KHÔNG giải mã/đọc mật khẩu đã lưu. Với FortiClient SSL-VPN, FortiClient IPsec (IKEv1) và GlobalProtect, ứng dụng còn KẾT NỐI
/// TRỰC TIẾP được (client giao thức tự viết, người dùng nhập tài khoản trong ứng dụng - xem direct/); các loại
/// khác chỉ "Mở phần mềm" đó để kết nối.
struct ThirdPartyVpnProfile
{
    QString clientName;  // "FortiClient" | "GlobalProtect" | "OpenVPN" | "Cisco Secure Client"
    QString name;        // tên hồ sơ/tunnel/portal do người dùng/quản trị đặt
    QString server;      // địa chỉ máy chủ (host hoặc host:port / portal URL)
    QString source;      // nơi đọc được (khóa registry / đường dẫn tệp) - để người dùng tra cứu
    QString exePath;     // đường dẫn tới chương trình client để MỞ (rỗng = không tìm thấy chương trình)
    QString launchArg;   // tham số khi mở (vd OpenVPN: "--connect <tên>.ovpn"); rỗng = chỉ mở client
    /// Giao thức ứng dụng tự kết nối được (None = chỉ "Mở phần mềm": FortiClient IPsec, OpenVPN, Cisco).
    DirectProtocol direct{DirectProtocol::None};

    bool canLaunch() const { return !exePath.isEmpty(); }
    bool canConnectDirectly() const { return direct != DirectProtocol::None && !server.trimmed().isEmpty(); }
};
