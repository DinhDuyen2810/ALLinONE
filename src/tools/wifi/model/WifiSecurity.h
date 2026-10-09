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

/// Ánh xạ cặp DOT11_AUTH_ALGORITHM / DOT11_CIPHER_ALGORITHM mà WLAN API trả về sang WifiSecurity.
/// Nhận giá trị SỐ (không phụ thuộc header Win32 - header MinGW đang dùng chỉ định nghĩa tới RSNA_PSK=7)
/// theo bảng DOT11_AUTH_ALGORITHM của Microsoft:
///   1 OPEN, 2 SHARED_KEY, 3 WPA, 4 WPA_PSK, 5 WPA_NONE, 6 RSNA, 7 RSNA_PSK,
///   8 WPA3 (= WPA3_ENT_192), 9 WPA3_SAE, 10 OWE, 11 WPA3_ENT, >= 0x80000000 = riêng của hãng (IHV).
/// Trước đây MỌI giá trị > 7 đều bị coi là WPA3-Personal: mạng WPA3-Enterprise (8, 11) và OWE (10) bị dán
/// nhãn sai, hộp thoại kết nối mời nhập "mật khẩu WPA3" cho mạng không hề dùng mật khẩu chung.
inline WifiSecurity fromDot11(unsigned authAlgo, unsigned cipherAlgo, bool securityEnabled)
{
    if (!securityEnabled)
        return WifiSecurity::Open;

    switch (authAlgo)
    {
        case 1:  return cipherAlgo == 0 ? WifiSecurity::Open : WifiSecurity::Wep; // cipher 0 = NONE
        case 2:  return WifiSecurity::Wep;
        case 3:  // WPA-Enterprise
        case 6:  // WPA2-Enterprise
        case 8:  // WPA3-Enterprise 192-bit
        case 11: // WPA3-Enterprise
            return WifiSecurity::Enterprise;
        case 4:  return WifiSecurity::WpaPsk;
        case 7:  return WifiSecurity::Wpa2Psk;
        case 9:  return WifiSecurity::Wpa3Sae;
        default: return WifiSecurity::Unknown; // 5 (WPA_NONE), 10 (OWE), IHV, giá trị tương lai
    }
}
} // namespace WifiSecurityUtil
