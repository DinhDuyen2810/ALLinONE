#pragma once

#include <QString>

/// Loại giao thức VPN - đúng các giá trị `TunnelType` mà Windows (Add-VpnConnection) hỗ trợ sẵn,
/// không cần driver/phần mềm thứ ba nào. KHÔNG có WireGuard ở đây - Windows chưa hỗ trợ WireGuard như
/// một loại kết nối VPN hệ thống tích hợp sẵn (cần phần mềm WireGuard riêng, ngoài phạm vi bản này).
enum class VpnTunnelType
{
    Automatic, // để Windows tự thương lượng giao thức phù hợp nhất
    Ikev2,
    L2tp,
    Sstp,
    Pptp,      // cũ, kém an toàn - vẫn liệt kê vì một số VPN cơ quan/cũ chỉ hỗ trợ PPTP
};

/// Một hồ sơ kết nối VPN do NGƯỜI DÙNG tự khai báo - địa chỉ máy chủ, tài khoản từ chính nhà cung cấp
/// VPN họ đã đăng ký (hoặc VPN cơ quan/tự dựng). KHÔNG có máy chủ nào được ứng dụng này tự cung cấp sẵn
/// - xem AndroidControlWindow... (tương tự) - đây chỉ là lớp quản lý/kết nối, không phải dịch vụ VPN.
struct VpnProfile
{
    QString name;          // tên hiển thị, vd "Hà Lan - Mullvad" - cũng là tên kết nối VPN trong Windows
    QString countryLabel;  // nhãn quốc gia tự đặt, vd "Hà Lan" - chỉ để hiển thị/sắp xếp, không ảnh hưởng kết nối
    QString serverAddress; // địa chỉ máy chủ VPN, vd "nl123.mullvad.net" hoặc IP
    VpnTunnelType tunnelType{VpnTunnelType::Ikev2};
    QString username;      // tên đăng nhập - mật khẩu KHÔNG lưu ở đây, xem VpnController.h
};
