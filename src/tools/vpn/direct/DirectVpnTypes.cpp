#include "DirectVpnTypes.h"

#include <QRegularExpression>

namespace DirectVpn
{

QString protocolName(DirectProtocol p)
{
    switch (p)
    {
        case DirectProtocol::FortinetSsl: return QStringLiteral("FortiGate SSL-VPN");
        case DirectProtocol::GlobalProtect: return QStringLiteral("GlobalProtect");
        case DirectProtocol::None: default: return QStringLiteral("(không hỗ trợ)");
    }
}

bool parseIpv4(const QString& text, quint32* out)
{
    const QStringList parts = text.trimmed().split(QChar('.'));
    if (parts.size() != 4)
        return false;
    quint32 v = 0;
    for (const QString& part : parts)
    {
        // Chỉ chữ số thập phân, 1-3 ký tự, không số 0 thừa ở đầu (tránh hiểu nhầm bát phân "010").
        if (part.isEmpty() || part.size() > 3 || (part.size() > 1 && part[0] == '0'))
            return false;
        for (const QChar c : part)
            if (c < '0' || c > '9')
                return false;
        const int n = part.toInt();
        if (n > 255)
            return false;
        v = (v << 8) | static_cast<quint32>(n);
    }
    if (out)
        *out = v;
    return true;
}

QString ipv4ToString(quint32 a)
{
    return QStringLiteral("%1.%2.%3.%4").arg((a >> 24) & 0xFF).arg((a >> 16) & 0xFF).arg((a >> 8) & 0xFF).arg(a & 0xFF);
}

int maskToPrefix(const QString& mask)
{
    quint32 m = 0;
    if (!parseIpv4(mask, &m))
        return -1;
    int prefix = 0;
    while (prefix < 32 && (m & (0x80000000u >> prefix)))
        ++prefix;
    // Phần còn lại phải toàn 0 (mặt nạ liền mạch).
    const quint32 expected = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
    return m == expected ? prefix : -1;
}

namespace
{
IpRoute normalized(quint32 net, int prefix)
{
    const quint32 mask = prefix == 0 ? 0u : (0xFFFFFFFFu << (32 - prefix));
    return IpRoute{net & mask, prefix};
}
} // namespace

bool parseRoute(const QString& text, IpRoute* out)
{
    const QString t = text.trimmed();
    const int slash = t.indexOf('/');
    quint32 net = 0;
    if (slash < 0)
    {
        if (!parseIpv4(t, &net))
            return false;
        if (out)
            *out = IpRoute{net, 32};
        return true;
    }
    if (!parseIpv4(t.left(slash), &net))
        return false;
    const QString rest = t.mid(slash + 1);
    int prefix = -1;
    if (rest.contains('.'))
        prefix = maskToPrefix(rest);
    else
    {
        bool ok = false;
        prefix = rest.toInt(&ok);
        if (!ok || rest.isEmpty() || rest.size() > 2)
            prefix = -1;
    }
    if (prefix < 0 || prefix > 32)
        return false;
    if (out)
        *out = normalized(net, prefix);
    return true;
}

bool parseRoute(const QString& ip, const QString& mask, IpRoute* out)
{
    quint32 net = 0;
    const int prefix = maskToPrefix(mask);
    if (!parseIpv4(ip, &net) || prefix < 0)
        return false;
    if (out)
        *out = normalized(net, prefix);
    return true;
}

QString routeToString(const IpRoute& r)
{
    return ipv4ToString(r.network) + QChar('/') + QString::number(r.prefix);
}

bool splitHostPort(const QString& server, int defaultPort, QString* host, int* port)
{
    QString s = server.trimmed();
    if (s.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
        s = s.mid(8);
    else if (s.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive))
        s = s.mid(7);
    const int pathPos = s.indexOf('/');
    if (pathPos >= 0)
        s = s.left(pathPos);
    QString h = s;
    int p = defaultPort;
    const int colon = s.lastIndexOf(':');
    if (colon >= 0 && s.count(':') == 1) // IPv6 không hỗ trợ ở đây (FortiClient/GP lưu IPv4/tên miền)
    {
        h = s.left(colon);
        bool ok = false;
        p = s.mid(colon + 1).toInt(&ok);
        if (!ok || p <= 0 || p > 65535)
            return false;
    }
    // Tên miền/IPv4: chữ, số, '.', '-' - không cho ký tự khác lọt vào URL/yêu cầu HTTP.
    static const QRegularExpression valid(QStringLiteral("^[A-Za-z0-9.-]{1,253}$"));
    if (h.isEmpty() || !valid.match(h).hasMatch() || h.startsWith('.') || h.startsWith('-'))
        return false;
    if (host)
        *host = h.toLower();
    if (port)
        *port = p;
    return true;
}

} // namespace DirectVpn
