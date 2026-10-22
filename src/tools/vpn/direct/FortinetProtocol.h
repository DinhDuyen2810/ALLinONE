#pragma once

#include "DirectVpnTypes.h"

#include <QByteArray>
#include <QString>

/// Phần THUẦN của client FortiGate SSL-VPN (không mạng) - dựng nội dung yêu cầu và đọc phản hồi. Luồng:
///   1. POST /remote/logincheck  (username, credential, realm, ajax=1) -> cookie SVPNCOOKIE
///      - hoặc "ret=...,tokeninfo=..." = cần mã OTP (FortiToken/email/SMS): gửi lại kèm "code" và các trường
///        reqid/polid/grp/portal/peer/magic máy chủ trả về;
///      - HTTP 405 = sai tên đăng nhập/mật khẩu (FortiOS dùng mã này, không phải 401/403).
///   2. GET /remote/fortisslvpn_xml -> XML cấu hình (IP được cấp, DNS, chia tuyến)
///   3. GET /remote/sslvpn-tunnel (kết nối TLS mới, kèm cookie) -> từ đây là khung PPP (xem PppSession).
/// Đặc tả theo mã nguồn mở OpenConnect (fortinet.c) - Fortinet không công bố giao thức.
namespace FortinetProtocol
{
QByteArray loginBody(const QString& username, const QString& password, const QString& realm);

struct LoginOutcome
{
    enum class Kind { Success, NeedToken, BadCredentials, Failed };
    Kind kind{Kind::Failed};
    QString message;        ///< thông báo cho người dùng (Failed/BadCredentials) hoặc lời nhắc OTP của máy chủ
    QByteArray tokenFields; ///< "reqid=..&polid=..&grp=..&portal=..&peer=..&magic=.." để gửi lại cùng mã OTP
    bool ftmPush{false};    ///< máy chủ hỗ trợ đẩy thông báo FortiToken Mobile (để trống mã = chờ xác nhận trên điện thoại)
};
/// Diễn giải phản hồi của /remote/logincheck. hasSessionCookie: máy chủ đã đặt cookie SVPNCOOKIE khác rỗng.
LoginOutcome interpretLogin(int httpStatus, const QByteArray& body, bool hasSessionCookie);

/// Nội dung gửi lại mã OTP. code rỗng + ftmPush = yêu cầu đẩy FortiToken Mobile (bỏ "magic", thêm ftmpush=1).
QByteArray tokenBody(const QString& username, const QString& code, const QString& realm, const QByteArray& tokenFields,
                     bool ftmPush);

/// Đọc XML /remote/fortisslvpn_xml. false + *error nếu không phải XML cấu hình SSL-VPN.
bool parseConfigXml(const QByteArray& xml, TunnelConfig* out, QString* error);

/// Dòng yêu cầu mở đường hầm (một kết nối TLS mới; thành công thì máy chủ KHÔNG trả HTTP mà vào PPP ngay).
QByteArray tunnelRequest(const QString& hostHeader, const QByteArray& svpnCookie);
} // namespace FortinetProtocol
