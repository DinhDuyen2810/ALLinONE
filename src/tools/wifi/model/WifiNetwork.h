#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <algorithm>

#include "WifiSecurity.h"

/// Một mạng WiFi tìm thấy khi quét xung quanh.
struct WifiNetwork
{
    QString ssid;           // SSID (giải mã UTF-8/Latin1 tốt nhất có thể); RỖNG nếu mạng ẩn - KHÔNG chứa
                            // chuỗi trang trí kiểu "(Mạng ẩn)", dùng displayName() để hiển thị
    QByteArray ssidBytes;   // Byte gốc của SSID (để dựng hồ sơ kết nối chính xác, kể cả SSID không phải UTF-8)
    QString bssid;          // MAC điểm phát (dạng AA:BB:CC:DD:EE:FF), rỗng nếu gộp nhiều BSSID
    int signalQuality{0};   // 0-100 theo WLAN API
    WifiSecurity security{WifiSecurity::Unknown};
    bool connected{false};
    bool hasProfile{false}; // Đã có hồ sơ lưu sẵn trên máy
    QString profileName;    // Tên hồ sơ nếu có (thường trùng SSID)
    int numberOfBssids{0};
    bool hidden{false};     // Mạng không quảng bá SSID (quét thấy SSID rỗng, hoặc người dùng tự nhập SSID)

    /// Tên để HIỂN THỊ trong bảng/thông báo. Tách khỏi ssid: trước đây chuỗi "(Mạng ẩn)" được gán thẳng
    /// vào ssid rồi bị dùng làm SSID/tên hồ sơ thật khi bấm Kết nối.
    QString displayName() const
    {
        if (!ssid.isEmpty())
            return ssid;
        return profileName.isEmpty() ? QStringLiteral("(Mạng ẩn)")
                                     : QStringLiteral("(Mạng ẩn - hồ sơ: %1)").arg(profileName);
    }
};

namespace WifiNetworkUtil
{
/// Gộp các mục cùng một mạng (WLAN API trả nhiều mục cho cùng SSID: mục gắn hồ sơ, mục không gắn hồ sơ,
/// nhiều BSSID): lấy tín hiệu mạnh nhất, giữ cờ đã kết nối/có hồ sơ và - quan trọng - TÊN HỒ SƠ. Trước đây
/// mục gộp chỉ giữ profileName của mục gặp đầu tiên: nếu đó là mục không gắn hồ sơ thì hasProfile=true
/// nhưng profileName rỗng, nút "Quên mạng này" sáng mà bấm không làm gì.
inline QList<WifiNetwork> mergeDuplicates(const QList<WifiNetwork>& networks)
{
    QList<WifiNetwork> merged;
    for (const WifiNetwork& w : networks)
    {
        auto it = std::find_if(merged.begin(), merged.end(), [&w](const WifiNetwork& m) {
            if (m.ssid != w.ssid || m.ssidBytes != w.ssidBytes || m.security != w.security)
                return false;
            // Các mạng ẩn đều có SSID rỗng - chỉ gộp khi không mâu thuẫn về hồ sơ.
            return !w.ssid.isEmpty() || m.profileName == w.profileName;
        });
        if (it == merged.end())
        {
            merged.push_back(w);
            continue;
        }
        it->signalQuality = std::max(it->signalQuality, w.signalQuality);
        it->connected = it->connected || w.connected;
        it->hasProfile = it->hasProfile || w.hasProfile;
        it->numberOfBssids += w.numberOfBssids;
        if (it->profileName.isEmpty())
            it->profileName = w.profileName;
    }
    return merged;
}
} // namespace WifiNetworkUtil
