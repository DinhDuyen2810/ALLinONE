#pragma once

#include <QString>

/// Kiểu bảo mật mạng WiFi, dùng cho cả mạng quét được lẫn hồ sơ đã lưu.
enum class WifiSecurity
{
    Open,       // Không mật khẩu
    Wep,        // WEP (cũ, yếu)
    WpaPsk,     // WPA-Personal (TKIP)
    Wpa2Psk,    // WPA2-Personal (AES) - phổ biến nhất
    Wpa3Sae,    // WPA3-Personal (SAE)
    Enterprise, // WPA/WPA2/WPA3-Enterprise (802.1X, cần chứng chỉ/RADIUS) - không hỗ trợ kết nối bằng mật khẩu đơn
    Unknown
};

namespace WifiSecurityUtil
{
inline QString displayName(WifiSecurity s)
{
    switch (s)
    {
        case WifiSecurity::Open:       return "Mở (không mật khẩu)";
        case WifiSecurity::Wep:        return "WEP";
        case WifiSecurity::WpaPsk:     return "WPA-Personal";
        case WifiSecurity::Wpa2Psk:    return "WPA2-Personal";
        case WifiSecurity::Wpa3Sae:    return "WPA3-Personal";
        case WifiSecurity::Enterprise: return "Enterprise (802.1X)";
        case WifiSecurity::Unknown:    return "Không xác định";
    }
    return "Không xác định";
}

/// Mạng bảo mật Enterprise cần chứng chỉ/RADIUS, không thể kết nối chỉ bằng SSID+mật khẩu.
inline bool requiresPassword(WifiSecurity s)
{
    return s == WifiSecurity::Wep || s == WifiSecurity::WpaPsk || s == WifiSecurity::Wpa2Psk || s == WifiSecurity::Wpa3Sae;
}

inline bool isSupportedForQuickConnect(WifiSecurity s)
{
    return s != WifiSecurity::Enterprise && s != WifiSecurity::Unknown;
}
} // namespace WifiSecurityUtil
