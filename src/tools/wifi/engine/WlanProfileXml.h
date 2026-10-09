#pragma once

#include <QByteArray>
#include <QString>

#include "../model/WifiProfile.h"
#include "../model/WifiSecurity.h"

/**
 * @brief Dựng và phân tích XML hồ sơ WLAN (WLAN_profile schema của Windows).
 * Không phụ thuộc Win32/WLAN API nên có thể kiểm thử độc lập trên mọi máy.
 * Tham khảo: Microsoft WLAN_profile schema
 * https://learn.microsoft.com/windows/win32/nativewifi/wlan-profileschema-elements
 */
namespace WlanProfileXml
{
/// Tạo XML hồ sơ để kết nối SSID bằng mật khẩu (hoặc mở nếu security = Open).
/// ssidBytes: byte gốc của SSID (tối đa 32 byte theo chuẩn 802.11).
/// nonBroadcast=true cho mạng ẩn (không quảng bá SSID): ghi <nonBroadcast>true</nonBroadcast> để Windows
/// chủ động dò (probe) đúng SSID đó - thiếu phần tử này Windows chỉ nối khi THẤY SSID trong kết quả quét,
/// tức không bao giờ nối được mạng ẩn.
QString build(const QByteArray& ssidBytes, const QString& ssidDisplayName, WifiSecurity security,
             const QString& password, bool autoConnect, bool nonBroadcast = false);

/// Phân tích XML hồ sơ đã lưu thành WifiProfile: name/ssid/security/autoConnect/rawXml; password chỉ được
/// điền khi khóa ở dạng chữ (<protected>false</protected>), ngược lại keyProtected=true.
/// Trả về false nếu XML không hợp lệ hoặc thiếu trường bắt buộc.
bool parse(const QString& xml, WifiProfile* out);

/// Chuỗi hex không phân cách, chữ hoa (định dạng Windows dùng cho <hex> SSID/khóa WEP).
QString toHex(const QByteArray& bytes);
QByteArray fromHex(const QString& hex);
} // namespace WlanProfileXml
