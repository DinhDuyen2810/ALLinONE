#pragma once

#include <QList>
#include <QString>
#include <QStringList>

/// Kết nối TRỰC TIẾP trong ứng dụng (không mở FortiClient/GlobalProtect) tới máy chủ VPN doanh nghiệp đã
/// cấu hình sẵn trên máy. Ứng dụng tự nói giao thức của máy chủ (client tự viết, tham chiếu đặc tả công khai
/// của dự án OpenConnect) và dùng adapter mạng ảo wintun.dll để đưa lưu lượng vào đường hầm.
enum class DirectProtocol
{
    None = 0,          // không hỗ trợ kết nối trực tiếp (vd FortiClient IPsec, Cisco, OpenVPN) - chỉ "Mở phần mềm"
    FortinetSsl = 1,   // FortiGate SSL-VPN (FortiClient "SSL-VPN"): đăng nhập HTTPS + PPP trong TLS
    GlobalProtect = 2, // Palo Alto GlobalProtect: portal -> gateway -> đường hầm HTTPS (gói IP thô)
};

/// Một tuyến IPv4 dạng mạng/độ dài tiền tố, thứ tự byte của MÁY (vd 10.0.0.0/8 = {0x0A000000, 8}).
struct IpRoute
{
    quint32 network{0};
    int prefix{0};

    bool operator==(const IpRoute& o) const { return network == o.network && prefix == o.prefix; }
};

/// Cấu hình mạng máy chủ cấp cho phiên VPN (gộp từ XML cấu hình + thương lượng PPP/IPCP).
struct TunnelConfig
{
    QString ipv4;                 // địa chỉ IPv4 máy chủ cấp cho máy này trong đường hầm
    int prefixLength{32};         // độ dài tiền tố của địa chỉ trên adapter (GlobalProtect có thể gửi netmask)
    QStringList dns;              // máy chủ DNS (IPv4, đã kiểm định dạng)
    QStringList dnsSuffix;        // hậu tố tìm kiếm DNS
    QList<IpRoute> includeRoutes; // chia tuyến: chỉ các mạng này đi qua VPN. RỖNG = toàn bộ lưu lượng (mặc định)
    QList<IpRoute> excludeRoutes; // các mạng KHÔNG đi qua VPN (dù nằm trong phạm vi trên)
    int mtu{0};                   // 0 = chưa rõ

    bool fullTunnel() const { return includeRoutes.isEmpty(); }
};

namespace DirectVpn
{
/// Tên hiển thị của giao thức.
QString protocolName(DirectProtocol p);

/// Phân tích "a.b.c.d" (IPv4 chuẩn, không chấp nhận dạng rút gọn/ký tự lạ) sang số thứ tự byte máy.
bool parseIpv4(const QString& text, quint32* out);
QString ipv4ToString(quint32 addr);
/// Địa chỉ dải riêng RFC 1918 (10/8, 172.16/12, 192.168/16).
bool isPrivateIpv4(quint32 addr);

/// Mặt nạ "255.255.255.0" -> độ dài tiền tố (24); -1 nếu không phải mặt nạ liền mạch hợp lệ.
int maskToPrefix(const QString& mask);

/// "10.0.0.0/8", "10.1.2.3" (=/32) hoặc "10.0.0.0/255.0.0.0" -> tuyến đã chuẩn hóa (bit máy chủ xóa về 0).
bool parseRoute(const QString& text, IpRoute* out);
/// Dạng ip + mặt nạ rời (Fortinet: ip="10.11.1.0" mask="255.255.255.0").
bool parseRoute(const QString& ip, const QString& mask, IpRoute* out);
QString routeToString(const IpRoute& r);

/// Tách "host[:port]" của hồ sơ (FortiClient lưu "1.2.3.4:10443", GlobalProtect lưu "vpn.cty.vn") - bỏ
/// tiền tố https://, đường dẫn phía sau. false nếu host rỗng/có ký tự không hợp lệ hoặc cổng sai.
bool splitHostPort(const QString& server, int defaultPort, QString* host, int* port);
} // namespace DirectVpn
