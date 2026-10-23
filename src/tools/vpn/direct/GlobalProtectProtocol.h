#pragma once

#include "DirectVpnTypes.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

/// Phần THUẦN của client Palo Alto GlobalProtect (không mạng). Luồng (theo mã nguồn mở OpenConnect -
/// auth-globalprotect.c / gpst.c; Palo Alto không công bố giao thức):
///   1. POST /global-protect/prelogin.esp      -> kiểu xác thực (tài khoản/mật khẩu hay SAML)
///   2. POST /global-protect/getconfig.esp     (user, passwd) -> danh sách gateway + portal-userauthcookie
///   3. POST /ssl-vpn/prelogin.esp + /ssl-vpn/login.esp ở gateway -> đối số JNLP (authcookie, portal, user...)
///   4. POST /ssl-vpn/getconfig.esp            -> IP được cấp, DNS, tuyến
///   5. GET /ssl-tunnel-connect.sslvpn?authcookie=..&user=..  -> "START_TUNNEL" rồi gói IP thô (xem GpFraming)
/// Mã OTP (2 lớp) có thể tới dạng JavaScript: var respStatus = "Challenge"; var respMsg = "..."; ...inputStr.
namespace GlobalProtectProtocol
{
struct Prelogin
{
    bool saml{false};   ///< máy chủ đòi đăng nhập SAML qua trình duyệt - chưa hỗ trợ kết nối trực tiếp
    QString message;    ///< authentication-message (lời nhắc)
    QString userLabel;
    QString passLabel;
};
bool parsePrelogin(const QByteArray& xml, Prelogin* out, QString* error);

struct LoginParams
{
    QString server;          ///< host của portal/gateway đang gửi tới
    QString computer;        ///< tên máy (GlobalProtect gửi kèm)
    QString username;
    QString password;        ///< mật khẩu - hoặc mã OTP khi trả lời challenge
    QString inputStr;        ///< khóa của challenge OTP (rỗng nếu không phải trả lời challenge)
    QString portalUserAuthCookie;
    QString portalPrelogonCookie;
};
QByteArray loginBody(const LoginParams& p);

struct PortalConfig
{
    QStringList gateways;       ///< host[:port] theo thứ tự ưu tiên
    QString userAuthCookie;     ///< portal-userauthcookie (để đăng nhập gateway không hỏi lại)
    QString prelogonCookie;
    QString version;            ///< phiên bản GlobalProtect portal báo (gửi lại làm app-version)
};
bool parsePortalConfig(const QByteArray& xml, PortalConfig* out, QString* error);

struct GatewayAuth
{
    QString authcookie;
    QString portal;
    QString user;
    QString domain;
    QString preferredIp;
    QString computer;
    /// Chuỗi tham số dùng lại cho getconfig.esp (authcookie, portal, user, domain, preferred-ip, computer).
    QByteArray cookieQuery() const;
};
bool parseGatewayLogin(const QByteArray& xml, GatewayAuth* out, QString* error);

/// Phân loại phản hồi văn bản của portal/gateway trước khi đọc XML.
struct ServerReply
{
    enum class Kind { Xml, Challenge, Error, Unknown };
    Kind kind{Kind::Unknown};
    QString message;   ///< thông báo lỗi / lời nhắc OTP
    QString inputStr;  ///< khóa challenge (Kind::Challenge)
};
ServerReply classify(const QByteArray& body);

QByteArray getConfigBody(const GatewayAuth& auth, const QString& appVersion);
/// Đọc XML getconfig.esp của gateway. *tunnelPath = đường dẫn đường hầm SSL (mặc định /ssl-tunnel-connect.sslvpn).
bool parseGatewayConfig(const QByteArray& xml, TunnelConfig* out, QString* tunnelPath, QString* error);

// ---- HIP (Host Information Profile) - gateway thường YÊU CẦU báo cáo tình trạng máy sau khi nối, không nộp
// thì gateway ngừng chuyển gói (đường hầm "treo" rồi app báo mất kết nối). Client chính thức luôn kiểm + nộp.
// MD5 là định danh ổn định của (máy + gateway), không đổi theo IP/authcookie (dựa trên cookie bỏ authcookie/
// preferred-ip - đúng cách OpenConnect build_csd_token).
QString hipMd5(const GatewayAuth& auth);
/// POST /ssl-vpn/hipreportcheck.esp: client-role + cookie + client-ip + md5.
QByteArray hipCheckBody(const GatewayAuth& auth, const QString& clientIp, const QString& md5);
/// true nếu phản hồi hipreportcheck có <hip-report-needed>yes</hip-report-needed>.
bool hipReportNeeded(const QByteArray& xml);
/// Nội dung XML báo cáo HIP (bản Windows tối thiểu, theo mẫu công khai của OpenConnect).
QString buildHipReportXml(const GatewayAuth& auth, const QString& clientIp, const QString& md5, const QString& appVersion);
/// POST /ssl-vpn/hipreport.esp: client-role + cookie + client-ip + report=<XML đã mã hóa URL>.
QByteArray hipReportBody(const GatewayAuth& auth, const QString& clientIp, const QByteArray& reportXml);

QByteArray tunnelRequest(const QString& path, const GatewayAuth& auth);
} // namespace GlobalProtectProtocol

/// Khung gói trong đường hầm SSL của GlobalProtect: 16 byte đầu rồi gói IP.
///   0x1a2b3c4d | EtherType (2 byte BE, 0x0800 = IPv4, 0 = keepalive) | độ dài (2 byte BE) | 01 00 00 00 00 00 00 00
/// Keepalive/DPD: 0x1a2b3c4d rồi 12 byte 0.
namespace GpFraming
{
QByteArray encapsulateIpv4(const QByteArray& ipPacket);
QByteArray keepalive();

class Deframer
{
public:
    enum class Result { NeedMore, Packet, Keepalive, Skip, Error };
    void feed(const QByteArray& data) { m_buf.append(data); }
    /// Packet: *ip là gói IPv4. Skip: gói bỏ qua (IPv6). Error: *error mô tả.
    Result next(QByteArray* ip, QString* error);

private:
    QByteArray m_buf;
};
} // namespace GpFraming
