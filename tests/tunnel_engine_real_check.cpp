// CÔNG CỤ KIỂM TRA TAY: chạy CHÍNH TunnelEngine của ứng dụng (đường FortiGate IPsec/IKEv1) với PSK + tài khoản
// THẬT ở chế độ noAdapterForTest - KHÔNG tạo adapter wintun, KHÔNG đổi tuyến (nên không cần quyền Admin) - rồi bơm
// gói IP tổng hợp (ICMP echo, truy vấn DNS, TCP SYN) vào "adapter giả" và đọc gói trả về qua tín hiệu test. Chứng
// minh đường dữ liệu IKE+ESP của chính ứng dụng (cùng mã với tiến trình helper) đi được hai chiều qua gateway thật.
// Bí mật truyền qua tham số dòng lệnh, KHÔNG ghi ra tệp; nhật ký của TunnelEngine/IkeSession không in mật khẩu/PSK.
//   tunnel_engine_real_check <gateway> <user> <pass> <psk> [giay=25] [dichNoiBo=172.16.6.25] [dns=96.45.45.45]
#include "tools/vpn/direct/DirectVpnTypes.h"
#include "tools/vpn/direct/TunnelEngine.h"

#include "RawPackets.h"

#include <QCoreApplication>
#include <QHash>
#include <QRegularExpression>
#include <QTimer>
#include <cstdio>

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);
    if (argc < 5)
    {
        std::printf("Dung: tunnel_engine_real_check <gateway> <user> <pass> <psk> [giay=25] [dichNoiBo=172.16.6.25] [dns=96.45.45.45]\n");
        return 1;
    }
    const int seconds = argc >= 6 ? QString::fromLatin1(argv[5]).toInt() : 25;
    quint32 target = 0xAC100619u, dns = 0x602D2D2Du; // 172.16.6.25, 96.45.45.45
    if (argc >= 7 && !DirectVpn::parseIpv4(QString::fromLatin1(argv[6]), &target)) { std::printf("IP dich sai\n"); return 1; }
    if (argc >= 8 && !DirectVpn::parseIpv4(QString::fromLatin1(argv[7]), &dns)) { std::printf("IP DNS sai\n"); return 1; }

    TunnelEngine engine;
    TunnelEngine::Job job;
    job.protocol = DirectProtocol::FortinetIpsec;
    job.server = QString::fromLocal8Bit(argv[1]);
    job.username = QString::fromLocal8Bit(argv[2]);
    job.password = QString::fromLocal8Bit(argv[3]);
    job.psk = QString::fromLocal8Bit(argv[4]);
    job.noAdapterForTest = true;

    quint32 assigned = 0;
    int round = 0;
    bool gotIcmp = false, gotDns = false, gotSynAck = false;
    int replies = 0;
    QTimer sender, deadline;
    sender.setInterval(1000);
    deadline.setSingleShot(true);

    QObject::connect(&engine, &TunnelEngine::logMessage, [](const QString& m) { std::printf("  [engine] %s\n", m.toUtf8().constData()); });
    QObject::connect(&engine, &TunnelEngine::status, [](const QString& m) { std::printf("  [trang thai] %s\n", m.toUtf8().constData()); });
    QObject::connect(&engine, &TunnelEngine::connected, [&](const QString& summary) {
        std::printf("\n== TunnelEngine bao DA KET NOI: %s ==\n", summary.toUtf8().constData());
        const auto m = QRegularExpression(QStringLiteral("IP (\\d+\\.\\d+\\.\\d+\\.\\d+)")).match(summary);
        if (!m.hasMatch() || !DirectVpn::parseIpv4(m.captured(1), &assigned))
        {
            std::printf("Khong doc duoc IP duoc cap tu tom tat.\n");
            QCoreApplication::exit(3);
            return;
        }
        sender.start();
        deadline.start(seconds * 1000);
    });
    QObject::connect(&sender, &QTimer::timeout, [&]() {
        ++round;
        // ICMP + DNS (UDP) + TCP SYN (HTTPS) tới máy nội bộ - đi vào đường hầm như gói từ adapter thật.
        engine.injectFromAdapterForTest(buildIcmpEcho(assigned, target, 0x4f41, static_cast<quint16>(round)));
        engine.injectFromAdapterForTest(buildDnsQuery(assigned, dns));
        engine.injectFromAdapterForTest(buildTcpSyn(assigned, target, static_cast<quint16>(50000 + round), 443));
    });
    QObject::connect(&engine, &TunnelEngine::packetToAdapterForTest, [&](const QByteArray& ip) {
        ++replies;
        const QString d = describeIp(ip);
        std::printf("     goi tra ve qua TunnelEngine: %s\n", d.toUtf8().constData());
        if (d.contains("ICMP type=0")) gotIcmp = true;
        if (d.contains("DNS tra loi") && !d.contains(" 0 ban ghi")) gotDns = true;
        if (d.contains("SYN-ACK")) gotSynAck = true;
    });
    QObject::connect(&deadline, &QTimer::timeout, [&]() {
        sender.stop();
        std::printf("\n== KET QUA (chinh TunnelEngine cua ung dung, noAdapterForTest) ==\n");
        std::printf("  goi tra ve: %d | ICMP echo reply: %s | DNS tra loi: %s | TCP SYN-ACK (HTTPS): %s\n", replies,
                    gotIcmp ? "CO" : "KHONG", gotDns ? "CO" : "KHONG", gotSynAck ? "CO" : "KHONG");
        engine.stop(QStringLiteral("kiem tra xong"));
    });
    QObject::connect(&engine, &TunnelEngine::finished, [&](bool wasConnected, const QString& message) {
        std::printf("  [ket thuc] daKetNoi=%d: %s\n", wasConnected ? 1 : 0, message.toUtf8().constData());
        QCoreApplication::exit(wasConnected && gotIcmp && gotDns && gotSynAck ? 0 : 4);
    });
    QTimer::singleShot(60000, &app, [&]() {
        if (assigned == 0)
        {
            std::printf("\n== QUA HAN 60s - TunnelEngine chua ket noi duoc ==\n");
            engine.stop(QStringLiteral("het gio"));
            QTimer::singleShot(3000, &app, []() { QCoreApplication::exit(5); });
        }
    });

    std::printf("== TunnelEngine THAT -> %s (user %s, IPsec/IKEv1, khong adapter) ==\n", argv[1], argv[2]);
    engine.start(job);
    return app.exec();
}
