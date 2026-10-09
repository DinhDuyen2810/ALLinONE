#pragma once

#include <QHash>
#include <QList>
#include <QString>

#include "../model/VpnProfile.h"

/// Một kết nối VPN mà Windows đang biết (từ `Get-VpnConnection`) - CỘNG thêm nhãn quốc gia do người
/// dùng tự đặt (Windows không có khái niệm "quốc gia" cho một kết nối VPN, chỉ có tên + địa chỉ máy
/// chủ) để hiển thị tiện hơn.
struct VpnConnectionStatus
{
    QString name;
    QString serverAddress;
    QString tunnelType;      // chuỗi Windows trả về, vd "Ikev2", "Automatic"...
    QString connectionStatus; // "Connected" / "Disconnected"
    QString countryLabel;     // nhãn tự đặt, rỗng nếu chưa đặt
    QString username;         // tên đăng nhập đã lưu (điền sẵn lúc kết nối), rỗng nếu chưa lưu - KHÔNG có mật khẩu

    bool isConnected() const { return connectionStatus.compare("Connected", Qt::CaseInsensitive) == 0; }
};

/**
 * @brief Lớp duy nhất quản lý kết nối VPN HỆ THỐNG của Windows (IKEv2/L2TP/SSTP/PPTP - tích hợp sẵn,
 * KHÔNG cần cài phần mềm VPN nào khác) qua các cmdlet PowerShell chính thức (Add-VpnConnection/
 * Get-VpnConnection/Remove-VpnConnection - cùng API mà Cài đặt > Mạng > VPN của Windows dùng).
 *
 * Người dùng TỰ cung cấp địa chỉ máy chủ + tài khoản từ chính nhà cung cấp VPN họ đã đăng ký (hoặc VPN
 * cơ quan/tự dựng) - ứng dụng này KHÔNG cung cấp bất kỳ máy chủ VPN nào, chỉ là lớp quản lý/kết nối,
 * giống hệt tinh thần đã áp dụng cho WiFi (chỉ quản lý hợp pháp) và Android Control (dùng adb/scrcpy
 * có sẵn, không tự dựng hạ tầng).
 *
 * Mật khẩu KHÔNG được lưu trong `vpn_profiles.json` của ứng dụng (AppPaths::profileFile, tức
 * %LOCALAPPDATA%\OneForAll\profiles) - tệp đó chỉ giữ nhãn quốc gia + tên đăng nhập. Xem VpnConnector.h.
 */
namespace VpnController
{
/// Thông tin phụ ứng dụng tự lưu cho một tên kết nối (Windows không biết các trường này).
struct VpnProfileMeta
{
    QString countryLabel;
    QString username; // chỉ để điền sẵn ô "Tên đăng nhập" lúc kết nối - mật khẩu KHÔNG BAO GIỜ được lưu
};

/// Liệt kê MỌI kết nối VPN Windows đang biết (không chỉ của riêng ứng dụng này) - đã ghép nhãn quốc
/// gia + tên đăng nhập tự đặt (nếu có) từ vpn_profiles.json.
QList<VpnConnectionStatus> listConnections(QString* error = nullptr);

/// Thêm một kết nối VPN mới vào Windows (per-user, không cần quyền Administrator) + lưu nhãn quốc gia
/// và tên đăng nhập. Trả về true nếu Windows đã tạo kết nối; khi đó *error (nếu khác rỗng) chỉ là CẢNH
/// BÁO rằng không lưu được thông tin phụ.
bool addConnection(const VpnProfile& profile, QString* error = nullptr);

/// Xóa một kết nối VPN khỏi Windows + xóa thông tin phụ đã lưu.
bool removeConnection(const QString& name, QString* error = nullptr);

/// Đọc/ghi thông tin phụ đã lưu (không đụng gì tới kết nối VPN thật trong Windows). Ghi nguyên tử;
/// trả về false + lý do nếu không ghi được tệp.
VpnProfileMeta profileMeta(const QString& name);
bool setProfileMeta(const QString& name, const VpnProfileMeta& meta, QString* error = nullptr);

/// Chỉ đổi nhãn quốc gia đã lưu, giữ nguyên tên đăng nhập.
void setCountryLabel(const QString& name, const QString& countryLabel);

// ---- Lõi thuần (không gọi PowerShell thật) - tách riêng để kiểm thử được bằng dữ liệu mẫu ----
namespace internal
{
QList<VpnConnectionStatus> parseConnectionsJson(const QByteArray& json, QString* error);
/// Thoát chuỗi để chèn vào giữa '...' trong script PowerShell - lớp bọc của
/// PowerShellRunner::quoteLiteral (nhân đôi cả ' lẫn các dấu nháy cong U+2018/2019/201A/201B).
QString escapePsString(const QString& s);
QString buildAddConnectionScript(const VpnProfile& profile);
QString buildRemoveConnectionScript(const QString& name);
QString buildListConnectionsScript();
} // namespace internal
} // namespace VpnController
