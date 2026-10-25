// CÔNG CỤ KIỂM TRA TAY đầu-cuối với ADAPTER THẬT (cần bấm Yes ở hộp thoại UAC): chạy ĐÚNG DirectVpnSession mà giao
// diện dùng - khởi chạy tiến trình phụ nâng quyền (chính tệp này với `--vpn-helper`, như OneForAll.exe), tạo adapter
// wintun + định tuyến + DNS - rồi để CHÍNH HỆ ĐIỀU HÀNH kiểm tra đường hầm: `ping` và `curl` (HTTPS) tới máy nội bộ,
// xem tuyến tới máy đó đi qua adapter VPN, sau đó ngắt và kiểm adapter/tuyến đã được dọn sạch.
// Bí mật truyền qua tham số dòng lệnh, KHÔNG ghi tệp; nhật ký không in mật khẩu/PSK.
//   directvpn_real_e2e ipsec <gateway> <user> <pass> <psk> [dichNoiBo=172.16.6.25]
// Mã thoát: 0 = ping có trả lời + HTTPS có phản hồi + dọn sạch; khác 0 = có bước thất bại (xem nhật ký).
#include "TestDataDir.h"

#include "tools/vpn/direct/DirectVpnSession.h"
#include "tools/vpn/direct/VpnHelperMain.h"

#include <QCoreApplication>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <cstdio>
#include <cstring>

namespace
{
/// Chạy một lệnh, trả về (mã thoát, stdout+stderr đã gộp). Lệnh hệ thống gọi bằng đường dẫn tuyệt đối.
QPair<int, QString> run(const QString& program, const QStringList& args, int timeoutMs)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForFinished(timeoutMs))
    {
        p.kill();
        p.waitForFinished(2000);
        return {-1, QStringLiteral("(het gio)")};
    }
    return {p.exitCode(), QString::fromLocal8Bit(p.readAll()).trimmed()};
}

QString powershell(const QString& script, int timeoutMs = 20000)
{
    return run(QStringLiteral("C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"),
               {QStringLiteral("-NoProfile"), QStringLiteral("-Command"), script}, timeoutMs).second;
}
} // namespace

int main(int argc, char** argv)
{
    // Tiến trình phụ nâng quyền (do DirectVpnSession khởi chạy qua UAC) - y như src/main.cpp.
    if (argc >= 4 && std::strcmp(argv[1], "--vpn-helper") == 0)
        return runVpnHelper(argc, argv);

    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QCoreApplication app(argc, argv);
    TestDataDir dataDir; // nhật ký/ghim chứng chỉ của lần chạy này vào thư mục tạm, không đụng dữ liệu người dùng
    if (argc < 6 || std::strcmp(argv[1], "ipsec") != 0)
    {
        std::printf("Dung: directvpn_real_e2e ipsec <gateway> <user> <pass> <psk> [dichNoiBo=172.16.6.25]\n");
        return 1;
    }
    const QString target = argc >= 7 ? QString::fromLatin1(argv[6]) : QStringLiteral("172.16.6.25");

    DirectVpnSession::Request req;
    req.protocol = DirectProtocol::FortinetIpsec;
    req.server = QString::fromLocal8Bit(argv[2]);
    req.username = QString::fromLocal8Bit(argv[3]);
    req.password = QString::fromLocal8Bit(argv[4]);
    req.psk = QString::fromLocal8Bit(argv[5]);

    DirectVpnSession session;
    int exitCode = 4;
    bool pingOk = false, httpsOk = false, routeOk = false;

    QObject::connect(&session, &DirectVpnSession::statusChanged,
                     [](const QString& t) { std::printf("  [trang thai] %s\n", t.toUtf8().constData()); });
    QObject::connect(&session, &DirectVpnSession::connected, [&](const QString& summary) {
        std::printf("\n== DirectVpnSession bao DA KET NOI (adapter that): %s ==\n", summary.toUtf8().constData());
        // 1) Tuyến tới máy nội bộ phải đi qua adapter VPN.
        const QString alias = powershell(QStringLiteral("(Find-NetRoute -RemoteIPAddress '%1' | Select-Object -First 1).InterfaceAlias").arg(target));
        routeOk = alias.contains(QStringLiteral("OneForAll"));
        std::printf("  tuyen toi %s di qua adapter: '%s' -> %s\n", target.toUtf8().constData(), alias.toUtf8().constData(), routeOk ? "DUNG" : "SAI");
        // 2) ping bằng chính HĐH.
        const auto ping = run(QStringLiteral("C:/Windows/System32/PING.EXE"), {QStringLiteral("-n"), QStringLiteral("4"), QStringLiteral("-w"), QStringLiteral("3000"), target}, 30000);
        // Chỉ tính trả lời THẬT từ máy đích ("Reply from <đích>: bytes="), không tính "Destination host unreachable" của router.
        int replies = 0;
        auto it = QRegularExpression(QStringLiteral("Reply from %1: bytes=").arg(QRegularExpression::escape(target))).globalMatch(ping.second);
        while (it.hasNext())
        {
            it.next();
            ++replies;
        }
        pingOk = replies > 0;
        std::printf("  ping %s: %s (%d/4 tra loi)\n", target.toUtf8().constData(), pingOk ? "CO TRA LOI" : "KHONG TRA LOI", replies);
        // 3) HTTPS bằng chính HĐH (curl.exe có sẵn từ Windows 10 1803).
        const auto curl = run(QStringLiteral("C:/Windows/System32/curl.exe"),
                              {QStringLiteral("-k"), QStringLiteral("-s"), QStringLiteral("-o"), QStringLiteral("NUL"), QStringLiteral("-w"),
                               QStringLiteral("%{http_code}"), QStringLiteral("--max-time"), QStringLiteral("20"), QStringLiteral("https://%1/").arg(target)}, 30000);
        const QString code = curl.second.right(3);
        httpsOk = curl.first == 0 && code != QStringLiteral("000") && QRegularExpression(QStringLiteral("^\\d{3}$")).match(code).hasMatch();
        std::printf("  HTTPS https://%s/ : ma curl=%d, HTTP status=%s -> %s\n", target.toUtf8().constData(), curl.first, code.toUtf8().constData(), httpsOk ? "CO PHAN HOI" : "KHONG PHAN HOI");
        std::printf("  Dang ngat ket noi...\n");
        session.disconnectVpn();
    });
    QObject::connect(&session, &DirectVpnSession::ended, [&](bool wasConnected, const QString& message) {
        std::printf("  [ket thuc] daKetNoi=%d: %s\n", wasConnected ? 1 : 0, message.toUtf8().constData());
        // Chờ tiến trình phụ dọn xong rồi kiểm adapter/tuyến đã sạch.
        QTimer::singleShot(4000, &app, [&, wasConnected]() {
            const QString left = powershell(QStringLiteral("(Get-NetAdapter -Name 'OneForAll VPN' -ErrorAction SilentlyContinue | Measure-Object).Count"));
            const QString alias = powershell(QStringLiteral("(Find-NetRoute -RemoteIPAddress '%1' | Select-Object -First 1).InterfaceAlias").arg(target));
            const bool clean = left.trimmed() == QStringLiteral("0") && !alias.contains(QStringLiteral("OneForAll"));
            std::printf("  don dep: so adapter 'OneForAll VPN' con lai=%s, tuyen toi %s nay di qua '%s' -> %s\n", left.toUtf8().constData(),
                        target.toUtf8().constData(), alias.toUtf8().constData(), clean ? "SACH" : "CON SOT");
            std::printf("\n== KET QUA: ket noi=%s | tuyen qua adapter=%s | ping=%s | HTTPS=%s | don dep=%s ==\n", wasConnected ? "CO" : "KHONG",
                        routeOk ? "CO" : "KHONG", pingOk ? "CO" : "KHONG", httpsOk ? "CO" : "KHONG", clean ? "CO" : "KHONG");
            exitCode = (wasConnected && routeOk && pingOk && httpsOk && clean) ? 0 : 4;
            QCoreApplication::exit(exitCode);
        });
    });

    std::printf("== DirectVpnSession THAT -> %s (IPsec/IKEv1, user %s) - BAM YES o hop thoai UAC ==\n", argv[2], argv[3]);
    QTimer::singleShot(0, &app, [&]() { session.connectTo(req); });
    // Quá hạn tổng: nếu chưa kết nối được thì ngắt.
    QTimer::singleShot(180000, &app, [&]() {
        if (!session.isConnected())
        {
            std::printf("\n== QUA HAN 180s - chua ket noi duoc ==\n");
            session.disconnectVpn();
            QTimer::singleShot(5000, &app, []() { QCoreApplication::exit(5); });
        }
    });
    const int rc = app.exec();
    session.shutdown(8000);
    return rc;
}
