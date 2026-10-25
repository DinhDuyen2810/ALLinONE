// Hàm dựng/mô tả gói IPv4 thô (ICMP echo, TCP SYN, truy vấn DNS) dùng chung cho các công cụ KIỂM TRA TAY
// (ike_real_connect, tunnel_engine_real_check). Gói tổng hợp đủ tùy chọn TCP như SYN của Windows.
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// Tổng kiểm 16-bit một bù (IP/ICMP).
inline quint16 inetChecksum(const QByteArray& b)
{
    quint32 sum = 0;
    for (int i = 0; i + 1 < b.size(); i += 2) sum += (static_cast<quint8>(b[i]) << 8) | static_cast<quint8>(b[i + 1]);
    if (b.size() & 1) sum += static_cast<quint8>(b[b.size() - 1]) << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<quint16>(~sum & 0xFFFF);
}
inline void put16(QByteArray& b, quint16 v) { b.append(char(v >> 8)); b.append(char(v & 0xFF)); }
inline void put32(QByteArray& b, quint32 v) { b.append(char(v >> 24)); b.append(char(v >> 16)); b.append(char(v >> 8)); b.append(char(v)); }

// Mô tả ngắn một gói IPv4 đã giải mã (ICMP/UDP-DNS/TCP) để có bằng chứng lưu lượng thật qua đường hầm.
inline QString describeIp(const QByteArray& ip)
{
    if (ip.size() < 20 || (static_cast<quint8>(ip[0]) >> 4) != 4) return QStringLiteral("(khong phai IPv4)");
    auto u8 = [&](int i) { return static_cast<quint8>(ip[i]); };
    auto be16 = [&](int i) { return (u8(i) << 8) | u8(i + 1); };
    auto addr = [&](int i) { return QString("%1.%2.%3.%4").arg(u8(i)).arg(u8(i + 1)).arg(u8(i + 2)).arg(u8(i + 3)); };
    const int ihl = (u8(0) & 15) * 4, proto = u8(9);
    QString s = addr(12) + " -> " + addr(16) + " ";
    if (proto == 1 && ip.size() >= ihl + 8)
        return s + QString("ICMP type=%1 (%2)").arg(u8(ihl)).arg(u8(ihl) == 0 ? "echo reply" : "khac");
    if (proto == 6 && ip.size() >= ihl + 14)
    {
        const int fl = u8(ihl + 13);
        return s + QString("TCP %1->%2 flags=0x%3%4").arg(be16(ihl)).arg(be16(ihl + 2)).arg(fl, 2, 16, QChar('0'))
                       .arg((fl & 0x12) == 0x12 ? " (SYN-ACK: may chu dang lang nghe)" : "");
    }
    if (proto == 17 && ip.size() >= ihl + 12)
    {
        s += QString("UDP %1->%2").arg(be16(ihl)).arg(be16(ihl + 2));
        if (be16(ihl) == 53 && ip.size() >= ihl + 8 + 12)
        {
            const int d = ihl + 8;
            const int an = be16(d + 6);
            int p = d + 12;
            while (p < ip.size() && u8(p) != 0) p += u8(p) + 1; // qname
            p += 1 + 4;                                         // 0 + qtype + qclass
            QStringList ips;
            for (int i = 0; i < an && p + 12 <= ip.size(); ++i)
            {
                p += ((u8(p) & 0xC0) == 0xC0) ? 2 : 0;
                const int type = be16(p), rdl = be16(p + 8);
                if (type == 1 && rdl == 4 && p + 14 <= ip.size()) ips << addr(p + 10);
                p += 10 + rdl;
            }
            s += QString(" DNS tra loi %1 ban ghi: %2").arg(an).arg(ips.join(','));
        }
        return s;
    }
    return s + QString("proto=%1").arg(proto);
}

// Gói IPv4 + ICMP echo request (src->dst).
inline QByteArray buildIcmpEcho(quint32 src, quint32 dst, quint16 id, quint16 seq)
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
inline QByteArray buildTcpSyn(quint32 src, quint32 dst, quint16 sport, quint16 dport)
{
    QByteArray tcp;
    put16(tcp, sport); put16(tcp, dport);
    put32(tcp, 0x12345678);           // seq
    put32(tcp, 0);                     // ack
    tcp.append(char(0x80)); tcp.append(char(0x02)); // data offset=8 (có tùy chọn), flags=SYN
    put16(tcp, 64240);                 // window (như Windows)
    put16(tcp, 0); put16(tcp, 0);      // checksum (sau), urgent
    // Tùy chọn như SYN của Windows: MSS 1360, NOP+WS(8), NOP+NOP+SACK-permitted.
    tcp.append("\x02\x04\x05\x50\x01\x03\x03\x08\x01\x01\x04\x02", 12);
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
inline QByteArray buildDnsQuery(quint32 src, quint32 dst)
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
