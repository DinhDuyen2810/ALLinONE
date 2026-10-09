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

/// Tên (SSID dạng chuỗi, cũng là tên hồ sơ) có đi NGUYÊN VẸN qua XML hồ sơ và qua tham số chuỗi của WLAN
/// API không. false khi rỗng hoặc chứa: U+0000 (WLAN API nhận chuỗi kết thúc bằng NUL nên tên bị cắt cụt
/// tại đó), ký tự xuống dòng CR (bộ đọc XML chuẩn hóa CR thành LF), ký tự điều khiển khác ngoài TAB/LF,
/// U+FFFE/U+FFFF, surrogate lẻ (XML 1.0 không chở được). Với các tên này, hồ sơ Windows tạo ra sẽ mang
/// một tên KHÁC tên ta dùng để kết nối/hoàn tác - nên phải từ chối trước khi dựng hồ sơ.
bool isSafeProfileName(const QString& name);

/// Hồ sơ (XML) có khai báo một SSID mang ĐÚNG các byte ssidBytes không (so theo <SSID><hex>, không theo tên
/// hồ sơ - tên hồ sơ không buộc trùng SSID: Windows tự đặt "Foo 2" khi đã có hồ sơ "Foo", tệp XML nhập vào
/// tự đặt tên tùy ý). Dùng để nhận ra hồ sơ trùng TÊN nhưng thuộc mạng KHÁC trước khi ghi đè nó.
/// Trả true khi XML không đọc được hoặc không có <hex> nào - không đủ căn cứ để nói là "mạng khác".
bool describesSsid(const QString& xml, const QByteArray& ssidBytes);

/// Chuỗi hex không phân cách, chữ hoa (định dạng Windows dùng cho <hex> SSID/khóa WEP).
QString toHex(const QByteArray& bytes);
QByteArray fromHex(const QString& hex);
} // namespace WlanProfileXml
