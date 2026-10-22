// KIỂM TRA TAY (KHÔNG phải bộ test tự động): kết nối VPN THẬT tới máy chủ công cộng VPN Gate qua SSTP bằng
// ĐÚNG chuỗi code của tab "Đổi IP theo quốc gia" (VpnGateClient -> VpnGateServer::sstpHost -> VpnController::
// addConnection -> VpnConnector/RasDial -> PublicIpChecker), đo IP công khai trước/sau rồi NGẮT và XÓA hồ sơ
// tạm. Không nằm trong bất kỳ bộ *_tests nào vì quy tắc dự án cấm test tự động kết nối VPN thật - chỉ chạy
// tay khi cần xác nhận tính năng trên máy thật: cmake --build build --target vpn_gate_sstp_real_check
//
// Không cần quyền Administrator (hồ sơ VPN theo người dùng). Thử lần lượt tối đa N máy chủ tốt nhất, dừng ở
// máy chủ đầu tiên kết nối được.

#include "tools/vpn/engine/PublicIpChecker.h"
#include "tools/vpn/engine/VpnConnector.h"
#include "tools/vpn/engine/VpnController.h"
#include "tools/vpn/engine/VpnGateClient.h"
#include "tools/vpn/model/VpnProfile.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QTimer>

#include <cstdio>

namespace
{
const QString kProfile = QStringLiteral("OneForAll-VPNGate-RealCheck");

// Chạy event loop tới khi 'done' được gọi hoặc hết giờ.
template <typename Setup> bool waitFor(int timeoutMs, Setup setup)
{
    QEventLoop loop;
    bool finished = false;
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    setup([&]() { finished = true; loop.quit(); });
    loop.exec();
    return finished;
}

// Dùng chung MỘT checker cho cả trước/sau khi nối (đúng như tab "Đổi IP theo quốc gia") - để tái hiện việc kết
// nối HTTPS cũ được giữ lại (keep-alive) bị kẹt sau khi tuyến mạng đổi.
PublicIpChecker* g_checker = nullptr;

QString publicIp(QString* place = nullptr, qint64* elapsedMs = nullptr)
{
    PublicIpChecker& checker = *g_checker;
    QElapsedTimer timer;
    timer.start();
    QString ip;
    waitFor(25000, [&](auto done) {
        QObject::connect(&checker, &PublicIpChecker::result, [&, done](PublicIpInfo info) {
            ip = info.ip;
            if (place)
                *place = info.city.isEmpty() ? info.country : info.city + ", " + info.country;
            done();
        });
        QObject::connect(&checker, &PublicIpChecker::errorOccurred, [&, done](QString msg) {
            ip = "ERR " + msg;
            done();
        });
        checker.check();
    });
    QObject::disconnect(&checker, nullptr, nullptr, nullptr);
    if (elapsedMs)
        *elapsedMs = timer.elapsed();
    return ip;
}

bool runConnector(VpnConnector& c, QString* message)
{
    bool ok = false;
    waitFor(60000, [&](auto done) {
        QObject::connect(&c, &VpnConnector::operationFinished, [&, done](bool success, QString msg) {
            ok = success;
            *message = msg;
            done();
        });
        c.start();
    });
    c.wait();
    return ok;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    PublicIpChecker sharedChecker;
    g_checker = &sharedChecker;
    const int maxTries = argc > 1 ? QString(argv[1]).toInt() : 5;

    QList<VpnGateServer> servers;
    VpnGateClient client;
    waitFor(60000, [&](auto done) {
        QObject::connect(&client, &VpnGateClient::finished, [&, done](QList<VpnGateServer> list, QString err) {
            servers = list;
            if (!err.isEmpty())
                std::printf("Loi tai danh sach: %s\n", qPrintable(err));
            done();
        });
        client.fetch();
    });
    std::printf("Tai duoc %d may chu VPN Gate\n", static_cast<int>(servers.size()));
    if (servers.isEmpty())
        return 2;

    const QString ipBefore = publicIp();
    std::printf("IP truoc khi noi: %s\n", qPrintable(ipBefore));

    int tried = 0;
    for (const VpnGateServer& s : servers)
    {
        if (tried >= maxTries)
            break;
        const QString host = s.sstpHost();
        if (host.isEmpty())
            continue;
        ++tried;
        std::printf("\n[%d] %s (%s) -> SSTP %s\n", tried, qPrintable(s.countryLong), qPrintable(s.ip), qPrintable(host));

        VpnProfile p;
        p.name = kProfile;
        p.serverAddress = host;
        p.tunnelType = VpnTunnelType::Sstp;
        p.username = "vpn";
        QString err;
        if (!VpnController::addConnection(p, &err))
        {
            std::printf("  addConnection THAT BAI: %s\n", qPrintable(err));
            continue;
        }

        VpnConnector conn;
        conn.setConnectTarget(kProfile, "vpn", "vpn");
        QString msg;
        const bool ok = runConnector(conn, &msg);
        std::printf("  ket noi: %s %s\n", ok ? "THANH CONG" : "THAT BAI", qPrintable(msg));
        if (ok)
        {
            QString place;
            qint64 ms = 0;
            const QString ipVpn = publicIp(&place, &ms);
            std::printf("  IP qua VPN: %s (%s) - mat %lld ms\n", qPrintable(ipVpn), qPrintable(place), static_cast<long long>(ms));
            std::printf("  => IP %s\n", (ipVpn != ipBefore && !ipVpn.startsWith("ERR")) ? "DA DOI (DAT)" : "KHONG DOI");

            VpnConnector disc;
            disc.setDisconnectTarget(kProfile);
            QString dmsg;
            std::printf("  ngat: %s %s\n", runConnector(disc, &dmsg) ? "OK" : "LOI", qPrintable(dmsg));
        }
        VpnController::removeConnection(kProfile, nullptr);
        if (ok)
        {
            std::printf("IP sau khi ngat: %s\n", qPrintable(publicIp()));
            return 0;
        }
    }
    std::printf("\nKhong may chu nao trong %d may chu dau ket noi duoc qua SSTP.\n", tried);
    return 1;
}
