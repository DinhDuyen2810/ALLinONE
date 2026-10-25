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
#include <QFile>
#include <QHostAddress>
#include <QTimer>
#include <QUdpSocket>
#include <cstdio>

#include "RawPackets.h"

static void dissectPhase1(const char* tag, const QByteArray& q);

// In tiêu đề ISAKMP (exch/cờ/msgid/độ dài) để đối chiếu từng bản tin với bản ghi FortiClient thật.
static void printHdr(const char* dir, const QByteArray& q)
{
    if (q.size() < 28) { std::printf("  %s (%d byte, ngan)\n", dir, static_cast<int>(q.size())); return; }
    const unsigned ex = static_cast<quint8>(q[18]), fl = static_cast<quint8>(q[19]);
    const quint32 mid = (quint32(quint8(q[20])) << 24) | (quint32(quint8(q[21])) << 16) | (quint32(quint8(q[22])) << 8) | quint8(q[23]);
    std::printf("  %s exch=%u flags=0x%02x msgid=%08x len=%d\n", dir, ex, fl, mid, static_cast<int>(q.size()));
    if (ex == 4 && mid == 0) dissectPhase1(dir, q);
}

// Bóc chuỗi payload của một bản tin Phase 1 KHÔNG mã hóa (msg1/msg2 Aggressive): loại + độ dài; VID/ID in nội dung;
// SA in số proposal/transform và thuộc tính. KHÔNG in HASH/KE/Nonce (chỉ độ dài) nên không lộ bí mật.
static void dissectPhase1(const char* tag, const QByteArray& q)
{
    if (q.size() < 28 || (static_cast<quint8>(q[19]) & 1)) return; // chỉ bản tin không mã hóa
    static const char* names[] = {"NONE", "SA", "P", "T", "KE", "ID", "CERT", "CR", "HASH", "SIG", "NONCE", "N", "D", "VID", "CP"};
    int nxt = static_cast<quint8>(q[16]);
    int off = 28;
    QString line = QString("  %1 payloads:").arg(tag);
    while (nxt != 0 && off + 4 <= q.size())
    {
        const int len = (static_cast<quint8>(q[off + 2]) << 8) | static_cast<quint8>(q[off + 3]);
        if (len < 4 || off + len > q.size()) break;
        const QByteArray body = q.mid(off + 4, len - 4);
        QString nm = nxt < 15 ? QString::fromLatin1(names[nxt]) : QString("T%1").arg(nxt);
        QString extra;
        if (nxt == 13) extra = " " + QString::fromLatin1(body.toHex());
        else if (nxt == 5 && body.size() >= 4)
            extra = QString(" idtype=%1 data=%2").arg(static_cast<quint8>(body[0])).arg(QString::fromLatin1(body.mid(4).toHex()));
        else if (nxt == 1 && body.size() >= 8)
        {
            int props = 0, trans = 0;
            int p = 8;
            QString attrs;
            while (p + 8 <= body.size())
            {
                const int pl = (static_cast<quint8>(body[p + 2]) << 8) | static_cast<quint8>(body[p + 3]);
                if (pl < 8) break;
                ++props;
                const int ntr = static_cast<quint8>(body[p + 7]);
                int t = p + 8 + static_cast<quint8>(body[p + 6]);
                for (int i = 0; i < ntr && t + 8 <= p + pl; ++i)
                {
                    const int tl = (static_cast<quint8>(body[t + 2]) << 8) | static_cast<quint8>(body[t + 3]);
                    if (tl < 8) break;
                    ++trans;
                    QString a;
                    for (int ap = t + 8; ap + 4 <= t + tl;)
                    {
                        const quint16 at = (static_cast<quint8>(body[ap]) << 8) | static_cast<quint8>(body[ap + 1]);
                        const quint16 v = (static_cast<quint8>(body[ap + 2]) << 8) | static_cast<quint8>(body[ap + 3]);
                        a += QString("%1=%2,").arg(at & 0x7fff).arg(v);
                        ap += (at & 0x8000) ? 4 : 4 + v;
                    }
                    attrs += QString("\n      T%1[%2]").arg(static_cast<quint8>(body[t + 4])).arg(a);
                    t += tl;
                }
                p += pl;
            }
            extra = QString(" props=%1 trans=%2%3").arg(props).arg(trans).arg(attrs);
        }
        line += QString("\n    %1(len=%2)%3").arg(nm).arg(len).arg(extra);
        nxt = static_cast<quint8>(q[off]);
        off += len;
    }
    std::printf("%s\n", line.toUtf8().constData());
}


// Tầng truyền tải UDP tối giản (giống TunnelEngine): IKE trên 500 -> NAT-T 4500 (dấu non-ESP).
class Driver : public QObject
{
public:
    Driver(quint32 gw, const IkeSession::Config& cfg) : m_gw(gw)
    {
        // ĐÚNG như FortiClient (bắt gói thật 2026-10-25): Phase 1 msg1/msg2 trên cổng 500 (IKE thuần),
        // RỒI MỚI float sang 4500 (có dấu NONESP) cho msg3+/ESP. Phải có bước float 500->4500 thì FortiGate
        // mới đánh dấu SA phase2 là UDP-encapsulated -> mới nhận ESP bọc UDP-4500. 2 socket: 500 và 4500.
        // IKE_ONE_SOCKET=1: MỘT socket cổng tạm cho cả cổng 500 lẫn 4500 (đúng mô hình TunnelEngine của sản phẩm).
        const bool oneSocket = qEnvironmentVariableIntValue("IKE_ONE_SOCKET") != 0;
        if (!oneSocket)
        {
            m_sock500 = new QUdpSocket(this);
            if (!m_sock500->bind(QHostAddress(QHostAddress::AnyIPv4), 500))
            {
                m_sock500->bind(QHostAddress(QHostAddress::AnyIPv4), 0); // IKEEXT giữ 500? dùng cổng tạm
                std::printf("  (canh bao: khong bind duoc UDP 500, dung cong tam %u)\n", m_sock500->localPort());
            }
            connect(m_sock500, &QUdpSocket::readyRead, this, &Driver::onRx);
        }
        m_sock = new QUdpSocket(this);
        if (oneSocket) m_sock500 = m_sock;
        // Cổng cục bộ cho IKE-sau-float + ESP: dùng cổng TẠM. Windows chặn ESP-in-UDP đến cổng cục bộ 4500 (tầng IPsec của HĐH
        // nhận trước, không tới socket) nên bind 4500 làm mất mọi gói ESP trả về; sau NAT, cổng cục bộ không ảnh hưởng gateway
        // (nó trả về địa chỉ:cổng đã quan sát). IKE_LOCAL_PORT=4500 để thử lại hành vi cũ.
        const quint16 localPort = static_cast<quint16>(qEnvironmentVariableIntValue("IKE_LOCAL_PORT"));
        if (!m_sock->bind(QHostAddress(QHostAddress::AnyIPv4), localPort))
            std::printf("  (canh bao: khong bind duoc UDP %u)\n", localPort);
        connect(m_sock, &QUdpSocket::readyRead, this, &Driver::onRx);

        m_ike = new IkeSession(this);
        m_ike->configure(cfg);
        // KHÔNG ép NAT-T: để IkeSession bắt đầu natActive=false (gửi msg1 tới cổng 500) rồi tự float sau msg2.
        m_natT = false;
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
            for (int i = 0; i < r.subnetsBase.size() && i < r.subnetsMask.size(); ++i)
                std::printf("    %s / %s\n", DirectVpn::ipv4ToString(r.subnetsBase[i]).toUtf8().constData(),
                            DirectVpn::ipv4ToString(r.subnetsMask[i]).toUtf8().constData());
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
            // RFC 3947: NAT-D tính theo cổng của trao đổi ban đầu (500), không phải cổng sau khi float (4500).
            m_ike->setLocalEndpoint(localIp, m_sock500->localPort());
            std::printf("  Local endpoint (NAT-D): %s:%u\n", DirectVpn::ipv4ToString(localIp).toUtf8().constData(), m_sock500->localPort());
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
        printHdr("TX", dg);
        QByteArray out = dg;
        if (natT) { out.prepend(QByteArray(4, '\0')); m_sock->writeDatagram(out, QHostAddress(m_gw), 4500); }
        else m_sock500->writeDatagram(out, QHostAddress(m_gw), 500); // Phase 1 trên cổng 500 (socket local 500)
    }
    void onRx()
    {
        QUdpSocket* s = qobject_cast<QUdpSocket*>(sender());
        if (!s) s = m_sock;
        while (s->hasPendingDatagrams())
        {
            QByteArray dg; dg.resize(static_cast<int>(s->pendingDatagramSize()));
            QHostAddress from; quint16 fp = 0;
            s->readDatagram(dg.data(), dg.size(), &from, &fp);
            if (dg.size() < 4) continue;
            std::printf("  [rx %d byte tu %s:%u]\n", dg.size(), from.toString().toUtf8().constData(), fp);
            if (fp == 4500 && !m_natT) // gateway đã chuyển NAT-T -> ta cũng gửi qua 4500
            {
                m_natT = true;
                m_ike->setNatActive(true);
                std::printf("  [gateway chuyen 4500 -> NAT-T]\n");
            }
            {
                const bool mk = dg.size() > 4 && !dg[0] && !dg[1] && !dg[2] && !dg[3];
                if (mk || fp == 500) printHdr("RX", mk ? dg.mid(4) : dg);
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
                    std::printf("     goi tra ve: %s\n", describeIp(ip).toUtf8().constData());
                    if (++m_hits == 1)
                    {
                        m_firstHitMs = m_start.elapsed();
                        std::printf("\n== THANH CONG: gateway giai ma + tra loi goi cua ta (bien the KEYMAT '%s'). Thu them vai giay... ==\n",
                                    m_vnames[hit].toUtf8().constData());
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
            if (m_hits > 0 && t - m_firstHitMs >= 6000)
            {
                std::printf("\n== KET QUA: %d goi ESP tra ve mo duoc - DU LIEU TRAO DOI 2 CHIEU QUA IPSEC/ESP THANH CONG. ==\n", m_hits);
                QCoreApplication::exit(0);
            }
            if (t - m_espSentMs >= 2000) { m_espSentMs = t; sendEspPings(); } // gửi lại ping
            if (t - m_espStartMs >= m_espWaitMs)
            {
                std::printf("\n== BAT TAY OK nhung ESP KHONG co goi du lieu ve trong %d s. ==\n", m_espWaitMs / 1000);
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
        // SYN tới 172.16.6.25 cổng 443 (HTTPS) và 80 (HTTP): SYN-ACK hoặc RST về đều chứng minh TCP đi qua được.
        // Thêm DNS qua TCP (96.45.45.45:53): máy DNS chắc chắn trả SYN-ACK -> phân biệt "TCP không qua" với "chính sách riêng".
        struct Dst { quint32 ip; quint16 port; };
        for (const Dst& d : {Dst{0xAC100619u, 443}, Dst{0xAC100619u, 80}, Dst{0x602D2D2Du, 53}})
        {
            const QByteArray syn = buildTcpSyn(m_res.assignedIp, d.ip, static_cast<quint16>(50001 + i + d.port), d.port);
            const QByteArray e1 = c->seal(syn, 4);
            if (!e1.isEmpty()) m_sock->writeDatagram(e1, QHostAddress(m_gw), 4500);
        }
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
        // Xuất khóa PHIÊN TẠM + một gói ESP mẫu để kiểm độc lập bằng OpenSSL/Python (IKE_ESP_DUMP=<tệp>).
        // Khóa này chỉ sống trong phiên thử (hết hạn sau vài phút), không chứa PSK/mật khẩu.
        if (!m_variants.isEmpty() && qEnvironmentVariableIsSet("IKE_ESP_DUMP"))
        {
            const auto ec = cfgFor(Opt{g});
            const QByteArray inner = buildIcmpEcho(m_res.assignedIp, 0xAC100619u, 0x4f41, 1);
            auto* dumpC = new GpEspCrypto();
            QString err2;
            QByteArray sealed;
            if (dumpC->init(ec, &err2)) sealed = dumpC->seal(inner, 4);
            QFile f(qEnvironmentVariable("IKE_ESP_DUMP"));
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
            {
                auto line = [&](const char* k, const QByteArray& v) { f.write(QByteArray(k) + "=" + v.toHex() + "\n"); };
                f.write(QByteArray("spiOut=") + QByteArray::number(m_res.esp.spiOut, 16) + "\n");
                f.write(QByteArray("spiIn=") + QByteArray::number(m_res.esp.spiIn, 16) + "\n");
                f.write(QByteArray("encLen=") + QByteArray::number(m_res.kmEncLen) + "\nhmacLen=" + QByteArray::number(m_res.kmHmacLen) + "\n");
                f.write(QByteArray("prf=") + (m_res.kmHash == IkeCrypto::Hash::Sha256 ? "sha256" : "sha1") + "\n");
                line("skeyid_d", m_res.kmSkeyidD); line("gxy", m_res.kmGxy); line("ni", m_res.kmNi); line("nr", m_res.kmNr);
                line("encKeyOut", ec.encKeyOut); line("hmacKeyOut", ec.hmacKeyOut);
                line("encKeyIn", ec.encKeyIn); line("hmacKeyIn", ec.hmacKeyIn);
                line("inner", inner); line("pkt", sealed);
                line("syn", buildTcpSyn(m_res.assignedIp, 0xAC100619u, 50001, 443));
                f.close();
                std::printf("  [IKE_ESP_DUMP: da ghi %s]\n", qUtf8Printable(f.fileName()));
            }
            delete dumpC;
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
    QUdpSocket* m_sock;      // local 4500 (sau float + ESP)
    QUdpSocket* m_sock500;   // local 500 (Phase 1 ban đầu)
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
    int m_hits = 0;            // số gói ESP trả về mở được
    qint64 m_firstHitMs = 0;
    bool m_waitEsp = false;
    qint64 m_upAt = 0;
    IkeSession::Result m_pendingRes;
public:
    bool m_clean = false; ///< chỉ gửi biến thể khóa ĐÚNG + không gửi SPI giả (tránh làm gateway sập SA)
    int m_espDelayMs = 0; ///< trễ trước khi gửi ESP (chỉ DPD) - để phân biệt DELETE tự phát vs do ESP
    int m_espWaitMs = 75000; ///< thời gian chờ gói ESP về trước khi kết luận im lặng
};

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0); // in ngay (không đệm khối) để đọc được khi chuyển hướng/dừng sớm
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

    c.forceNatT = qEnvironmentVariableIntValue("IKE_FORCE_NATT") != 0;     // IKE_FORCE_NATT=1: NAT-D lech co y
    c.encMsg3 = qEnvironmentVariableIntValue("IKE_ENC_MSG3") != 0;       // IKE_ENC_MSG3=1: msg3 ma hoa nhu FortiClient
    c.initialContact = qEnvironmentVariableIntValue("IKE_INIT_CONTACT") != 0; // IKE_INIT_CONTACT=1: kem Notify INITIAL-CONTACT
    c.cfgFortiClient = qEnvironmentVariableIntValue("IKE_CFG_FCT") != 0; // IKE_CFG_FCT=1: Mode-config xin bo thuoc tinh kieu FortiClient
    c.vidMask = (argc >= 11) ? QString::fromLatin1(argv[10]).toUInt(nullptr, 0) : 0; // arg10: bitmask VID kieu FortiClient (vd 0x1ff)
    const bool clean = (argc >= 9) ? (QString::fromLatin1(argv[8]).toInt() != 0) : false; // arg8: 1=clean (chi khoa dung, khong SPI gia)
    std::printf("== Bat tay IKEv1 THAT toi %s (user %s, PFS=%d, IDci=%s, encap=%d, clean=%d) ==\n", argv[1], argv[2],
                c.pfs ? 1 : 0, c.qmIdAssigned ? "IPcap/32" : "0.0.0.0/0", c.qmEncap, clean ? 1 : 0);
    const int espDelay = (argc >= 10) ? QString::fromLatin1(argv[9]).toInt() : 0; // arg9: tre N giay moi gui ESP (chi DPD truoc)
    Driver d(gw, c);
    d.m_clean = clean;
    d.m_espDelayMs = espDelay * 1000;
    if (argc >= 12) d.m_espWaitMs = QString::fromLatin1(argv[11]).toInt() * 1000; // arg11: giay cho ESP ve (mac dinh 75)
    return app.exec();
}
