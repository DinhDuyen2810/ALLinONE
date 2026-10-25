// CÔNG CỤ KIỂM TRA TAY: chạy ĐÚNG lớp IkeSession (như ứng dụng dùng trong tiến trình helper) với PSK + tài
// khoản THẬT để kiểm bắt tay IKEv1 đầu-cuối tới FortiGate IPsec, nhưng KHÔNG tạo adapter/ESP (chỉ cần xem bắt
// tay tới đâu - không cần quyền Admin). In nhật ký từng bước. Bí mật truyền qua tham số dòng lệnh, KHÔNG ghi ra
// tệp, nhật ký IkeSession KHÔNG in mật khẩu/PSK.
//   ike_real_connect <gatewayIp> <user> <pass> <psk>
#include "tools/vpn/direct/IkeSession.h"
#include "tools/vpn/direct/DirectVpnTypes.h"
#include "tools/vpn/direct/GpEsp.h"
#include "tools/vpn/direct/IkeKeys.h"

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

// Gói IPv4 + TCP SYN tới dst:dport (vd 172.16.6.25:443) - máy chủ HTTPS sẽ trả SYN-ACK nếu tới nơi.
static QByteArray buildTcpSyn(quint32 src, quint32 dst, quint16 sport, quint16 dport)
{
    QByteArray tcp;
    put16(tcp, sport); put16(tcp, dport);
    put32(tcp, 0x12345678);           // seq
    put32(tcp, 0);                     // ack
    tcp.append(char(0x50)); tcp.append(char(0x02)); // data offset=5, flags=SYN
    put16(tcp, 0xFAF0);                // window
    put16(tcp, 0); put16(tcp, 0);      // checksum (sau), urgent
    QByteArray pseudo;
    put32(pseudo, src); put32(pseudo, dst);
    pseudo.append(char(0)); pseudo.append(char(6)); put16(pseudo, static_cast<quint16>(tcp.size()));
    quint16 tck = inetChecksum(pseudo + tcp);
    tcp[16] = char(tck >> 8); tcp[17] = char(tck & 0xFF);

    QByteArray ip;
    ip.append(char(0x45)); ip.append(char(0));
    put16(ip, static_cast<quint16>(20 + tcp.size()));
    put16(ip, 0x1236); put16(ip, 0x4000); // id, DF
    ip.append(char(64)); ip.append(char(6)); // TTL, proto=TCP
    put16(ip, 0);
    put32(ip, src); put32(ip, dst);
    const quint16 ick = inetChecksum(ip);
    ip[10] = char(ick >> 8); ip[11] = char(ick & 0xFF);
    return ip + tcp;
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
        // FortiClient làm TOÀN BỘ IKE+ESP trên UDP 4500<->4500 từ cổng nguồn 4500 (0 gói cổng 500). Bắt chước:
        // bind cổng 4500 + ép NAT-T ngay từ msg1 (có dấu NONESP). Cổng tạm -> NAT/gateway không đưa ESP về.
        if (!m_sock->bind(QHostAddress(QHostAddress::AnyIPv4), 4500))
            std::printf("  (canh bao: khong bind duoc UDP 4500, dung cong tam)\n");
        connect(m_sock, &QUdpSocket::readyRead, this, &Driver::onRx);

        m_ike = new IkeSession(this);
        m_ike->configure(cfg);
        m_ike->setNatActive(true); // 4500 ngay tu dau
        m_natT = true;
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
            if (m_espDelayMs > 0)
            {
                m_pendingRes = r;
                m_upAt = m_start.elapsed();
                m_waitEsp = true;
                std::printf("  [cho %d s (chi DPD, CHUA gui ESP) de xem gateway co tu gui DELETE khong]\n", m_espDelayMs / 1000);
            }
            else
                startEspTest(r);
        });

        // Điểm cuối cục bộ (cho NAT-D): dùng socket UDP "connect" để biết IP nguồn tuyến tới gateway.
        {
            QUdpSocket probe;
            probe.connectToHost(QHostAddress(m_gw), 500);
            probe.waitForConnected(500);
            const quint32 localIp = probe.localAddress().toIPv4Address();
            m_ike->setLocalEndpoint(localIp, m_sock->localPort());
            std::printf("  Local endpoint (NAT-D): %s:%u\n", DirectVpn::ipv4ToString(localIp).toUtf8().constData(), m_sock->localPort());
        }

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
            std::printf("  [rx %d byte tu %s:%u]\n", dg.size(), from.toString().toUtf8().constData(), fp);
            if (fp == 4500 && !m_natT) // gateway đã chuyển NAT-T -> ta cũng gửi qua 4500
            {
                m_natT = true;
                m_ike->setNatActive(true);
                std::printf("  [gateway chuyen 4500 -> NAT-T]\n");
            }
            const bool marker = static_cast<quint8>(dg[0]) == 0 && static_cast<quint8>(dg[1]) == 0 &&
                                static_cast<quint8>(dg[2]) == 0 && static_cast<quint8>(dg[3]) == 0;
            if ((fp == 4500 || m_natT) && marker)
                m_ike->onDatagram(dg.mid(4));
            else if (m_espMode) // gói ESP (SPI != 0) sau khi lên - thử mở bằng TỪNG biến thể KEYMAT
            {
                const quint32 spi = (quint8(dg[0])<<24)|(quint8(dg[1])<<16)|(quint8(dg[2])<<8)|quint8(dg[3]);
                QByteArray ip; quint8 nh = 0;
                int hit = -1;
                for (int v = 0; v < m_variants.size(); ++v)
                    if (m_variants[v]->open(dg, &ip, &nh)) { hit = v; break; }
                std::printf("  <= nhan %d byte tu cong %d, SPI=%08x, mo boi bien the: %s\n",
                            dg.size(), fp, spi, hit >= 0 ? m_vnames[hit].toUtf8().constData() : "KHONG BIEN THE NAO");
                if (hit >= 0)
                {
                    std::printf("\n== TIM RA KEYMAT DUNG: '%s'. gateway giai ma + tra loi duoc goi cua ta! ==\n",
                                m_vnames[hit].toUtf8().constData());
                    QCoreApplication::exit(0);
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
        if (m_waitEsp)
        {
            static qint64 lastDpdW = -1;
            if (lastDpdW < 0 || t - lastDpdW >= 5000) { lastDpdW = t; m_ike->sendDpd(); }
            if (t - m_upAt >= m_espDelayMs) { m_waitEsp = false; std::printf("  [het cho - bat dau gui ESP]\n"); startEspTest(m_pendingRes); }
            return;
        }
        if (m_espMode)
        {
            // NAT-T keepalive (1 byte 0xFF) mỗi 20s như app/FortiClient - giữ ánh xạ NAT.
            static qint64 lastKa = 0;
            if (t - lastKa >= 20000) { lastKa = t; m_sock->writeDatagram(QByteArray(1, '\xFF'), QHostAddress(m_gw), 4500); }
            // DPD R-U-THERE mỗi 5s: nếu gateway ACK -> IKE SA còn sống (lỗi ở SA/khóa ESP); nếu im -> gateway bỏ phiên.
            static qint64 lastDpd = -1;
            if (lastDpd < 0 || t - lastDpd >= 5000) { lastDpd = t; m_ike->sendDpd(); }
            if (t - m_espSentMs >= 2000) { m_espSentMs = t; sendEspPings(); } // gửi lại ping
            if (t - m_espStartMs >= 75000)
            {
                std::printf("\n== BAT TAY OK nhung ESP KHONG co goi du lieu ve trong 75s. ==\n");
                QCoreApplication::exit(0);
            }
            return;
        }
        if (t - lastRe >= 3000) { lastRe = t; m_ike->onRetransmit(); }
        if (t >= 40000) { std::printf("\n== QUA HAN 40s - khong hoan tat bat tay ==\n"); QCoreApplication::exit(3); }
    }

    void sendEspPings()
    {
        // MỖI vòng thử MỘT biến thể KEYMAT (tránh trùng seq trên cùng SPI). Gửi TCP SYN + ICMP tới 172.16.6.25.
        if (m_variants.isEmpty()) return;
        const int i = m_round % m_variants.size();
        ++m_round;
        GpEspCrypto* c = m_variants[i];
        const QByteArray syn = buildTcpSyn(m_res.assignedIp, 0xAC100619u, static_cast<quint16>(50001 + i), 443);
        const QByteArray e1 = c->seal(syn, 4);
        if (!e1.isEmpty()) m_sock->writeDatagram(e1, QHostAddress(m_gw), 4500);
        const QByteArray icmp = buildIcmpEcho(m_res.assignedIp, 0xAC100619u, 0x4f41, static_cast<quint16>(i + 1));
        const QByteArray e2 = c->seal(icmp, 4);
        if (!e2.isEmpty()) m_sock->writeDatagram(e2, QHostAddress(m_gw), 4500);
        std::printf("  -> vong %d: gui ESP (SYN+ICMP 172.16.6.25) bang bien the '%s'\n", m_round, m_vnames[i].toUtf8().constData());

        // Dò ĐÍCH CHẮC CHẮN TRẢ LỜI bằng biến thể khóa ĐÚNG CHUẨN (index 0 = 'PFS'): máy chủ DNS gateway cấp.
        // DNS server sẽ đáp truy vấn hợp lệ -> nếu tầng dữ liệu chạy thì ta phải thấy gói ESP về.
        {
            GpEspCrypto* c0 = m_variants[0];
            // Danh sách đích dò bằng khóa CHUẨN: DNS gateway cấp + 8.8.8.8/1.1.1.1 (DNS công cộng, CHẮC CHẮN đáp qua full-tunnel).
            QList<quint32> dnsTargets;
            if (!m_res.dns.isEmpty()) dnsTargets << m_res.dns[0];
            dnsTargets << 0x08080808u << 0x01010101u; // 8.8.8.8, 1.1.1.1
            for (quint32 dns : dnsTargets)
            {
                const QByteArray q = buildDnsQuery(m_res.assignedIp, dns);
                const QByteArray eq = c0->seal(q, 4);
                if (!eq.isEmpty()) m_sock->writeDatagram(eq, QHostAddress(m_gw), 4500);
                const QByteArray ip2 = buildIcmpEcho(m_res.assignedIp, dns, 0x4f42, static_cast<quint16>(m_round));
                const QByteArray eip = c0->seal(ip2, 4);
                if (!eip.isEmpty()) m_sock->writeDatagram(eip, QHostAddress(m_gw), 4500);
            }
            if (m_round <= 2)
                std::printf("     + dò DNS/ICMP toi DNS-cap + 8.8.8.8 + 1.1.1.1 (khóa chuẩn 'PFS')\n");
        }
    }

    // KEYMAT linh hoạt: hash prf, g^xy đầu/cuối/không.
    static QByteArray km(IkeCrypto::Hash h, const QByteArray& d, quint32 spiNet, const QByteArray& ni, const QByteArray& nr,
                         int len, const QByteArray& gxy, bool gxyEnd)
    {
        QByteArray spiB; put32(spiB, spiNet);
        const QByteArray midp = QByteArray(1, '\x03') + spiB + ni + nr;
        const QByteArray fixed = gxyEnd ? (midp + gxy) : (gxy + midp);
        QByteArray out, k = IkeCrypto::prf(h, d, fixed);
        out += k;
        while (out.size() < len) { k = IkeCrypto::prf(h, d, k + fixed); out += k; }
        return out.left(len);
    }

    struct Opt { QByteArray gxy; bool swapN=false; bool swapSpi=false; bool authFirst=false; bool gxyEnd=false; IkeCrypto::Hash h=IkeCrypto::Hash::Sha256; };
    GlobalProtectProtocol::EspConfig cfgFor(const Opt& o)
    {
        GlobalProtectProtocol::EspConfig e;
        e.present = true; e.udpPort = 4500;
        e.encAlgo = m_res.esp.encAlgo; e.hmacAlgo = m_res.esp.hmacAlgo;
        e.spiOut = m_res.esp.spiOut; e.spiIn = m_res.esp.spiIn;
        const int el = m_res.kmEncLen, hl = m_res.kmHmacLen, need = el + hl;
        const QByteArray ni = o.swapN ? m_res.kmNr : m_res.kmNi;
        const QByteArray nr = o.swapN ? m_res.kmNi : m_res.kmNr;
        const quint32 spiForOut = o.swapSpi ? m_res.esp.spiIn : m_res.esp.spiOut;
        const quint32 spiForIn = o.swapSpi ? m_res.esp.spiOut : m_res.esp.spiIn;
        const QByteArray kmOut = km(o.h, m_res.kmSkeyidD, spiForOut, ni, nr, need, o.gxy, o.gxyEnd);
        const QByteArray kmIn = km(o.h, m_res.kmSkeyidD, spiForIn, ni, nr, need, o.gxy, o.gxyEnd);
        if (o.authFirst) { e.hmacKeyOut = kmOut.left(hl); e.encKeyOut = kmOut.mid(hl, el); e.hmacKeyIn = kmIn.left(hl); e.encKeyIn = kmIn.mid(hl, el); }
        else { e.encKeyOut = kmOut.left(el); e.hmacKeyOut = kmOut.mid(el, hl); e.encKeyIn = kmIn.left(el); e.hmacKeyIn = kmIn.mid(el, hl); }
        return e;
    }

    void startEspTest(const IkeSession::Result& r)
    {
        m_res = r;
        const QByteArray g = r.kmGxy;
        using H = IkeCrypto::Hash;
        QList<QPair<QString, Opt>> vs;
        vs << qMakePair(QString("PFS"), Opt{g}); // biến thể ĐÚNG CHUẨN RFC (luôn ở index 0)
        if (!m_clean)
        {
            vs << qMakePair(QString("PFS-SHA1prf"), Opt{g, false, false, false, false, H::Sha1});
            vs << qMakePair(QString("PFS-gxyEnd"), Opt{g, false, false, false, true, H::Sha256});
            vs << qMakePair(QString("PFS-authFirst"), Opt{g, false, false, true, false, H::Sha256});
            vs << qMakePair(QString("PFS-swapN"), Opt{g, true});
            vs << qMakePair(QString("PFS-swapSPI"), Opt{g, false, true});
            vs << qMakePair(QString("noPFS"), Opt{QByteArray()});
            vs << qMakePair(QString("noPFS-SHA1prf"), Opt{QByteArray(), false, false, false, false, H::Sha1});
        }
        for (const auto& v : vs)
        {
            auto* c = new GpEspCrypto();
            QString err;
            if (c->init(cfgFor(v.second), &err)) { m_variants << c; m_vnames << v.first; }
            else { std::printf("  bien the %s loi: %s\n", v.first.toUtf8().constData(), err.toUtf8().constData()); delete c; }
        }
        std::printf("  %s%d bien the KEYMAT. Gui TCP SYN + ICMP toi 172.16.6.25 moi vong...\n",
                    m_clean ? "CLEAN mode (chi bien the chuan, khong SPI gia): " : "Thu ", m_variants.size());
        // Chẩn đoán: gửi ESP với SPI GIẢ -> nếu gateway tra INVALID-SPI (notify) thi SA that DA cai (loi khoa);
        // neu im ca voi SPI gia thi gateway khong bao loi (hoac khong nhan duoc goi ta).
        // CLEAN mode: KHÔNG gửi SPI giả (tránh gateway tra DELETE/INVALID-SPI lam sap SA).
        if (!m_clean)
        {
            QByteArray bogus; bogus.append("\xde\xad\xbe\xef", 4); bogus.append("\x00\x00\x00\x01", 4);
            bogus.append(QByteArray(80, 'x'));
            m_sock->writeDatagram(bogus, QHostAddress(m_gw), 4500);
            std::printf("  -> gui 1 ESP SPI GIA (0xdeadbeef) de thu INVALID-SPI\n");
        }
        m_espMode = true;
        m_espStartMs = m_start.elapsed();
        m_espSentMs = m_espStartMs;
        sendEspPings();
    }

    quint32 m_gw;
    QUdpSocket* m_sock;
    IkeSession* m_ike;
    QTimer* m_tick;
    QElapsedTimer m_start;
    bool m_natT = false;

    // Thử tầng dữ liệu ESP - nhiều biến thể KEYMAT
    QList<GpEspCrypto*> m_variants;
    QStringList m_vnames;
    int m_round = 0;
    bool m_espMode = false;
    IkeSession::Result m_res;
    qint64 m_espStartMs = 0;
    qint64 m_espSentMs = 0;
    int m_espReplies = 0;
    bool m_waitEsp = false;
    qint64 m_upAt = 0;
    IkeSession::Result m_pendingRes;
public:
    bool m_clean = false; ///< chỉ gửi biến thể khóa ĐÚNG + không gửi SPI giả (tránh làm gateway sập SA)
    int m_espDelayMs = 0; ///< trễ trước khi gửi ESP (chỉ DPD) - để phân biệt DELETE tự phát vs do ESP
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
    c.qmIdAssigned = (argc >= 7) ? (QString::fromLatin1(argv[6]).toInt() != 0) : true; // arg6: 1=IDci IP cap/32 (FortiClient), 0=0.0.0.0/0
    c.qmEncap = (argc >= 8) ? QString::fromLatin1(argv[7]).toInt() : 0; // arg7: 0=Tunnel(1), 1=61443, 2=3, 3=da-transform

    const bool clean = (argc >= 9) ? (QString::fromLatin1(argv[8]).toInt() != 0) : false; // arg8: 1=clean (chi khoa dung, khong SPI gia)
    std::printf("== Bat tay IKEv1 THAT toi %s (user %s, PFS=%d, IDci=%s, encap=%d, clean=%d) ==\n", argv[1], argv[2],
                c.pfs ? 1 : 0, c.qmIdAssigned ? "IPcap/32" : "0.0.0.0/0", c.qmEncap, clean ? 1 : 0);
    const int espDelay = (argc >= 10) ? QString::fromLatin1(argv[9]).toInt() : 0; // arg9: tre N giay moi gui ESP (chi DPD truoc)
    Driver d(gw, c);
    d.m_clean = clean;
    d.m_espDelayMs = espDelay * 1000;
    return app.exec();
}
