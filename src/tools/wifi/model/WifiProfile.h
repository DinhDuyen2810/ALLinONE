#pragma once

#include <QString>

#include "WifiSecurity.h"

/// Hồ sơ (profile) WiFi đã lưu trên máy (Windows WLAN profile).
struct WifiProfile
{
    QString name;              // Tên hồ sơ (thường trùng SSID)
    QString ssid;
    WifiSecurity security{WifiSecurity::Unknown};
    bool autoConnect{false};   // connectionMode = auto (ngược lại là manual)

    bool hasPassword{false};       // Đã đọc được mật khẩu dạng chữ (plaintext)
    bool passwordAccessDenied{false}; // Hệ thống từ chối cấp mật khẩu (cần quyền Admin)
    bool keyProtected{false};      // XML có <keyMaterial> nhưng ở dạng MÃ HÓA (<protected>true) - không phải
                                   // mật khẩu đọc được; cũng là dấu hiệu "không được cấp mật khẩu dạng chữ"
    QString password;

    QString rawXml;            // XML gốc, dùng để xuất file
};
