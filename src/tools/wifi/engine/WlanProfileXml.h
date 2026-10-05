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
QString build(const QByteArray& ssidBytes, const QString& ssidDisplayName, WifiSecurity security,
             const QString& password, bool autoConnect);

/// Phân tích XML hồ sơ đã lưu thành WifiProfile (không điền password/rawXml, chỉ name/ssid/security/autoConnect).
/// Trả về false nếu XML không hợp lệ hoặc thiếu trường bắt buộc.
bool parse(const QString& xml, WifiProfile* out);

/// Chuỗi hex không phân cách, chữ hoa (định dạng Windows dùng cho <hex> SSID/khóa WEP).
QString toHex(const QByteArray& bytes);
QByteArray fromHex(const QString& hex);
} // namespace WlanProfileXml
