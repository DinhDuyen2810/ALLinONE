#pragma once

#include <QString>

#include "../direct/DirectVpnTypes.h"

/// Hồ sơ VPN kết nối TRỰC TIẾP do NGƯỜI DÙNG tự thêm trong ứng dụng (chế độ "GlobalProtect" hoặc "FortiClient" của hộp
/// thoại "Thêm hồ sơ VPN") - khác hồ sơ do phần mềm VPN bên thứ ba cấu hình sẵn (ThirdPartyVpnProfile, chỉ đọc) và khác
/// hồ sơ VPN hệ thống của Windows (VpnProfile). Chỉ lưu tên + địa chỉ máy chủ + giao thức; tài khoản/mật khẩu/PSK KHÔNG
/// nằm ở đây (hỏi lúc kết nối, có thể lưu riêng vào Windows Credential Manager - xem direct/CredentialStore.h).
struct DirectVpnProfile
{
    QString name;      ///< tên hiển thị do người dùng đặt (duy nhất theo cặp giao thức + tên)
    QString server;    ///< host[:port] - GlobalProtect: portal; FortiClient SSL-VPN: máy chủ[:cổng]; FortiClient IPsec: máy chủ
    DirectProtocol protocol{DirectProtocol::None}; ///< GlobalProtect | FortinetSsl | FortinetIpsec

    bool operator==(const DirectVpnProfile& o) const { return name == o.name && server == o.server && protocol == o.protocol; }
};
