#include "GlobalProtectProtocol.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QRegularExpression>
#include <QUrl>
#include <QXmlStreamReader>

namespace GlobalProtectProtocol
{
namespace
{
QByteArray opt(const char* key, const QString& value)
{
    return '&' + QByteArray(key) + '=' + QUrl::toPercentEncoding(value);
}

/// Giá trị rỗng/"(null)"/"-1"/"empty" GlobalProtect dùng để chỉ "không có".
QString cleaned(const QString& v)
{
    const QString t = v.trimmed();
    if (t.isEmpty() || t == QLatin1String("(null)") || t == QLatin1String("-1") || t == QLatin1String("empty"))
        return {};
    return t;
}

QString unescapeJs(const QString& s)
{
    QString out;
    for (int i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\\' && i + 1 < s.size())
        {
            const QChar c = s[++i];
            out += c == 'n' ? QChar('\n') : c;
        }
        else
            out += s[i];
    }
    return out;
}
} // namespace

bool parsePrelogin(const QByteArray& xml, Prelogin* out, QString* error)
{
    Prelogin p;
    QXmlStreamReader r(xml);
    bool root = false;
    QString status;
    QString msg;
    while (!r.atEnd())
    {
        r.readNext();
        if (!r.isStartElement())
            continue;
        if (!root)
        {
            if (r.name() != QLatin1String("prelogin-response"))
                break;
            root = true;
            continue;
        }
        const QString name = r.name().toString();
        const QString text = r.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
        if (name == QLatin1String("status"))
            status = text;
        else if (name == QLatin1String("msg"))
            msg = text;
        else if (name == QLatin1String("authentication-message"))
            p.message = text;
        else if (name == QLatin1String("username-label"))
            p.userLabel = text;
        else if (name == QLatin1String("password-label"))
            p.passLabel = text;
        else if (name == QLatin1String("saml-auth-method") || name == QLatin1String("saml-request"))
            p.saml = p.saml || !text.isEmpty();
    }
    if (!root)
    {
        if (error)
            *error = QStringLiteral("Máy chủ không phải GlobalProtect (không có prelogin-response).");
        return false;
    }
    if (status.compare(QLatin1String("Success"), Qt::CaseInsensitive) != 0)
    {
        if (error)
            *error = msg.isEmpty() ? QStringLiteral("GlobalProtect từ chối (prelogin: %1).").arg(status) : msg;
        return false;
    }
    if (out)
        *out = p;
    return true;
}

QByteArray loginBody(const LoginParams& p)
{
    QByteArray b = "jnlpReady=jnlpReady&ok=Login&direct=yes&clientVer=4100&prot=https:&internal=no";
    b += opt("ipv6-support", QStringLiteral("no"));
    b += opt("clientos", QStringLiteral("Windows"));
    b += opt("os-version", QStringLiteral("Microsoft Windows 10 Pro , 64-bit"));
    b += opt("server", p.server);
    b += opt("computer", p.computer);
    if (!p.portalUserAuthCookie.isEmpty())
        b += opt("portal-userauthcookie", p.portalUserAuthCookie);
    if (!p.portalPrelogonCookie.isEmpty())
        b += opt("portal-prelogonuserauthcookie", p.portalPrelogonCookie);
    if (!p.inputStr.isEmpty())
        b += opt("inputStr", p.inputStr);
    b += opt("user", p.username);
    b += opt("passwd", p.password);
    return b;
}

bool parsePortalConfig(const QByteArray& xml, PortalConfig* out, QString* error)
{
    PortalConfig c;
    QXmlStreamReader r(xml);
    bool root = false;
    QStringList path; // ngăn xếp tên phần tử
    while (!r.atEnd())
    {
        r.readNext();
        if (r.isStartElement())
        {
            const QString name = r.name().toString();
            if (!root)
            {
                if (name != QLatin1String("policy"))
                    break;
                root = true;
            }
            path << name;
            // policy/gateways/external/list/entry name="host[:port]"
            if (name == QLatin1String("entry") && path.size() >= 5 && path[path.size() - 2] == QLatin1String("list") &&
                path[path.size() - 3] == QLatin1String("external") && path[path.size() - 4] == QLatin1String("gateways"))
            {
                const QString gw = r.attributes().value("name").toString().trimmed();
                QString host;
                int port = 443;
                if (DirectVpn::splitHostPort(gw, 443, &host, &port))
                    c.gateways << (port == 443 ? host : host + ':' + QString::number(port));
            }
            else if (path.size() == 2 && (name == QLatin1String("portal-userauthcookie") ||
                                          name == QLatin1String("portal-prelogonuserauthcookie") ||
                                          name == QLatin1String("version")))
            {
                // CHỈ đọc văn bản các phần tử lá cần dùng - readElementText trên phần tử chứa (vd <gateways>)
                // sẽ nuốt cả cây con, làm mất danh sách gateway.
                const QString text = r.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                path.removeLast();
                if (name == QLatin1String("portal-userauthcookie"))
                    c.userAuthCookie = cleaned(text);
                else if (name == QLatin1String("portal-prelogonuserauthcookie"))
                    c.prelogonCookie = cleaned(text);
                else if (name == QLatin1String("version"))
                    c.version = text;
            }
        }
        else if (r.isEndElement() && !path.isEmpty())
            path.removeLast();
    }
    if (!root)
    {
        if (error)
            *error = QStringLiteral("Portal GlobalProtect không trả về cấu hình hợp lệ.");
        return false;
    }
    if (c.gateways.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Cấu hình portal không liệt kê gateway nào.");
        return false;
    }
    if (out)
        *out = c;
    return true;
}

QByteArray GatewayAuth::cookieQuery() const
{
    QByteArray q = "authcookie=" + QUrl::toPercentEncoding(authcookie);
    if (!portal.isEmpty())
        q += opt("portal", portal);
    q += opt("user", user);
    if (!domain.isEmpty())
        q += opt("domain", domain);
    if (!preferredIp.isEmpty())
        q += opt("preferred-ip", preferredIp);
    q += opt("computer", computer);
    return q;
}

bool parseGatewayLogin(const QByteArray& xml, GatewayAuth* out, QString* error)
{
    // <jnlp><application-desc><argument>..</argument>...: ý nghĩa theo VỊ TRÍ (OpenConnect gp_login_args):
    // 0 ?, 1 authcookie, 2 persistent-cookie, 3 portal, 4 user, 5 auth-source, 6 configuration, 7 domain,
    // 8-11 ?, 12 connection-type ("tunnel"), 13 password-expiration-days, 14 clientVer, 15 preferred-ip, ...
    QStringList args;
    QXmlStreamReader r(xml);
    bool root = false;
    while (!r.atEnd())
    {
        r.readNext();
        if (!r.isStartElement())
            continue;
        if (!root)
        {
            if (r.name() != QLatin1String("jnlp"))
                break;
            root = true;
            continue;
        }
        if (r.name() == QLatin1String("argument"))
            args << QUrl::fromPercentEncoding(r.readElementText().toUtf8());
    }
    if (!root || args.size() < 5)
    {
        if (error)
            *error = QStringLiteral("Gateway GlobalProtect không trả về thông tin phiên (JNLP).");
        return false;
    }
    GatewayAuth a;
    a.authcookie = cleaned(args.value(1));
    a.portal = cleaned(args.value(3));
    a.user = cleaned(args.value(4));
    a.domain = cleaned(args.value(7));
    a.preferredIp = cleaned(args.value(15));
    const QString connType = cleaned(args.value(12));
    if (a.authcookie.isEmpty() || a.user.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Gateway GlobalProtect không cấp cookie phiên (authcookie).");
        return false;
    }
    if (!connType.isEmpty() && connType != QLatin1String("tunnel"))
    {
        if (error)
            *error = QStringLiteral("Gateway GlobalProtect báo kiểu kết nối \"%1\" (không phải tunnel).").arg(connType);
        return false;
    }
    if (out)
        *out = a;
    return true;
}

ServerReply classify(const QByteArray& body)
{
    ServerReply rep;
    const QByteArray t = body.trimmed();
    // JavaScript (có thể bọc trong HTML): var respStatus = "Challenge|Error"; var respMsg = "..."; thisForm.inputStr.value = "...";
    static const QRegularExpression status(QStringLiteral("var\\s+respStatus\\s*=\\s*\"(\\w+)\""));
    static const QRegularExpression msg(QStringLiteral("var\\s+respMsg\\s*=\\s*\"((?:[^\"\\\\]|\\\\.)*)\""));
    static const QRegularExpression input(QStringLiteral("inputStr\\.value\\s*=\\s*\"([^\"]*)\""));
    const QString s = QString::fromUtf8(t);
    const auto ms = status.match(s);
    if (ms.hasMatch())
    {
        const auto mm = msg.match(s);
        rep.message = mm.hasMatch() ? unescapeJs(mm.captured(1)) : QString();
        if (ms.captured(1) == QLatin1String("Challenge"))
        {
            rep.kind = ServerReply::Kind::Challenge;
            const auto mi = input.match(s);
            rep.inputStr = mi.hasMatch() ? mi.captured(1) : QString();
            if (rep.message.isEmpty())
                rep.message = QStringLiteral("Nhập mã xác thực (OTP):");
        }
        else
        {
            rep.kind = ServerReply::Kind::Error;
            if (rep.message.isEmpty())
                rep.message = QStringLiteral("GlobalProtect báo lỗi đăng nhập.");
        }
        return rep;
    }
    if (t.startsWith('<'))
    {
        // <response status="error"><error>Invalid username or password</error></response>
        QXmlStreamReader r(t);
        while (!r.atEnd())
        {
            r.readNext();
            if (r.isStartElement() && r.name() == QLatin1String("response") &&
                r.attributes().value("status").toString().compare(QLatin1String("error"), Qt::CaseInsensitive) == 0)
            {
                QString err;
                while (!r.atEnd())
                {
                    r.readNext();
                    if (r.isStartElement() && r.name() == QLatin1String("error"))
                    {
                        err = r.readElementText().trimmed();
                        break;
                    }
                }
                rep.kind = ServerReply::Kind::Error;
                rep.message = err.isEmpty() ? QStringLiteral("GlobalProtect báo lỗi.") : err;
                return rep;
            }
            if (r.isStartElement())
                break; // phần tử gốc khác -> XML bình thường
        }
        rep.kind = ServerReply::Kind::Xml;
        return rep;
    }
    if (!t.isEmpty())
    {
        rep.kind = ServerReply::Kind::Error;
        rep.message = QString::fromUtf8(t.left(200));
    }
    return rep;
}

QByteArray getConfigBody(const GatewayAuth& auth, const QString& appVersion)
{
    QByteArray b = "client-type=1&protocol-version=p1&internal=no";
    b += opt("app-version", appVersion.isEmpty() ? QStringLiteral("6.3.0-33") : appVersion);
    b += opt("ipv6-support", QStringLiteral("no"));
    b += opt("clientos", QStringLiteral("Windows"));
    b += opt("os-version", QStringLiteral("Microsoft Windows 10 Pro , 64-bit"));
    // Quảng cáo thuật toán ESP ta hỗ trợ (qua BCrypt của Windows) - gateway chọn trong số này. Nhiều gateway
    // BẮT BUỘC kênh dữ liệu ESP (IPsec/UDP): chỉ dùng đường hầm SSL để thiết lập rồi gửi dữ liệu qua ESP, không
    // gửi gì qua SSL (đã xác nhận thật: gateway im lặng trên SSL -> rớt ở 60s). Xem GpEsp/TunnelEngine.
    b += opt("hmac-algo", QStringLiteral("sha1,sha256"));
    b += opt("enc-algo", QStringLiteral("aes-128-cbc,aes-256-cbc"));
    b += '&' + auth.cookieQuery();
    return b;
}

bool parseGatewayConfig(const QByteArray& xml, TunnelConfig* out, QString* tunnelPath, QString* error)
{
    TunnelConfig cfg;
    QString path = QStringLiteral("/ssl-tunnel-connect.sslvpn");
    QXmlStreamReader r(xml);
    bool root = false;
    QString netmask;
    QStringList stack;
    bool fullTunnel = false;
    while (!r.atEnd())
    {
        r.readNext();
        if (r.isStartElement())
        {
            const QString name = r.name().toString();
            if (!root)
            {
                if (name != QLatin1String("response"))
                    break;
                root = true;
                stack << name;
                continue;
            }
            stack << name;
            const QString parent = stack.size() >= 2 ? stack[stack.size() - 2] : QString();
            if (name == QLatin1String("member"))
            {
                const QString text = r.readElementText().trimmed();
                stack.removeLast();
                IpRoute route;
                if (parent == QLatin1String("dns") && DirectVpn::parseIpv4(text, nullptr) && !cfg.dns.contains(text) &&
                    cfg.dns.size() < 3)
                    cfg.dns << text;
                else if (parent == QLatin1String("dns-suffix") && !text.isEmpty())
                    cfg.dnsSuffix << text;
                else if (parent == QLatin1String("access-routes") && DirectVpn::parseRoute(text, &route))
                {
                    if (route.prefix == 0)
                        fullTunnel = true; // 0.0.0.0/0 = mọi lưu lượng qua VPN
                    else
                        cfg.includeRoutes << route;
                }
                else if (parent == QLatin1String("exclude-access-routes") && DirectVpn::parseRoute(text, &route))
                    cfg.excludeRoutes << route;
                continue;
            }
            if (stack.size() == 2) // con trực tiếp của <response>
            {
                const bool container = name == QLatin1String("dns") || name == QLatin1String("dns-suffix") ||
                                       name == QLatin1String("access-routes") ||
                                       name == QLatin1String("exclude-access-routes");
                if (container)
                    continue;
                const QString text = r.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                stack.removeLast();
                if (name == QLatin1String("ip-address") && DirectVpn::parseIpv4(text, nullptr))
                    cfg.ipv4 = text;
                else if (name == QLatin1String("netmask"))
                    netmask = text;
                else if (name == QLatin1String("mtu"))
                    cfg.mtu = text.toInt();
                else if (name == QLatin1String("ssl-tunnel-url") && text.startsWith('/') && !text.contains(' '))
                    path = text;
            }
        }
        else if (r.isEndElement() && !stack.isEmpty())
            stack.removeLast();
    }
    if (!root || cfg.ipv4.isEmpty())
    {
        if (error)
            *error = QStringLiteral("Gateway GlobalProtect không cấp địa chỉ IP (cấu hình thiếu ip-address).");
        return false;
    }
    const int prefix = netmask.isEmpty() ? 32 : DirectVpn::maskToPrefix(netmask);
    cfg.prefixLength = prefix > 0 ? prefix : 32;
    if (fullTunnel)
        cfg.includeRoutes.clear();
    if (out)
        *out = cfg;
    if (tunnelPath)
        *tunnelPath = path;
    return true;
}

bool parseEspConfig(const QByteArray& xml, EspConfig* out)
{
    EspConfig c;
    QXmlStreamReader r(xml);
    bool inIpsec = false;
    while (!r.atEnd())
    {
        r.readNext();
        if (!r.isStartElement())
            continue;
        const QString name = r.name().toString();
        if (name == QLatin1String("ipsec"))
        {
            inIpsec = true;
            c.present = true;
            continue;
        }
        if (name == QLatin1String("gw-address"))
        {
            quint32 a = 0;
            if (DirectVpn::parseIpv4(r.readElementText().trimmed(), &a))
                c.magicV4 = a;
            continue;
        }
        if (!inIpsec)
            continue;
        auto hexKey = [&r]() {
            // <ekey-c2s><bits>128</bits><val>hex</val></ekey-c2s> - lấy <val>.
            QByteArray key;
            const QString end = r.name().toString();
            while (!r.atEnd())
            {
                r.readNext();
                if (r.isStartElement() && r.name() == QLatin1String("val"))
                    key = QByteArray::fromHex(r.readElementText().trimmed().toLatin1());
                else if (r.isEndElement() && r.name() == end)
                    break;
            }
            return key;
        };
        if (name == QLatin1String("udp-port"))
            c.udpPort = r.readElementText().toInt();
        else if (name == QLatin1String("enc-algo"))
            c.encAlgo = r.readElementText().trimmed().toLower();
        else if (name == QLatin1String("hmac-algo"))
            c.hmacAlgo = r.readElementText().trimmed().toLower();
        else if (name == QLatin1String("c2s-spi"))
            c.spiOut = r.readElementText().trimmed().toUInt(nullptr, 16);
        else if (name == QLatin1String("s2c-spi"))
            c.spiIn = r.readElementText().trimmed().toUInt(nullptr, 16);
        else if (name == QLatin1String("ekey-c2s"))
            c.encKeyOut = hexKey();
        else if (name == QLatin1String("ekey-s2c"))
            c.encKeyIn = hexKey();
        else if (name == QLatin1String("akey-c2s"))
            c.hmacKeyOut = hexKey();
        else if (name == QLatin1String("akey-s2c"))
            c.hmacKeyIn = hexKey();
        else if (name == QLatin1String("ipsec") && r.isEndElement())
            inIpsec = false;
    }
    // Đủ dùng khi có đủ 2 SPI + 4 khóa + thuật toán + địa chỉ magic.
    const bool ok = c.present && c.spiOut && c.spiIn && !c.encKeyOut.isEmpty() && !c.encKeyIn.isEmpty() &&
                    !c.hmacKeyOut.isEmpty() && !c.hmacKeyIn.isEmpty() && c.encAlgo.startsWith("aes") && c.magicV4;
    if (ok && out)
        *out = c;
    return ok;
}

QString hipMd5(const GatewayAuth& auth)
{
    // Chuỗi ổn định cho (máy + gateway) - KHÔNG gồm authcookie/preferred-ip (đổi mỗi phiên).
    const QByteArray basis = (auth.portal + '|' + auth.user + '|' + auth.domain + '|' + auth.computer).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(basis, QCryptographicHash::Md5).toHex());
}

QByteArray hipCheckBody(const GatewayAuth& auth, const QString& clientIp, const QString& md5)
{
    QByteArray b = "client-role=global-protect-full&" + auth.cookieQuery();
    b += opt("client-ip", clientIp);
    b += opt("md5", md5);
    return b;
}

bool hipReportNeeded(const QByteArray& xml)
{
    QXmlStreamReader r(xml);
    while (!r.atEnd())
    {
        r.readNext();
        if (r.isStartElement() && r.name() == QLatin1String("hip-report-needed"))
            return r.readElementText().trimmed().compare(QLatin1String("no"), Qt::CaseInsensitive) != 0;
    }
    return false; // không rõ -> coi như không cần (tránh nộp thừa gây lỗi)
}

QString buildHipReportXml(const GatewayAuth& auth, const QString& clientIp, const QString& md5, const QString& appVersion)
{
    const QString ver = appVersion.isEmpty() ? QStringLiteral("6.3.0-33") : appVersion;
    const QDateTime now = QDateTime::currentDateTime();
    const QString nowStr = now.toString(QStringLiteral("MM/dd/yyyy HH:mm:ss"));
    const QString user = auth.user.toHtmlEscaped();
    const QString domain = auth.domain.toHtmlEscaped();
    const QString pc = auth.computer.toHtmlEscaped();
    // Bản Windows tối thiểu theo mẫu công khai OpenConnect (trojans/hipreport.sh) - đủ cho gateway chấp nhận.
    return QStringLiteral(
               "<hip-report name=\"hip-report\">\n"
               "<md5-sum>%1</md5-sum>\n"
               "<user-name>%2</user-name>\n"
               "<domain>%3</domain>\n"
               "<host-name>%4</host-name>\n"
               "<host-id>deadbeef-dead-beef-dead-beefdeadbeef</host-id>\n"
               "<ip-address>%5</ip-address>\n"
               "<ipv6-address></ipv6-address>\n"
               "<generate-time>%6</generate-time>\n"
               "<hip-report-version>4</hip-report-version>\n"
               "<categories>\n"
               "<entry name=\"host-info\">\n"
               "<client-version>%7</client-version>\n"
               "<os>Microsoft Windows 10 Pro , 64-bit</os>\n"
               "<os-vendor>Microsoft</os-vendor>\n"
               "<domain>%3.internal</domain>\n"
               "<host-name>%4</host-name>\n"
               "<host-id>deadbeef-dead-beef-dead-beefdeadbeef</host-id>\n"
               "<network-interface>\n"
               "<entry name=\"{DEADBEEF-DEAD-BEEF-DEAD-BEEFDEADBEEF}\">\n"
               "<description>PANGP Virtual Ethernet Adapter</description>\n"
               "<mac-address>01-02-03-00-00-01</mac-address>\n"
               "<ip-address><entry name=\"%5\"/></ip-address>\n"
               "<ipv6-address><entry name=\"\"/></ipv6-address>\n"
               "</entry>\n"
               "</network-interface>\n"
               "</entry>\n"
               "<entry name=\"firewall\">\n"
               "<list>\n"
               "<entry><ProductInfo><Prod name=\"Microsoft Windows Firewall\" version=\"10.0\" vendor=\"Microsoft Corp.\">"
               "</Prod><is-enabled>yes</is-enabled></ProductInfo></entry>\n"
               "</list>\n"
               "</entry>\n"
               "</categories>\n"
               "</hip-report>")
        .arg(md5, user, domain, pc, clientIp, nowStr, ver.toHtmlEscaped());
}

QByteArray hipReportBody(const GatewayAuth& auth, const QString& clientIp, const QByteArray& reportXml)
{
    QByteArray b = "client-role=global-protect-full&" + auth.cookieQuery();
    b += opt("client-ip", clientIp);
    b += "&report=" + QUrl::toPercentEncoding(QString::fromUtf8(reportXml));
    return b;
}

QByteArray tunnelRequest(const QString& path, const GatewayAuth& auth)
{
    // Đúng dạng client chính thức (OpenConnect gpst_connect): không có Host, chỉ authcookie + user.
    return "GET " + path.toLatin1() + "?authcookie=" + QUrl::toPercentEncoding(auth.authcookie) +
           "&user=" + QUrl::toPercentEncoding(auth.user) + " HTTP/1.1\r\n\r\n";
}
} // namespace GlobalProtectProtocol

// ---------------------------------------------------------------------------------------------------------

namespace GpFraming
{
namespace
{
constexpr quint32 kMagic = 0x1a2b3c4d;
quint16 be16(const QByteArray& b, int pos)
{
    return static_cast<quint16>((static_cast<quint8>(b[pos]) << 8) | static_cast<quint8>(b[pos + 1]));
}
} // namespace

QByteArray encapsulateIpv4(const QByteArray& ip)
{
    QByteArray h(16, '\0');
    h[0] = 0x1a; h[1] = 0x2b; h[2] = 0x3c; h[3] = 0x4d;
    h[4] = 0x08; h[5] = 0x00;
    h[6] = static_cast<char>((ip.size() >> 8) & 0xFF);
    h[7] = static_cast<char>(ip.size() & 0xFF);
    h[8] = 0x01; // "01 00 00 00 00 00 00 00"
    return h + ip;
}

QByteArray keepalive()
{
    QByteArray h(16, '\0');
    h[0] = 0x1a; h[1] = 0x2b; h[2] = 0x3c; h[3] = 0x4d;
    return h;
}

Deframer::Result Deframer::next(QByteArray* ip, QString* error)
{
    if (m_buf.size() < 16)
        return Result::NeedMore;
    const quint32 magic = (static_cast<quint32>(be16(m_buf, 0)) << 16) | be16(m_buf, 2);
    if (magic != kMagic)
    {
        if (error)
            *error = m_buf.startsWith("HTTP/") ? QString::fromLatin1(m_buf.left(m_buf.indexOf('\n'))).trimmed()
                                                : QStringLiteral("Gói đường hầm GlobalProtect hỏng (sai magic).");
        return Result::Error;
    }
    const quint16 ethertype = be16(m_buf, 4);
    const quint16 len = be16(m_buf, 6);
    if (m_buf.size() < 16 + len)
        return Result::NeedMore;
    const QByteArray payload = m_buf.mid(16, len);
    m_buf.remove(0, 16 + len);
    if (ethertype == 0)
        return Result::Keepalive;
    if (ethertype == 0x0800)
    {
        if (ip)
            *ip = payload;
        return Result::Packet;
    }
    if (ethertype == 0x86DD)
        return Result::Skip;
    if (error)
        *error = QStringLiteral("Gói đường hầm GlobalProtect lạ (EtherType 0x%1).").arg(ethertype, 4, 16, QChar('0'));
    return Result::Error;
}
} // namespace GpFraming
