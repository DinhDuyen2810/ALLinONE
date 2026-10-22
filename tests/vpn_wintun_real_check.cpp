// KIỂM TRA TAY (KHÔNG phải bộ test tự động - CẦN QUYỀN ADMINISTRATOR, tạo adapter mạng THẬT): xác nhận lớp
// adapter của kết nối VPN trực tiếp (WintunAdapter + NetConfigurator) chạy đúng trên máy thật, CHỈ dùng dải
// địa chỉ thử nghiệm RFC 2544 198.18.0.0/15 (không bao giờ được định tuyến ra Internet) - không đụng tuyến
// mặc định, không đổi DNS:
//   1. tạo adapter "OneForAll VPN Check", gán 198.18.77.1/32, tuyến 198.18.77.0/24 vào adapter, tuyến /32 tới
//      "máy chủ" giả 198.18.99.99 qua đường mạng gốc;
//   2. máy -> đường hầm: gửi UDP tới 198.18.77.2 -> luồng đọc wintun phải nhận đúng gói;
//   3. đường hầm -> máy: bơm gói UDP (198.18.77.2 -> 198.18.77.1:<cổng>) vào adapter -> socket trên máy nhận;
//   4. gỡ tuyến + xóa adapter -> tuyến phải biến mất.
// Kết quả ghi ra tệp (tiến trình nâng quyền không có console để đọc). Chỉ chạy tay:
//   cmake --build build --target vpn_wintun_real_check
//   (PowerShell) Start-Process build\vpn_wintun_real_check.exe -ArgumentList '<tệp kết quả>' -Verb RunAs -Wait

#include "tools/vpn/direct/WintunAdapter.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QMutex>
#include <QNetworkDatagram>
#include <QThread>
#include <QUdpSocket>

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>

namespace
{
QFile* g_out = nullptr;
int g_fail = 0, g_pass = 0;
void line(const QString& s)
{
    if (g_out)
    {
        g_out->write((s + "\n").toUtf8());
        g_out->flush();
    }
}
#define CHECK(cond)                                                                            \
    do {                                                                                       \
        if (cond) { ++g_pass; line(QStringLiteral("OK   ") + #cond); }                         \
        else { ++g_fail; line(QStringLiteral("FAIL line %1: %2").arg(__LINE__).arg(#cond)); } \
    } while (0)

quint64 bestRouteLuid(quint32 hostOrderIp)
{
    SOCKADDR_INET dest{};
    dest.Ipv4.sin_family = AF_INET;
    dest.Ipv4.sin_addr.s_addr = htonl(hostOrderIp);
    MIB_IPFORWARD_ROW2 row;
    SOCKADDR_INET src;
    if (GetBestRoute2(nullptr, 0, nullptr, &dest, 0, &row, &src) != NO_ERROR)
        return 0;
    return row.InterfaceLuid.Value;
}

quint16 ipChecksum(const QByteArray& hdr)
{
    quint32 sum = 0;
    for (int i = 0; i + 1 < hdr.size(); i += 2)
        sum += (static_cast<quint8>(hdr[i]) << 8) | static_cast<quint8>(hdr[i + 1]);
    while (sum >> 16)
        sum = (sum & 0xFFFF) + (sum >> 16);
    return static_cast<quint16>(~sum);
}

/// Gói IPv4/UDP hoàn chỉnh (checksum IP đúng, checksum UDP = 0 = không dùng - hợp lệ với IPv4).
QByteArray udpPacket(quint32 src, quint32 dst, quint16 sport, quint16 dport, const QByteArray& payload)
{
    QByteArray ip(20, '\0');
    const int total = 20 + 8 + payload.size();
    ip[0] = 0x45;
    ip[2] = static_cast<char>(total >> 8);
    ip[3] = static_cast<char>(total & 0xFF);
    ip[8] = 64;
    ip[9] = 17;
    for (int i = 0; i < 4; ++i)
    {
        ip[12 + i] = static_cast<char>(src >> (24 - 8 * i));
        ip[16 + i] = static_cast<char>(dst >> (24 - 8 * i));
    }
    const quint16 c = ipChecksum(ip);
    ip[10] = static_cast<char>(c >> 8);
    ip[11] = static_cast<char>(c & 0xFF);
    QByteArray udp(8, '\0');
    const int ulen = 8 + payload.size();
    udp[0] = static_cast<char>(sport >> 8);
    udp[1] = static_cast<char>(sport & 0xFF);
    udp[2] = static_cast<char>(dport >> 8);
    udp[3] = static_cast<char>(dport & 0xFF);
    udp[4] = static_cast<char>(ulen >> 8);
    udp[5] = static_cast<char>(ulen & 0xFF);
    return ip + udp + payload;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QFile out(argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("vpn_wintun_real_check.txt"));
    out.open(QIODevice::WriteOnly | QIODevice::Truncate);
    g_out = &out;

    const quint32 local = 0xC6124D01;  // 198.18.77.1
    const quint32 peer = 0xC6124D02;   // 198.18.77.2
    const quint32 server = 0xC6126363; // 198.18.99.99 ("máy chủ VPN" giả)

    line(QStringLiteral("wintun.dll: %1 (co: %2)").arg(WintunAdapter::dllPath()).arg(WintunAdapter::isAvailable()));
    const quint64 underlayBefore = bestRouteLuid(server);

    WintunAdapter adapter;
    QString err;
    const bool opened = adapter.open(QStringLiteral("OneForAll VPN Check"), &err);
    CHECK(opened);
    if (!opened)
    {
        line(QStringLiteral("Loi mo adapter: %1").arg(err));
        line(QStringLiteral("passed=%1 failed=%2").arg(g_pass).arg(g_fail));
        return 1;
    }
    line(QStringLiteral("adapter LUID = %1").arg(adapter.luid()));

    NetConfigurator net;
    CHECK(net.captureUnderlay(server, &err));
    TunnelConfig cfg;
    cfg.ipv4 = QStringLiteral("198.18.77.1");
    cfg.includeRoutes << IpRoute{0xC6124D00, 24};
    const bool applied = net.apply(adapter.luid(), cfg, server, 1400, &err);
    CHECK(applied);
    if (!applied)
        line(QStringLiteral("Loi cau hinh: %1").arg(err));
    QThread::msleep(1500); // địa chỉ mới cần chút thời gian để sẵn sàng (DAD)

    CHECK(bestRouteLuid(0xC6124D05) == adapter.luid());   // 198.18.77.5 đi vào adapter
    CHECK(bestRouteLuid(server) == underlayBefore);         // "máy chủ" vẫn đi đường gốc (tuyến /32)

    // 2) máy -> đường hầm
    QMutex mu;
    QList<QByteArray> fromOs;
    adapter.startReader([&](QByteArray ip) {
        QMutexLocker lock(&mu);
        fromOs << ip;
    });
    QUdpSocket sender;
    sender.writeDatagram(QByteArray("ping-from-os"), QHostAddress(QStringLiteral("198.18.77.2")), 40001);
    bool gotOut = false;
    QElapsedTimer t;
    t.start();
    while (!gotOut && t.elapsed() < 5000)
    {
        QThread::msleep(50);
        QMutexLocker lock(&mu);
        for (const QByteArray& p : fromOs)
            if (p.size() >= 28 && static_cast<quint8>(p[9]) == 17 && p.endsWith("ping-from-os"))
                gotOut = true;
    }
    CHECK(gotOut);

    // 3) đường hầm -> máy
    QUdpSocket receiver;
    const bool bound = receiver.bind(QHostAddress(QStringLiteral("198.18.77.1")), 40002);
    CHECK(bound);
    CHECK(adapter.sendPacket(udpPacket(peer, local, 40003, 40002, "pong-from-tunnel")));
    bool gotIn = false;
    t.restart();
    while (!gotIn && t.elapsed() < 5000)
    {
        if (receiver.waitForReadyRead(200))
            while (receiver.hasPendingDatagrams())
                if (receiver.receiveDatagram().data() == "pong-from-tunnel")
                    gotIn = true;
    }
    CHECK(gotIn);

    // 4) dọn dẹp: gỡ tuyến + xóa adapter -> 198.18.77.x KHÔNG còn đi vào adapter cũ.
    const quint64 adapterLuid = adapter.luid();
    receiver.close();
    net.revert();
    adapter.close();
    QThread::msleep(500);
    const quint64 afterLuid = bestRouteLuid(0xC6124D05);
    CHECK(afterLuid != adapterLuid);
    line(QStringLiteral("tuyen 198.18.77.5 sau khi go: LUID %1 (adapter da xoa la %2)").arg(afterLuid).arg(adapterLuid));

    line(QStringLiteral("passed=%1 failed=%2").arg(g_pass).arg(g_fail));
    return g_fail == 0 ? 0 : 1;
}
