#include "FortinetProtocol.h"

#include <QUrl>
#include <QXmlStreamReader>

namespace FortinetProtocol
{
namespace
{
QByteArray enc(const QString& s)
{
    return QUrl::toPercentEncoding(s);
}

/// Phản hồi dạng "ret=2,reqid=123,polid=1-1-1,grp=VPN,...,magic=1-abc,tokeninfo=,chal_msg=..." -> giá trị khóa.
QString field(const QByteArray& body, const QByteArray& key)
{
    const QList<QByteArray> parts = body.trimmed().split(',');
    for (const QByteArray& p : parts)
    {
        const int eq = p.indexOf('=');
        if (eq > 0 && p.left(eq).trimmed() == key)
            return QString::fromUtf8(p.mid(eq + 1));
    }
    return {};
}
} // namespace

QByteArray loginBody(const QString& username, const QString& password, const QString& realm)
{
    return "username=" + enc(username) + "&credential=" + enc(password) + "&realm=" + enc(realm) +
           "&ajax=1&just_logged_in=1";
}

LoginOutcome interpretLogin(int httpStatus, const QByteArray& body, bool hasSessionCookie)
{
    LoginOutcome out;
    if (hasSessionCookie)
    {
        out.kind = LoginOutcome::Kind::Success;
        return out;
    }
    const QByteArray b = body.trimmed();
    if (httpStatus == 405)
    {
        // FortiOS dùng "405 Method Not Allowed" để báo sai thông tin đăng nhập (đã ghi nhận ở OpenConnect).
        out.kind = LoginOutcome::Kind::BadCredentials;
        out.message = QStringLiteral("Sai tên đăng nhập hoặc mật khẩu.");
        return out;
    }
    if (b.startsWith("ret=") && b.contains(",tokeninfo="))
    {
        out.kind = LoginOutcome::Kind::NeedToken;
        out.ftmPush = field(b, "tokeninfo").startsWith(QStringLiteral("ftm_push"));
        // Giữ nguyên thứ tự; "magic" phải ở CUỐI (bỏ được khi dùng ftm push - xem tokenBody).
        for (const char* k : {"reqid", "polid", "grp", "portal", "peer", "magic"})
        {
            const QString v = field(b, k);
            if (v.isEmpty() && QByteArray(k) != "magic")
                continue;
            if (!out.tokenFields.isEmpty())
                out.tokenFields += '&';
            out.tokenFields += QByteArray(k) + '=' + enc(v);
        }
        out.message = field(b, "chal_msg");
        if (out.message.isEmpty())
            out.message = QStringLiteral("Máy chủ yêu cầu mã xác thực 2 lớp (OTP).");
        return out;
    }
    if (b.startsWith("ret=0") || b.contains("err=sslvpn_login"))
    {
        out.kind = LoginOutcome::Kind::BadCredentials;
        const QString err = field(b, "redir");
        out.message = err.contains(QStringLiteral("permission_denied"))
                          ? QStringLiteral("Tài khoản không có quyền dùng SSL-VPN trên máy chủ này.")
                          : QStringLiteral("Đăng nhập bị từ chối (sai tài khoản/mật khẩu hoặc tài khoản bị khóa).");
        return out;
    }
    if (httpStatus == 401)
    {
        out.kind = LoginOutcome::Kind::Failed;
        out.message = QStringLiteral("Máy chủ yêu cầu xác thực 2 lớp dạng biểu mẫu web - chưa hỗ trợ kết nối trực tiếp. "
                                     "Hãy dùng \"Mở phần mềm\".");
        return out;
    }
    out.kind = LoginOutcome::Kind::Failed;
    out.message = httpStatus > 0 ? QStringLiteral("Máy chủ trả mã HTTP %1 khi đăng nhập (không nhận được cookie phiên).").arg(httpStatus)
                                 : QStringLiteral("Không nhận được cookie phiên từ máy chủ.");
    return out;
}

QByteArray tokenBody(const QString& username, const QString& code, const QString& realm, const QByteArray& tokenFields,
                     bool ftmPush)
{
    QByteArray body = "username=" + enc(username) + "&code=" + enc(code) + "&realm=" + enc(realm);
    if (!tokenFields.isEmpty())
        body += '&' + tokenFields;
    if (ftmPush && code.isEmpty())
    {
        const int m = body.indexOf("&magic=");
        if (m >= 0)
            body.truncate(m);
        body += "&ftmpush=1";
    }
    return body;
}

bool parseConfigXml(const QByteArray& xml, TunnelConfig* out, QString* error)
{
    TunnelConfig cfg;
    QXmlStreamReader r(xml);
    bool sawRoot = false;
    bool inIpv4 = false;
    bool splitNegate = false;
    bool inSplit = false;
    while (!r.atEnd())
    {
        r.readNext();
        if (r.isStartElement())
        {
            const auto name = r.name();
            const auto attrs = r.attributes();
            if (!sawRoot)
            {
                if (name != QLatin1String("sslvpn-tunnel"))
                    break;
                sawRoot = true;
                continue;
            }
            if (name == QLatin1String("ipv4"))
                inIpv4 = true;
            else if (inIpv4 && name == QLatin1String("assigned-addr"))
            {
                const QString ip = attrs.value("ipv4").toString();
                if (DirectVpn::parseIpv4(ip, nullptr))
                    cfg.ipv4 = ip;
            }
            else if (inIpv4 && name == QLatin1String("dns"))
            {
                const QString ip = attrs.value("ip").toString();
                if (DirectVpn::parseIpv4(ip, nullptr) && !cfg.dns.contains(ip) && cfg.dns.size() < 3)
                    cfg.dns << ip;
                const QString domain = attrs.value("domain").toString().trimmed();
                if (!domain.isEmpty() && !cfg.dnsSuffix.contains(domain))
                    cfg.dnsSuffix << domain;
            }
            else if (inIpv4 && name == QLatin1String("split-tunnel-info"))
            {
                inSplit = true;
                splitNegate = attrs.value("negate").toString() == QLatin1String("1");
            }
            else if (inIpv4 && inSplit && name == QLatin1String("addr"))
            {
                IpRoute route;
                if (DirectVpn::parseRoute(attrs.value("ip").toString(), attrs.value("mask").toString(), &route))
                    (splitNegate ? cfg.excludeRoutes : cfg.includeRoutes) << route;
            }
        }
        else if (r.isEndElement())
        {
            if (r.name() == QLatin1String("ipv4"))
                inIpv4 = false;
            else if (r.name() == QLatin1String("split-tunnel-info"))
                inSplit = false;
        }
    }
    if (!sawRoot || (r.hasError() && r.error() != QXmlStreamReader::PrematureEndOfDocumentError))
    {
        if (error)
            *error = QStringLiteral("Máy chủ không trả về cấu hình SSL-VPN hợp lệ (có thể phiên đăng nhập đã hết hạn).");
        return false;
    }
    if (out)
        *out = cfg;
    return true;
}

QByteArray tunnelRequest(const QString& hostHeader, const QByteArray& svpnCookie)
{
    return "GET /remote/sslvpn-tunnel HTTP/1.1\r\n"
           "Host: " + hostHeader.toLatin1() + "\r\n"
           "User-Agent: Mozilla/5.0 SV1\r\n"
           "Cookie: SVPNCOOKIE=" + svpnCookie + "\r\n"
           "\r\n";
}
} // namespace FortinetProtocol
