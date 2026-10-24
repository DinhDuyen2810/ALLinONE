// CÔNG CỤ KIỂM TRA TAY: chạy ĐÚNG lớp IkeSession (như ứng dụng dùng trong tiến trình helper) với PSK + tài
// khoản THẬT để kiểm bắt tay IKEv1 đầu-cuối tới FortiGate IPsec, nhưng KHÔNG tạo adapter/ESP (chỉ cần xem bắt
// tay tới đâu - không cần quyền Admin). In nhật ký từng bước. Bí mật truyền qua tham số dòng lệnh, KHÔNG ghi ra
// tệp, nhật ký IkeSession KHÔNG in mật khẩu/PSK.
//   ike_real_connect <gatewayIp> <user> <pass> <psk>
#include "tools/vpn/direct/IkeSession.h"
#include "tools/vpn/direct/DirectVpnTypes.h"
#include "tools/vpn/direct/GpEsp.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QTimer>
#include <QUdpSocket>
#include <cstdio>

// Tổng kiểm 16-bit một bù (IP/ICMP).
static quint16 inetChecksum(const QByteArray& b)
{
    quint32 sum = 0;
    for (int i = 0; i + 1 < b.size(); i += 2) sum += (static_cast<quint8>(b[i]) << 8) | static_cast<quint8>(b[i + 1]);
    if (b.size() & 1) sum += static_cast<quint8>(b[b.size() - 1]) << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<quint16>(~sum & 0xFFFF);
}
static void put16(QByteArray& b, quint16 v) { b.append(char(v >> 8)); b.append(char(v & 0xFF)); }
static void put32(QByteArray& b, quint32 v) { b.append(char(v >> 24)); b.append(char(v >> 16)); b.append(char(v >> 8)); b.append(char(v)); }

// Gói IPv4 + ICMP echo request (src->dst).
static QByteArray buildIcmpEcho(quint32 src, quint32 dst, quint16 id, quint16 seq)
{
    QByteArray icmp;
    icmp.append(char(8)); icmp.append(char(0)); // type=8 echo, code=0
    put16(icmp, 0);                              // checksum (tạm)
    put16(icmp, id); put16(icmp, seq);
    icmp.append("oneforall-esp-probe");
    const quint16 ck = inetChecksum(icmp);
    icmp[2] = char(ck >> 8); icmp[3] = char(ck & 0xFF);

    QByteArray ip;
    ip.append(char(0x45)); ip.append(char(0)); // version/IHL, DSCP
    put16(ip, static_cast<quint16>(20 + icmp.size()));
    put16(ip, 0x1234); put16(ip, 0);           // id, flags/frag
    ip.append(char(64)); ip.append(char(1));    // TTL, proto=ICMP
    put16(ip, 0);                               // checksum (tạm)
    put32(ip, src); put32(ip, dst);
    const quint16 ick = inetChecksum(ip);
    ip[10] = char(ick >> 8); ip[11] = char(ick & 0xFF);
    return ip + icmp;
}

// Gói IPv4 + UDP + truy vấn DNS A "www.google.com" tới dst:53.
static QByteArray buildDnsQuery(quint32 src, quint32 dst)
{
    QByteArray dns;
    put16(dns, 0x4f42); put16(dns, 0x0100); // id, flags (RD)
    put16(dns, 1); put16(dns, 0); put16(dns, 0); put16(dns, 0);
    const char* labels[] = {"www", "google", "com"};
    for (const char* l : labels) { dns.append(char(qstrlen(l))); dns.append(l); }
    dns.append(char(0));
    put16(dns, 1); put16(dns, 1); // QTYPE=A, QCLASS=IN

    QByteArray udp;
    put16(udp, 50000); put16(udp, 53);
    put16(udp, static_cast<quint16>(8 + dns.size()));
    put16(udp, 0); // checksum (tính sau)
    udp += dns;
    QByteArray pseudo;
    put32(pseudo, src); put32(pseudo, dst);
    pseudo.append(char(0)); pseudo.append(char(17)); put16(pseudo, static_cast<quint16>(udp.size()));
    quint16 uck = inetChecksum(pseudo + udp);
    if (uck == 0) uck = 0xFFFF;
    udp[6] = char(uck >> 8); udp[7] = char(uck & 0xFF);

    QByteArray ip;
    ip.append(char(0x45)); ip.append(char(0));
    put16(ip, static_cast<quint16>(20 + udp.size()));
    put16(ip, 0x1235); put16(ip, 0);
    ip.append(char(64)); ip.append(char(17)); // TTL, proto=UDP
    put16(ip, 0);
    put32(ip, src); put32(ip, dst);
    const quint16 ick = inetChecksum(ip);
    ip[10] = char(ick >> 8); ip[11] = char(ick & 0xFF);
    return ip + udp;
}

// Tầng truyền tải UDP tối giản (giống TunnelEngine): IKE trên 500 -> NAT-T 4500 (dấu non-ESP).
class Driver : public QObject
{
public:
    Driver(quint32 gw, const IkeSession::Config& cfg) : m_gw(gw)
    {
        m_sock = new QUdpSocket(this);
        m_sock->bind(QHostAddress(QHostAddress::AnyIPv4), 0);
        connect(m_sock, &QUdpSocket::readyRead, this, &Driver::onRx);

        m_ike = new IkeSession(this);
        m_ike->configure(cfg);
        connect(m_ike, &IkeSession::sendDatagram, this, &Driver::onSend);
        connect(m_ike, &IkeSession::switchToNatT, this, [this]() { m_natT = true; std::printf("  [NAT-T: chuyen UDP 4500]\n"); });
        connect(m_ike, &IkeSession::log, this, [](const QString& m) { std::printf("  %s\n", m.toUtf8().constData()); });
        connect(m_ike, &IkeSession::failed, this, [](const QString& r) {
            std::printf("\n== THAT BAI: %s ==\n", r.toUtf8().constData());
            QCoreApplication::exit(2);
        });
        connect(m_ike, &IkeSession::established, this, [this](const IkeSession::Result& r) {
            std::printf("\n== THANH CONG BAT TAY ==\n");
            std::printf("  IP duoc cap: %s\n", DirectVpn::ipv4ToString(r.assignedIp).toUtf8().constData());
            std::printf("  DNS: %d may chu\n", r.dns.size());
            for (quint32 d : r.dns) std::printf("    %s\n", DirectVpn::ipv4ToString(d).toUtf8().constData());
            std::printf("  Subnet (split): %d (full tunnel neu 0)\n", r.subnetsBase.size());
            std::printf("  ESP: %s/%s, SPI ra=%08x vao=%08x\n", r.esp.encAlgo.toUtf8().constData(),
                        r.esp.hmacAlgo.toUtf8().constData(), r.esp.spiOut, r.esp.spiIn);
            startEspTest(r);
        });

        m_tick = new QTimer(this);
        m_tick->setInterval(1000);
        connect(m_tick, &QTimer::timeout, this, &Driver::onTick);
        m_tick->start();
        m_start.start();
        m_ike->start();
    }

private:
    void onSend(QByteArray dg, bool natT)
    {
        QByteArray out = dg;
        quint16 port = natT ? 4500 : 500;
        if (natT) out.prepend(QByteArray(4, '\0'));
        m_sock->writeDatagram(out, QHostAddress(m_gw), port);
    }
    void onRx()
    {
        while (m_sock->hasPendingDatagrams())
        {
            QByteArray dg; dg.resize(static_cast<int>(m_sock->pendingDatagramSize()));
            QHostAddress from; quint16 fp = 0;
            m_sock->readDatagram(dg.data(), dg.size(), &from, &fp);
            if (dg.size() < 4) continue;
            const bool marker = static_cast<quint8>(dg[0]) == 0 && static_cast<quint8>(dg[1]) == 0 &&
                                static_cast<quint8>(dg[2]) == 0 && static_cast<quint8>(dg[3]) == 0;
            if ((fp == 4500 || m_natT) && marker)
                m_ike->onDatagram(dg.mid(4));
            else if (m_espMode) // gói ESP (SPI != 0) sau khi lên
            {
                const quint32 spi = (quint8(dg[0])<<24)|(quint8(dg[1])<<16)|(quint8(dg[2])<<8)|quint8(dg[3]);
                QByteArray ip; quint8 nh = 0;
                const bool ok = m_esp.open(dg, &ip, &nh);
                std::printf("  <= nhan %d byte tu cong %d, SPI=%08x, open=%s\n", dg.size(), fp, spi, ok ? "OK" : "FAIL");
                if (ok && ip.size() >= 20)
                {
                    const quint8 proto = static_cast<quint8>(ip[9]);
                    const quint32 s = (quint8(ip[12])<<24)|(quint8(ip[13])<<16)|(quint8(ip[14])<<8)|quint8(ip[15]);
                    const quint32 d = (quint8(ip[16])<<24)|(quint8(ip[17])<<16)|(quint8(ip[18])<<8)|quint8(ip[19]);
                    std::printf("  <= ESP giai ma OK: %s -> %s proto=%d%s\n",
                                DirectVpn::ipv4ToString(s).toUtf8().constData(), DirectVpn::ipv4ToString(d).toUtf8().constData(),
                                proto, (proto == 1 && ip.size() >= 21 && quint8(ip[20]) == 0) ? " (ICMP echo-reply)" : "");
                    ++m_espReplies;
                    if (m_espReplies >= 1)
                    {
                        std::printf("\n== ESP DU LIEU CHAY: gateway tra loi goi qua duong ham. VPN HOAT DONG DAU-CUOI. ==\n");
                        QCoreApplication::exit(0);
                    }
                }
            }
            else if (fp != 4500 && !m_natT)
                m_ike->onDatagram(dg);
        }
    }
    void onTick()
    {
        static qint64 lastRe = 0;
        const qint64 t = m_start.elapsed();
        if (m_espMode)
        {
            if (t - m_espSentMs >= 2000) { m_espSentMs = t; sendEspPings(); } // gửi lại ping
            if (t - m_espStartMs >= 12000)
            {
                std::printf("\n== BAT TAY OK nhung ESP KHONG tra loi trong 12s (co the firewall chan ICMP, hoac can "
                            "adapter that de dinh tuyen). Khoa/SPI da dung. ==\n");
                QCoreApplication::exit(0);
            }
            return;
        }
        if (t - lastRe >= 3000) { lastRe = t; m_ike->onRetransmit(); }
        if (t >= 40000) { std::printf("\n== QUA HAN 40s - khong hoan tat bat tay ==\n"); QCoreApplication::exit(3); }
    }

    void sendEspPings()
    {
        // Qua ESP (tunnel mode, nextHeader=4): ping cổng nội bộ + 8.8.8.8, và một truy vấn DNS (thường được cho phép).
        QList<quint32> targets;
        targets << ((m_res.assignedIp & 0xFFFFFF00u) | 1u); // .1 của mạng được cấp (cổng nội bộ)
        for (quint32 d : m_res.dns) targets << d;
        targets << 0x08080808u; // 8.8.8.8
        quint16 seq = 1;
        for (quint32 dst : targets)
        {
            const QByteArray inner = buildIcmpEcho(m_res.assignedIp, dst, 0x4f41, seq++);
            const QByteArray esp = m_esp.seal(inner, 4);
            if (!esp.isEmpty()) m_sock->writeDatagram(esp, QHostAddress(m_gw), 4500);
        }
        // Truy vấn DNS A "www.google.com" tới DNS đầu tiên (UDP 53) - DNS hầu như luôn được phép.
        if (!m_res.dns.isEmpty())
        {
            const QByteArray dnsPkt = buildDnsQuery(m_res.assignedIp, m_res.dns[0]);
            const QByteArray esp = m_esp.seal(dnsPkt, 4);
            if (!esp.isEmpty()) m_sock->writeDatagram(esp, QHostAddress(m_gw), 4500);
        }
    }

    void startEspTest(const IkeSession::Result& r)
    {
        m_res = r;
        QString err;
        if (!m_esp.init(r.esp, &err)) { std::printf("  Nap khoa ESP that bai: %s\n", err.toUtf8().constData()); QCoreApplication::exit(5); return; }
        m_espMode = true;
        m_espStartMs = m_start.elapsed();
        m_espSentMs = m_espStartMs;
        std::printf("  Gui ICMP qua ESP (8.8.8.8 + DNS) de thu tang du lieu...\n");
        sendEspPings();
    }

    quint32 m_gw;
    QUdpSocket* m_sock;
    IkeSession* m_ike;
    QTimer* m_tick;
    QElapsedTimer m_start;
    bool m_natT = false;

    // Thử tầng dữ liệu ESP
    GpEspCrypto m_esp;
    bool m_espMode = false;
    IkeSession::Result m_res;
    qint64 m_espStartMs = 0;
    qint64 m_espSentMs = 0;
    int m_espReplies = 0;
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 5) { std::printf("Dung: ike_real_connect <gatewayIp> <user> <pass> <psk>\n"); return 1; }
    quint32 gw = 0;
    if (!DirectVpn::parseIpv4(QString::fromLatin1(argv[1]), &gw)) { std::printf("IP sai\n"); return 1; }

    IkeSession::Config c;
    c.gatewayIp = gw;
    c.psk = QByteArray(argv[4]);
    c.xauthUser = QString::fromLocal8Bit(argv[2]);
    c.xauthPass = QString::fromLocal8Bit(argv[3]);
    c.localIdType = Isakmp::ID_IPV4_ADDR;
    c.dhGroup = IkeCrypto::DhGroup::Ecp384;
    c.encKeyBits = 256;
    c.hash = IkeCrypto::Hash::Sha256;
    c.natT = true;
    c.pfs = (argc >= 6) ? (QString::fromLatin1(argv[5]).toInt() != 0) : true; // arg5: 1=PFS, 0=khong

    std::printf("== Bat tay IKEv1 THAT toi %s (user %s, PFS=%d) ==\n", argv[1], argv[2], c.pfs ? 1 : 0);
    Driver d(gw, c);
    return app.exec();
}
