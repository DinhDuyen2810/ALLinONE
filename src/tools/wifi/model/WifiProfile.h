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
    QString password;

    QString rawXml;            // XML gốc, dùng để xuất file
};
