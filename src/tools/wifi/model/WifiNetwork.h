#pragma once

#include <QByteArray>
#include <QString>

#include "WifiSecurity.h"

/// Một mạng WiFi tìm thấy khi quét xung quanh.
struct WifiNetwork
{
    QString ssid;           // Tên hiển thị (giải mã UTF-8/Latin1 tốt nhất có thể)
    QByteArray ssidBytes;   // Byte gốc của SSID (để dựng hồ sơ kết nối chính xác, kể cả SSID không phải UTF-8)
    QString bssid;          // MAC điểm phát (dạng AA:BB:CC:DD:EE:FF), rỗng nếu gộp nhiều BSSID
    int signalQuality{0};   // 0-100 theo WLAN API
    WifiSecurity security{WifiSecurity::Unknown};
    bool connected{false};
    bool hasProfile{false}; // Đã có hồ sơ lưu sẵn trên máy
    QString profileName;    // Tên hồ sơ nếu có (thường trùng SSID)
    int numberOfBssids{0};
};
