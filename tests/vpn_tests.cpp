// Kiểm thử lõi VPN & Location. KHÔNG cần kết nối VPN thật hay quyền Administrator - chỉ kiểm thử lõi
// THUẦN (phân tích output Get-VpnConnection mẫu dựng sẵn, dựng script PowerShell) tách riêng khỏi phần
// gọi PowerShell/RAS API thật, giống mẫu đã dùng cho PartitionManager/AdbController.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include "TestDataDir.h"
#include <cstdio>

#include "core/AppPaths.h"
#include "core/PowerShellRunner.h"
#include "tools/vpn/engine/DirectProfileStore.h"
#include "tools/vpn/engine/PublicIpChecker.h"
#include "tools/vpn/engine/PublicIpv6Checker.h"
#include "tools/vpn/engine/Ipv6LeakGuard.h"
#include "tools/vpn/engine/Ipv6GuardHelper.h"
#include "tools/vpn/engine/Ipv6GuardSession.h"
#include "core/WinElevation.h"
#include "tools/vpn/engine/VpnConnector.h"
#include "tools/vpn/engine/VpnController.h"
#include "tools/vpn/engine/VpnGateCache.h"
#include "tools/vpn/engine/VpnGateClient.h"
#include "tools/vpn/engine/ConnectivityTester.h"
#include "tools/vpn/engine/ThirdPartyVpnScanner.h"
#include "tools/vpn/engine/WireGuardController.h"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkProxyFactory>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // Mọi tệp dữ liệu (vpn_profiles.json) ghi vào thư mục tạm - KHÔNG đụng dữ liệu thật của người dùng.
    TestDataDir dataDir;
    AppPaths::setDataDirOverride(dataDir.path());

    // ---- VpnController::internal::parseConnectionsJson: dữ liệu mẫu dựng sẵn (không gọi PowerShell) ----
    {
        // Mảng JSON bình thường (>= 2 kết nối)
        const QByteArray arrayJson = R"([
            {"Name":"Ha Lan - Mullvad","ServerAddress":"nl123.mullvad.net","TunnelType":"Ikev2","ConnectionStatus":"Connected"},
            {"Name":"My - NordVPN","ServerAddress":"us456.nordvpn.com","TunnelType":"Automatic","ConnectionStatus":"Disconnected"}
        ])";
        QString err;
        const auto list = VpnController::internal::parseConnectionsJson(arrayJson, &err);
        CHECK(err.isEmpty());
        CHECK(list.size() == 2);
        if (list.size() == 2)
        {
            CHECK(list[0].name == "Ha Lan - Mullvad");
            CHECK(list[0].serverAddress == "nl123.mullvad.net");
            CHECK(list[0].tunnelType == "Ikev2");
            CHECK(list[0].isConnected());
            CHECK(!list[1].isConnected());
        }

        // PowerShell 5.1: CHỈ 1 kết nối -> ConvertTo-Json trả về 1 OBJECT đơn, không bọc mảng - đúng
        // lỗi thật đã gặp khi xây PartitionManager, chủ động test lại ở đây.
        const QByteArray singleObjectJson = R"({"Name":"Nhat - ProtonVPN","ServerAddress":"jp1.protonvpn.net","TunnelType":"Sstp","ConnectionStatus":"Disconnected"})";
        QString err2;
        const auto list2 = VpnController::internal::parseConnectionsJson(singleObjectJson, &err2);
        CHECK(err2.isEmpty());
        CHECK(list2.size() == 1);
        if (list2.size() == 1)
            CHECK(list2[0].name == "Nhat - ProtonVPN");

        // Rỗng -> không có kết nối nào, không phải lỗi
        QString err3;
        CHECK(VpnController::internal::parseConnectionsJson("", &err3).isEmpty());
        CHECK(err3.isEmpty());

        // JSON hỏng -> báo lỗi rõ ràng, không crash
        QString err4;
        CHECK(VpnController::internal::parseConnectionsJson("{khong phai json hop le", &err4).isEmpty());
        CHECK(!err4.isEmpty());
    }

    // ---- VpnController::internal::escapePsString: thoát dấu nháy đơn chuẩn PowerShell ----
    {
        CHECK(VpnController::internal::escapePsString("Ha Lan") == "Ha Lan");
        CHECK(VpnController::internal::escapePsString("O'Brien's VPN") == "O''Brien''s VPN");
    }

    // ---- Dựng script PowerShell: chỉ kiểm tra NỘI DUNG CHUỖI (thuần, không gọi powershell.exe) ----
    {
        CHECK(VpnController::internal::buildListConnectionsScript().contains("Get-VpnConnection"));

        VpnProfile p;
        p.name = "Ha Lan - Test";
        p.serverAddress = "nl1.example.com";
        p.tunnelType = VpnTunnelType::Ikev2;
        const QString addScript = VpnController::internal::buildAddConnectionScript(p);
        CHECK(addScript.contains("Add-VpnConnection"));
        CHECK(addScript.contains("-Name 'Ha Lan - Test'"));
        CHECK(addScript.contains("-ServerAddress 'nl1.example.com'"));
        CHECK(addScript.contains("-TunnelType Ikev2"));
        CHECK(!addScript.contains("-AllUserConnection")); // mặc định per-user, KHÔNG cần quyền Administrator

        // Tên có dấu nháy đơn - phải được thoát đúng để không làm hỏng cú pháp script
        VpnProfile p2;
        p2.name = "Viet's VPN";
        p2.serverAddress = "vn1.example.com";
        const QString addScript2 = VpnController::internal::buildAddConnectionScript(p2);
        CHECK(addScript2.contains("-Name 'Viet''s VPN'"));

        const QString removeScript = VpnController::internal::buildRemoveConnectionScript("Ha Lan - Test");
        CHECK(removeScript.contains("Remove-VpnConnection"));
        CHECK(removeScript.contains("-Name 'Ha Lan - Test'"));
    }

    // ---- Mọi loại VpnTunnelType đều dựng ra script hợp lệ (không rỗng/crash) cho cả 5 giá trị ----
    {
        for (VpnTunnelType t : {VpnTunnelType::Automatic, VpnTunnelType::Ikev2, VpnTunnelType::L2tp,
                                VpnTunnelType::Sstp, VpnTunnelType::Pptp})
        {
            VpnProfile p;
            p.name = "Test";
            p.serverAddress = "1.2.3.4";
            p.tunnelType = t;
            const QString script = VpnController::internal::buildAddConnectionScript(p);
            CHECK(!script.isEmpty());
            CHECK(script.contains("-TunnelType "));
        }
    }

    // ---- Hồi quy lỗi THẬT đã gặp trên Windows: Ikev2 CHỈ chấp nhận Eap/MachineCertificate, KHÔNG
    // chấp nhận MSChapv2 ("IKEv2 tunnel type only supports Eap and Machine certificate as
    // authentication method."). Xác nhận qua vpn_real_check.exe (Add-VpnConnection thật) rằng
    // -AuthenticationMethod Eap cho Ikev2 và MSChapv2 cho 4 loại còn lại đều thành công thật trên
    // Windows (addOk=1 cho cả 5 loại tunnel). Test này khoá lại lựa chọn đó ở mức nội dung script. ----
    {
        VpnProfile pIkev2;
        pIkev2.name = "Test";
        pIkev2.serverAddress = "1.2.3.4";
        pIkev2.tunnelType = VpnTunnelType::Ikev2;
        const QString ikev2Script = VpnController::internal::buildAddConnectionScript(pIkev2);
        CHECK(ikev2Script.contains("-AuthenticationMethod Eap"));
        CHECK(!ikev2Script.contains("-AuthenticationMethod MSChapv2"));

        for (VpnTunnelType t : {VpnTunnelType::Automatic, VpnTunnelType::L2tp, VpnTunnelType::Sstp, VpnTunnelType::Pptp})
        {
            VpnProfile p;
            p.name = "Test";
            p.serverAddress = "1.2.3.4";
            p.tunnelType = t;
            const QString script = VpnController::internal::buildAddConnectionScript(p);
            CHECK(script.contains("-AuthenticationMethod MSChapv2"));
        }
    }

    // ---- Dấu nháy "cong" U+2018/2019/201A/201B: PowerShell coi là dấu nháy đơn - phải được nhân đôi ----
    // Hồi quy lỗ hổng chèn lệnh: tên kết nối "x’; Start-Process calc; ‘" từng đóng chuỗi '...' sớm và phần
    // còn lại chạy như LỆNH (escapePsString cũ chỉ nhân đôi dấu ' ASCII).
    {
        const QString curly = QString::fromUtf8("Vi\xE2\x80\x99s VPN");              // ’ (U+2019)
        const QString doubled = QString::fromUtf8("Vi\xE2\x80\x99\xE2\x80\x99s VPN"); // ’’
        CHECK(VpnController::internal::escapePsString(curly) == doubled);
        CHECK(VpnController::internal::escapePsString(curly) == PowerShellRunner::quoteLiteral(curly));
        for (ushort quote : {0x0027, 0x2018, 0x2019, 0x201A, 0x201B})
        {
            const QString one = QString("a") + QChar(quote) + "b";
            const QString two = QString("a") + QChar(quote) + QChar(quote) + "b";
            CHECK(VpnController::internal::escapePsString(one) == two);
        }

        const QString attack = QString::fromUtf8("x\xE2\x80\x99; Start-Process calc; \xE2\x80\x98");
        VpnProfile p;
        p.name = attack;
        p.serverAddress = attack;
        const QString add = VpnController::internal::buildAddConnectionScript(p);
        const QString remove = VpnController::internal::buildRemoveConnectionScript(attack);
        const QString safe = "'" + PowerShellRunner::quoteLiteral(attack) + "'";
        CHECK(add.contains("-Name " + safe));
        CHECK(add.contains("-ServerAddress " + safe));
        CHECK(remove.contains("-Name " + safe));
        // Không còn dấu nháy cong ĐƠN LẺ nào trong script (mọi dấu đều đã thành cặp)
        for (const QString& script : {add, remove})
        {
            QString stripped = script;
            stripped.remove(QString(QChar(0x2019)) + QChar(0x2019));
            stripped.remove(QString(QChar(0x2018)) + QChar(0x2018));
            CHECK(!stripped.contains(QChar(0x2019)) && !stripped.contains(QChar(0x2018)));
        }
    }

    // ---- Tên có dấu tiếng Việt: JSON UTF-8 (PowerShellRunner nay ép stdout sang UTF-8) phải ra đúng tên ----
    {
        const QByteArray json = QString::fromUtf8(
            R"([{"Name":"Hà Lan - Mullvad","ServerAddress":"nl1.example.com","TunnelType":"Ikev2","ConnectionStatus":"Disconnected"},
                {"Name":"Nhật Bản ’riêng’","ServerAddress":"jp1.example.com","TunnelType":"Sstp","ConnectionStatus":"Connected"}])").toUtf8();
        QString err;
        const auto list = VpnController::internal::parseConnectionsJson(json, &err);
        CHECK(err.isEmpty());
        CHECK(list.size() == 2);
        if (list.size() == 2)
        {
            CHECK(list[0].name == QString::fromUtf8("Hà Lan - Mullvad"));
            CHECK(list[1].name == QString::fromUtf8("Nhật Bản ’riêng’"));
            CHECK(list[1].isConnected());
        }
        // Tên có dấu đi vào script nguyên vẹn (script được truyền dạng UTF-16 qua -EncodedCommand)
        CHECK(VpnController::internal::buildAddConnectionScript(VpnProfile{QString::fromUtf8("Hà Lan"), {}, "a.b", VpnTunnelType::Ikev2, {}})
                  .contains(QString::fromUtf8("-Name 'Hà Lan'")));
    }

    // ---- Tệp thông tin phụ (nhãn quốc gia + tên đăng nhập): nằm trong thư mục dữ liệu, ghi/đọc đúng ----
    {
        const QString file = AppPaths::profileFile("vpn_profiles.json");
        CHECK(file.startsWith(dataDir.path())); // đường dẫn tuyệt đối trong thư mục dữ liệu (ở đây: thư mục tạm)

        const QString name = QString::fromUtf8("OneForAllTest-Hà Lan");
        CHECK(VpnController::profileMeta(name).countryLabel.isEmpty());

        QString err;
        CHECK(VpnController::setProfileMeta(name, {QString::fromUtf8("Hà Lan"), "nguoidung@example.com"}, &err));
        CHECK(err.isEmpty());
        CHECK(QFile::exists(file));
        const VpnController::VpnProfileMeta meta = VpnController::profileMeta(name);
        CHECK(meta.countryLabel == QString::fromUtf8("Hà Lan"));
        CHECK(meta.username == "nguoidung@example.com");

        // setCountryLabel chỉ đổi nhãn, giữ nguyên tên đăng nhập
        VpnController::setCountryLabel(name, QString::fromUtf8("Đức"));
        CHECK(VpnController::profileMeta(name).countryLabel == QString::fromUtf8("Đức"));
        CHECK(VpnController::profileMeta(name).username == "nguoidung@example.com");

        // Tệp KHÔNG có trường mật khẩu nào
        {
            QFile f(file);
            CHECK(f.open(QIODevice::ReadOnly));
            const QByteArray content = f.readAll().toLower();
            CHECK(!content.contains("password") && !content.contains("matkhau"));
        }

        // Định dạng cũ { "<tên>": "<nhãn>" } vẫn đọc được, và được giữ lại khi ghi thêm mục khác
        {
            QFile f(file);
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(QString::fromUtf8(R"({"OneForAllTest-Cu":"Nhật Bản"})").toUtf8());
        }
        CHECK(VpnController::profileMeta("OneForAllTest-Cu").countryLabel == QString::fromUtf8("Nhật Bản"));
        CHECK(VpnController::setProfileMeta("OneForAllTest-Moi", {"VN", "u"}, nullptr));
        CHECK(VpnController::profileMeta("OneForAllTest-Cu").countryLabel == QString::fromUtf8("Nhật Bản"));
        CHECK(VpnController::profileMeta("OneForAllTest-Moi").username == "u");
    }

    // ---- VpnConnector::describeRasError: thông báo tiếng Việt + giữ mã lỗi ----
    {
        const QString notFound = VpnConnector::describeRasError(623);
        CHECK(notFound.contains("623"));
        CHECK(notFound.contains(QString::fromUtf8("Không tìm thấy hồ sơ VPN")));
        CHECK(VpnConnector::describeRasError(691).contains(QString::fromUtf8("sai tên đăng nhập hoặc mật khẩu")));
        CHECK(VpnConnector::describeRasError(809).contains("809"));
        CHECK(!VpnConnector::describeRasError(999999).isEmpty());
        std::printf("describeRasError(623) = %s\n", qPrintable(notFound));
    }

    // ---- VpnConnector::evaluateDialPoll: quyết định của vòng chờ quay số (thuần, không gọi RAS) ----
    {
        using Outcome = VpnConnector::DialOutcome;
        using Poll = VpnConnector::DialPoll;
        QString msg;

        Poll poll; // đang quay số, chưa có kết quả gì
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Pending);
        CHECK(msg.isEmpty());

        // Hồi quy: RASCS_Disconnected KHÔNG kèm mã lỗi từng bị bỏ qua -> vòng chờ đứng tới hết 45 giây.
        poll = Poll{};
        poll.connState = VpnConnector::kRasStateDisconnected;
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Failed);
        CHECK(msg.contains(QString::fromUtf8("đã bị ngắt trước khi hoàn tất")));
        std::printf("Disconnected khong ma loi -> '%s'\n", qPrintable(msg));
        // ...kể cả khi chỉ hàm gọi lại báo, còn trạng thái lúc này không đọc được
        poll = Poll{};
        poll.callbackDisconnected = true;
        poll.statusResult = 31; // ERROR_GEN_FAILURE
        msg.clear();
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Failed && !msg.isEmpty());

        // Có mã lỗi thì vẫn báo theo MÃ LỖI (rõ hơn thông báo chung)
        poll = Poll{};
        poll.connState = VpnConnector::kRasStateDisconnected;
        poll.statusError = 691;
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Failed && msg.contains("691"));
        poll = Poll{};
        poll.callbackDisconnected = true;
        poll.callbackError = 809;
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Failed && msg.contains("809"));

        // Mã 600 (đang chờ) không phải lỗi; trạng thái không đọc được thì không kết luận gì từ nó
        poll = Poll{};
        poll.callbackError = 600;
        poll.statusError = 600;
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Pending);
        poll = Poll{};
        poll.statusResult = 31;
        poll.connState = VpnConnector::kRasStateDisconnected; // rác - statusResult != 0
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Pending);

        // Đã kết nối luôn thắng
        poll = Poll{};
        poll.connState = VpnConnector::kRasStateConnected;
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Connected);
        poll = Poll{};
        poll.callbackConnected = true;
        poll.statusResult = 6;
        poll.invalidHandleMs = VpnConnector::kInvalidHandleGraceMs + 1000;
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Connected);

        // HỒI QUY v1.19.30 (lỗi THẬT, đo trên máy người dùng): RasGetConnectStatusW trả ERROR_INVALID_HANDLE
        // (6) ngay ở +16ms trong lúc phiên vẫn đang tiến triển. Coi đó là thất bại ở nhịp đầu khiến MỌI lần
        // quay số qua app bị gác máy (rasdial.exe vẫn nối được cùng hồ sơ). Mã 6 tạm thời phải là Pending.
        poll = Poll{};
        poll.statusResult = 6;
        poll.invalidHandleMs = 0;
        msg.clear();
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Pending && msg.isEmpty());
        poll.invalidHandleMs = VpnConnector::kInvalidHandleGraceMs - 1;
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Pending);
        // Kéo dài quá ngưỡng mà hàm gọi lại không báo gì -> mới coi là phiên đã mất, thông báo dễ hiểu
        // (KHÔNG phải "The handle is invalid").
        poll.invalidHandleMs = VpnConnector::kInvalidHandleGraceMs;
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Failed);
        CHECK(!msg.isEmpty() && !msg.contains("handle"));
        // Hàm gọi lại ĐÃ có mã lỗi thật (vd 789) thì báo NGAY theo mã đó, không cần chờ ngưỡng.
        poll = Poll{};
        poll.statusResult = 6;
        poll.callbackError = 789;
        msg.clear();
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Failed && msg.contains("789"));
        // ...và hàm gọi lại báo đã ngắt (vd sau khi máy chủ từ chối) cũng là thất bại ngay
        poll = Poll{};
        poll.statusResult = 6;
        poll.callbackDisconnected = true;
        poll.callbackError = 628;
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Failed && msg.contains("628"));
    }

    // ---- PublicIpChecker: máy chủ HTTP GIẢ trên 127.0.0.1 (không gọi ra Internet) ----
    {
        QNetworkProxyFactory::setUseSystemConfiguration(false); // không đẩy yêu cầu tới 127.0.0.1 qua proxy của máy

        QTcpServer server;
        CHECK(server.listen(QHostAddress::LocalHost, 0));
        int answerDelayMs = 0; // < 0: không bao giờ trả lời
        int requests = 0;
        QObject::connect(&server, &QTcpServer::newConnection, [&] {
            while (QTcpSocket* socket = server.nextPendingConnection())
            {
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &answerDelayMs, &requests] {
                    if (socket->property("answered").toBool() || !socket->peek(65536).contains("\r\n\r\n"))
                        return;
                    socket->setProperty("answered", true);
                    ++requests;
                    if (answerDelayMs < 0)
                        return;
                    QTimer::singleShot(answerDelayMs, socket, [socket] {
                        const QByteArray body = R"({"success":true,"ip":"203.0.113.7","country":"Testland","city":"Thu"})";
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                                      QByteArray::number(body.size()) + "\r\n\r\n" + body);
                        socket->disconnectFromHost();
                    });
                });
            }
        });

        // Chạy một lần check(); trả về thời gian (ms) tới khi có kết quả/lỗi, -1 nếu quá 'waitMs' không có gì.
        auto runCheck = [](PublicIpChecker& checker, int waitMs, QString* ip, QString* error) -> qint64 {
            ip->clear();
            error->clear();
            QEventLoop loop;
            bool done = false;
            const auto c1 = QObject::connect(&checker, &PublicIpChecker::result, &loop, [&](PublicIpInfo info) {
                *ip = info.ip;
                done = true;
                loop.quit();
            });
            const auto c2 = QObject::connect(&checker, &PublicIpChecker::errorOccurred, &loop, [&](QString message) {
                *error = message;
                done = true;
                loop.quit();
            });
            QTimer::singleShot(waitMs, &loop, &QEventLoop::quit);
            QElapsedTimer timer;
            timer.start();
            checker.check();
            loop.exec();
            QObject::disconnect(c1);
            QObject::disconnect(c2);
            return done ? timer.elapsed() : -1;
        };
        auto idle = [](int ms) {
            QEventLoop loop;
            QTimer::singleShot(ms, &loop, &QEventLoop::quit);
            loop.exec();
        };

        const QUrl url(QString("http://127.0.0.1:%1/").arg(server.serverPort()));
        QString ip, error;
        {
            // Hồi quy: bộ hẹn giờ của một phản hồi ĐÃ XONG không được hủy phản hồi của lần check() sau. Lần 1
            // xong ngay; lần 2 gửi ở ~1,0 giây và được trả lời ở ~1,9 giây - bộ hẹn giờ 1,5 giây của LẦN 1 nổ
            // đúng lúc lần 2 đang chờ (trước đây nó hủy nhầm khi phản hồi mới được cấp phát trùng địa chỉ cũ).
            PublicIpChecker checker;
            checker.setEndpointForTest(url, 1500);
            for (int round = 0; round < 2; ++round)
            {
                answerDelayMs = 0;
                CHECK(runCheck(checker, 5000, &ip, &error) >= 0);
                CHECK(ip == "203.0.113.7" && error.isEmpty());
                idle(1000);
                answerDelayMs = 900;
                const qint64 took = runCheck(checker, 5000, &ip, &error);
                CHECK(took >= 800);
                CHECK(ip == "203.0.113.7");
                CHECK(error.isEmpty());
                idle(700); // qua hẳn mốc 1,5 giây của mọi bộ hẹn giờ cũ trước vòng kế tiếp
            }
        }
        {
            // Thời gian chờ vẫn hoạt động: máy chủ im lặng -> báo lỗi sau ~0,4 giây, và lần sau vẫn dùng được.
            PublicIpChecker checker;
            checker.setEndpointForTest(url, 400);
            answerDelayMs = -1;
            const qint64 took = runCheck(checker, 5000, &ip, &error);
            CHECK(took >= 300 && took < 3000);
            CHECK(ip.isEmpty() && !error.isEmpty());
            answerDelayMs = 0;
            CHECK(runCheck(checker, 5000, &ip, &error) >= 0);
            CHECK(ip == "203.0.113.7" && error.isEmpty());
        }
        CHECK(requests == 6);
        std::printf("PublicIpChecker: %d yeu cau toi may chu gia 127.0.0.1\n", requests);
    }

    // ---- VpnConnector qua RAS API - lời gọi THẬT nhưng AN TOÀN: tên hồ sơ KHÔNG tồn tại ----
    // Không có VPN nào được kết nối/tạo/xóa: RasDialW từ chối ngay vì không có mục sổ danh bạ tên này.
    // Kiểm luôn hồi quy "cờ hủy không bao giờ được đặt lại": sau một requestCancel(), thao tác KẾ TIẾP phải
    // chạy bình thường (trước đây mọi lần sau đều tự "Đã hủy." trong 200ms).
    {
        const QString missing = QString("OneForAllTest-KhongTonTai-%1").arg(QCoreApplication::applicationPid());

        auto runOnce = [](VpnConnector& connector, bool* ok, QString* message) {
            bool finished = false;
            const auto connection = QObject::connect(&connector, &VpnConnector::operationFinished,
                                                     [&](bool success, QString text) {
                                                         *ok = success;
                                                         *message = text;
                                                         finished = true;
                                                     });
            connector.start();
            const bool exited = connector.wait(60000);
            QObject::disconnect(connection);
            return exited && finished;
        };

        VpnConnector connector;
        bool ok = true;
        QString message;

        connector.setConnectTarget(missing, "nguoi dung/gia", "mat-khau-gia-KHONG-co-that");
        CHECK(runOnce(connector, &ok, &message));
        std::printf("RasDial ho so khong ton tai -> ok=%d, '%s'\n", ok ? 1 : 0, qPrintable(message));
        CHECK(!ok);
        CHECK(!message.isEmpty());
        CHECK(message.contains("623")); // ERROR_CANNOT_FIND_PHONEBOOK_ENTRY
        CHECK(!message.contains("mat-khau-gia")); // mật khẩu không bao giờ lọt vào thông báo

        // Tên/tên đăng nhập bắt đầu bằng "/" và mật khẩu "*": với rasdial.exe là tham số/chờ nhập bàn phím;
        // với RAS API chỉ là chuỗi thường -> vẫn trả lỗi "không có hồ sơ" ngay, không treo.
        connector.setConnectTarget("/" + missing, "/PHONEBOOK:x", "*");
        CHECK(runOnce(connector, &ok, &message));
        CHECK(!ok && message.contains("623"));

        // Sau khi bị yêu cầu hủy, thao tác kế tiếp KHÔNG được tự hủy
        connector.requestCancel();
        connector.setConnectTarget(missing, QString(), QString());
        CHECK(runOnce(connector, &ok, &message));
        CHECK(!ok);
        CHECK(message != QString::fromUtf8("Đã hủy."));
        CHECK(message.contains("623"));

        connector.requestCancel();
        connector.setDisconnectTarget(missing);
        ok = false;
        CHECK(runOnce(connector, &ok, &message));
        std::printf("Ngat ket noi ho so khong ket noi -> ok=%d, '%s'\n", ok ? 1 : 0, qPrintable(message));
        CHECK(ok); // không có gì để ngắt - không phải lỗi
        CHECK(message != QString::fromUtf8("Đã hủy."));

        // Tên quá dài không bị cắt ngắn âm thầm thành một tên khác
        connector.setConnectTarget(QString(400, 'x'), "u", "p");
        CHECK(runOnce(connector, &ok, &message));
        CHECK(!ok && !message.isEmpty());

        // Hủy đối tượng NGAY sau start(): destructor phải tự hủy + chờ luồng thoát (hủy một QThread còn
        // đang chạy là hành vi không xác định - trước đây chỉ VpnTab tự lo việc này).
        for (int i = 0; i < 10; ++i)
        {
            auto* shortLived = new VpnConnector;
            shortLived->setConnectTarget(missing, "u", "p");
            shortLived->start();
            delete shortLived;
        }
        CHECK(true); // tới được đây = không crash/treo
    }

    // ---- "%1".."%4" trong tên/địa chỉ không bị QString::arg thay thế chéo sang tham số khác ----
    {
        VpnProfile p;
        p.name = "%2 %3 %4";
        p.serverAddress = "%1";
        const QString script = VpnController::internal::buildAddConnectionScript(p);
        CHECK(script.contains("-Name '%2 %3 %4' -ServerAddress '%1' -TunnelType Ikev2 -AuthenticationMethod Eap"));
        CHECK(VpnController::internal::buildRemoveConnectionScript("%1%2").contains("-Name '%1%2' -Force"));
    }

    // ---- Hồi quy v1.19.23: khóa chia sẻ trước L2TP/IPsec (-L2tpPsk) ----
    {
        VpnProfile p;
        p.name = "L2TP thử";
        p.serverAddress = "vpn.example.com";
        p.tunnelType = VpnTunnelType::L2tp;
        p.l2tpPsk = "kh'oa%1bi’mat";
        const QString script = VpnController::internal::buildAddConnectionScript(p);
        CHECK(script.contains("-L2tpPsk $env:ONEFORALL_L2TP_PSK")); // khóa đi qua biến môi trường riêng
        CHECK(!script.contains("bi’mat") && !script.contains("kh'oa")); // không bao giờ nằm trong script/dòng lệnh
        CHECK(script.contains("-TunnelType L2tp"));
        p.l2tpPsk.clear();
        CHECK(!VpnController::internal::buildAddConnectionScript(p).contains("L2tpPsk"));
        p.tunnelType = VpnTunnelType::Ikev2;
        p.l2tpPsk = "khong-dung";
        CHECK(!VpnController::internal::buildAddConnectionScript(p).contains("L2tpPsk")); // chỉ L2TP mới có
    }

    // ---- WireGuardController::internal::sanitizeTunnelName: thuần, không gọi wireguard.exe ----
    {
        namespace WG = WireGuardController::internal;
        CHECK(WG::sanitizeTunnelName("NL-Proton") == "NL-Proton"); // đã hợp lệ: giữ nguyên
        CHECK(WG::sanitizeTunnelName("Hà Lan Proton") == "H__Lan_Proton"); // chữ có dấu -> "_"
        CHECK(WG::sanitizeTunnelName("...tunnel...") == "tunnel"); // bỏ "." đầu/cuối
        CHECK(WG::sanitizeTunnelName("") == "tunnel"); // rỗng -> mặc định
        CHECK(WG::sanitizeTunnelName(QString(40, QChar('a'))).size() == 32); // cắt tối đa 32 ký tự
        CHECK(WG::sanitizeTunnelName("a b/c:d") == "a_b_c_d"); // khoảng trắng, /, : đều bị thay
    }

    // ---- WireGuardController::internal::serviceName ----
    {
        CHECK(WireGuardController::internal::serviceName("NL-Proton") == "WireGuardTunnel$NL-Proton");
    }

    // ---- WireGuardController::internal::parseConfForDisplay: đọc .conf mẫu, KHÔNG BAO GIỜ trả khóa riêng ----
    {
        namespace WG = WireGuardController::internal;
        const QString conf = R"([Interface]
PrivateKey = yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk=
Address = 10.2.0.2/32
DNS = 10.2.0.1

[Peer]
PublicKey = xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg=
Endpoint = 123.45.67.89:51820
AllowedIPs = 0.0.0.0/0, ::/0
)";
        const auto parsed = WG::parseConfForDisplay(conf);
        CHECK(parsed.valid);
        CHECK(parsed.address == "10.2.0.2/32");
        CHECK(parsed.dns == "10.2.0.1");
        CHECK(parsed.endpoint == "123.45.67.89:51820");
        CHECK(parsed.allowedIps == "0.0.0.0/0, ::/0");

        // Nội dung .conf KHÔNG BAO GIỜ bị trả lại nguyên văn khóa riêng/khóa công khai qua struct hiển thị.
        CHECK(parsed.address != conf && parsed.endpoint != conf); // không trả nguyên văn (hiển nhiên, chỉ để chắc)

        // .conf rỗng/không đúng định dạng -> invalid + có lý do
        const auto empty = WG::parseConfForDisplay("khong phai dinh dang wireguard");
        CHECK(!empty.valid);
        CHECK(!empty.error.isEmpty());
    }

    // ---- WireGuardController::internal::isServiceRunningOutput: output mẫu của `sc query` ----
    {
        namespace WG = WireGuardController::internal;
        const QString running = R"(
SERVICE_NAME: WireGuardTunnel$NL-Proton
        TYPE               : 10  WIN32_OWN_PROCESS
        STATE              : 4  RUNNING
                                (STOPPABLE, NOT_PAUSABLE, ACCEPTS_SHUTDOWN)
        WIN32_EXIT_CODE    : 0  (0x0)
)";
        CHECK(WG::isServiceRunningOutput(running));

        const QString stopped = R"(
SERVICE_NAME: WireGuardTunnel$NL-Proton
        TYPE               : 10  WIN32_OWN_PROCESS
        STATE              : 1  STOPPED
        WIN32_EXIT_CODE    : 0  (0x0)
)";
        CHECK(!WG::isServiceRunningOutput(stopped));

        // Dịch vụ không tồn tại (chưa từng cài, hoặc đã gỡ): sc.exe báo lỗi, không có dòng STATE nào.
        const QString notFound = "[SC] EnumQueryServicesStatus:OpenService FAILED 1060:\n\nThe specified service does not exist as an installed service.\n";
        CHECK(!WG::isServiceRunningOutput(notFound));
        CHECK(!WG::isServiceRunningOutput("")); // rỗng cũng không crash, chỉ false
    }

    // ---- WireGuardController::internal::tunnelNameFromConfigFileName ----
    {
        namespace WG = WireGuardController::internal;
        CHECK(WG::tunnelNameFromConfigFileName("NL-Proton.conf.dpapi") == "NL-Proton");
        CHECK(WG::tunnelNameFromConfigFileName("NL-Proton.conf.dpapi").toLower() != "nl-proton.conf.dpapi");
        CHECK(WG::tunnelNameFromConfigFileName("khong-phai-wireguard.txt").isEmpty());
        CHECK(WG::tunnelNameFromConfigFileName("").isEmpty());
    }

    // ---- WireGuardController::setCountryLabel/listTunnels metadata: ghi/đọc trong thư mục dữ liệu test ----
    // (CHƯA kiểm chứng import/removeTunnel/startTunnel/stopTunnel/listTunnels thật - cần máy có WireGuard
    // for Windows đã cài + một tunnel/máy chủ WireGuard thật, môi trường build không có cả hai.)

    // ---- VpnGateClient: phân tích CSV của API VPN Gate (thuần, không mạng) ----
    {
        using namespace VpnGateClientInternal;
        CHECK(isValidIpv4("38.34.239.132"));
        CHECK(isValidIpv4("1.2.3.4"));
        CHECK(!isValidIpv4("2001:db8::1"));      // IPv6 - VPN Gate chỉ IPv4
        CHECK(!isValidIpv4("vpn.example.com"));  // tên miền
        CHECK(!isValidIpv4("") && !isValidIpv4("999.1.1.1"));

        // Đúng định dạng thật: dòng *vpn_servers, dòng tiêu đề #..., các dòng dữ liệu 15 trường, dòng kết thúc *.
        const QByteArray csv =
            "*vpn_servers\r\n"
            "#HostName,IP,Score,Ping,Speed,CountryLong,CountryShort,NumVpnSessions,Uptime,TotalUsers,TotalTraffic,LogType,Operator,Message,OpenVPN_ConfigData_Base64\r\n"
            "vpn111,38.34.239.132,4316364,10,348583205,United States,US,142,3151150053,164393,25575562959680,2weeks,owner,hi,QUJD\r\n"
            "vpn222,219.100.37.1,900000,55,12000000,Japan,JP,7,100,50,999,2weeks,op,msg,QUJD\r\n"
            "vpnBAD,not-an-ip,100,5,100,Nowhere,NO,1,1,1,1,2weeks,,,QUJD\r\n"
            "vpnSHIFT,8.8.8.8,100,5,100,Korea, Republic of,KR,1,1,1,1,2weeks,,,QUJD\r\n"
            "vpnNEG,9.9.9.9,-5,-3,-100,Negland,NG,-2,1,1,1,2weeks,,,QUJD\r\n"
            "*\r\n";
        const QList<VpnGateServer> servers = parseCsv(csv);
        // 3 dòng hợp lệ (US, JP, NG). Dòng IP sai bị bỏ; dòng "Korea, Republic of" lệch cột -> mã QG không
        // phải 2 chữ cái -> bỏ.
        CHECK(servers.size() == 3);
        if (servers.size() == 3)
        {
            const VpnGateServer& us = servers[0];
            CHECK(us.hostName == "vpn111" && us.ip == "38.34.239.132");
            CHECK(us.countryLong == "United States" && us.countryShort == "US");
            CHECK(us.score == 4316364 && us.ping == 10 && us.speedBps == 348583205 && us.sessions == 142);
            CHECK(us.speedMbps() > 348.0 && us.speedMbps() < 349.0);
            CHECK(servers[1].countryShort == "JP" && servers[1].ip == "219.100.37.1");
            // Giá trị âm bị kẹp về 0.
            const VpnGateServer& ng = servers[2];
            CHECK(ng.countryShort == "NG" && ng.score == 0 && ng.ping == 0 && ng.speedBps == 0 && ng.sessions == 0);
        }

        // Rỗng/chỉ có dòng điều khiển -> danh sách rỗng, không crash.
        CHECK(parseCsv("").isEmpty());
        CHECK(parseCsv("*vpn_servers\n#HostName,IP\n*\n").isEmpty());
        CHECK(parseCsv("rac rac rac\nkhong,du,cot\n").isEmpty());
    }

    // ---- VpnGateCache: gộp tích lũy + loại cũ + JSON khứ hồi (thuần) ----
    {
        auto mk = [](const QString& ip, const QString& cc, qint64 score, qint64 seen) {
            VpnGateServer s; s.ip = ip; s.countryShort = cc; s.countryLong = cc; s.score = score; s.lastSeenEpoch = seen; return s;
        };
        const qint64 now = 1000000;
        const qint64 day = 24 * 60 * 60;
        QList<VpnGateServer> existing = {mk("1.1.1.1", "US", 100, now - 1*day), mk("2.2.2.2", "JP", 200, now - 10*day)};
        QList<VpnGateServer> fresh = {mk("1.1.1.1", "US", 999, 0), mk("3.3.3.3", "KR", 50, 0)};
        // 1.1.1.1 cập nhật (điểm mới 999, lastSeen=now); 2.2.2.2 quá cũ (10 ngày > 3) bị loại; 3.3.3.3 thêm mới.
        const QList<VpnGateServer> merged = VpnGateCache::merge(existing, fresh, now, 3*day, 1000);
        CHECK(merged.size() == 2);
        if (merged.size() == 2)
        {
            CHECK(merged[0].ip == "1.1.1.1" && merged[0].score == 999 && merged[0].lastSeenEpoch == now); // điểm cao lên đầu
            CHECK(merged[1].ip == "3.3.3.3" && merged[1].lastSeenEpoch == now);
        }
        // Trần số lượng.
        QList<VpnGateServer> many;
        for (int i = 0; i < 10; ++i) many << mk(QString("10.0.0.%1").arg(i), "US", i, now);
        CHECK(VpnGateCache::merge({}, many, now, 3*day, 3).size() == 3);
        // JSON khứ hồi.
        const QByteArray js = VpnGateCache::internal::toJson(merged);
        const QList<VpnGateServer> back = VpnGateCache::internal::fromJson(js);
        CHECK(back.size() == 2 && back[0].ip == "1.1.1.1" && back[0].score == 999);
        // JSON hỏng / IP sai bị bỏ.
        CHECK(VpnGateCache::internal::fromJson("rac").isEmpty());
        CHECK(VpnGateCache::internal::fromJson(R"({"servers":[{"ip":"not-ip","countryShort":"US"}]})").isEmpty());
    }

    // ---- ConnectivityTester: TCP tới máy chủ cục bộ (chạm được) và tới cổng đóng (không được) ----
    {
        auto runTest = [](ConnectivityTester& t, const QString& host, quint16 port, int waitMs, bool* reachable, int* ms) {
            QEventLoop loop;
            bool done = false;
            const auto c = QObject::connect(&t, &ConnectivityTester::finished, &loop,
                                            [&](bool r, int e, QString) { *reachable = r; *ms = e; done = true; loop.quit(); });
            QTimer::singleShot(waitMs, &loop, &QEventLoop::quit);
            t.test(host, port, waitMs - 500);
            loop.exec();
            QObject::disconnect(c);
            return done;
        };
        QTcpServer srv;
        CHECK(srv.listen(QHostAddress::LocalHost, 0));
        const quint16 openPort = srv.serverPort();
        bool reachable = false; int ms = -1;
        ConnectivityTester t1;
        CHECK(runTest(t1, "127.0.0.1", openPort, 4000, &reachable, &ms));
        CHECK(reachable && ms >= 0);
        // Đóng máy chủ -> cổng đó không còn ai nghe -> không chạm được (connection refused, nhanh).
        srv.close();
        ConnectivityTester t2;
        reachable = true; ms = -1;
        CHECK(runTest(t2, "127.0.0.1", openPort, 4000, &reachable, &ms));
        CHECK(!reachable);
    }

    // ---- "Sửa lỗi L2TP sau NAT": script đọc + lệnh đặt khóa registry (thuần) ----
    {
        const QString readScript = VpnController::internal::buildReadAssumeUdpScript();
        CHECK(readScript.contains("AssumeUDPEncapsulationContextOnSendRule"));
        CHECK(readScript.contains("PolicyAgent") && readScript.contains("none"));
        const QString setCmd = VpnController::internal::assumeUdpSetCommand();
        CHECK(setCmd.contains("AssumeUDPEncapsulationContextOnSendRule"));
        CHECK(setCmd.contains("PolicyAgent") && setCmd.contains("-Value 2") && setCmd.contains("DWord"));
    }

    // ---- ThirdPartyVpnScanner: các parser THUẦN (yêu cầu 2 - đọc hồ sơ VPN bên thứ ba) ----
    {
        using namespace ThirdPartyVpnScanner::internal;
        // .ovpn: lấy dòng remote đầu tiên, bỏ comment, kèm cổng nếu có.
        CHECK(parseOvpnRemote("client\n# comment\nremote vpn.example.com 1194 udp\nremote backup 2000\n") == "vpn.example.com:1194");
        CHECK(parseOvpnRemote("remote host-only\n") == "host-only");
        CHECK(parseOvpnRemote(";remote commented 1\nremote real 443\n") == "real:443");
        CHECK(parseOvpnRemote("khong co remote\n").isEmpty());

        // Cisco profile XML: HostEntry -> (HostName, HostAddress).
        const QByteArray cisco =
            "<?xml version=\"1.0\"?><AnyConnectProfile><ServerList>"
            "<HostEntry><HostName>Cong ty A</HostName><HostAddress>vpn.a.com</HostAddress></HostEntry>"
            "<HostEntry><HostName>Chi nhanh B</HostName><HostAddress>vpn.b.com</HostAddress></HostEntry>"
            "</ServerList></AnyConnectProfile>";
        const auto hosts = parseCiscoProfileXml(cisco);
        CHECK(hosts.size() == 2);
        if (hosts.size() == 2)
        {
            CHECK(hosts[0].first == "Cong ty A" && hosts[0].second == "vpn.a.com");
            CHECK(hosts[1].first == "Chi nhanh B" && hosts[1].second == "vpn.b.com");
        }
        CHECK(parseCiscoProfileXml("<x/>").isEmpty());
        CHECK(parseCiscoProfileXml("rac rac khong phai xml").isEmpty());

        CHECK(normalizeServer("  vpn.x.com:10443  ") == "vpn.x.com:10443");
        CHECK(firstExistingPath({"Z:/khong/ton/tai.exe", ""}).isEmpty());

        // scan() thật (chỉ đọc registry/tệp - an toàn; máy build thường không cài client nào -> rỗng).
        const auto found = ThirdPartyVpnScanner::scan();
        std::printf("ThirdPartyVpnScanner::scan() that: %d ho so VPN ben thu ba\n", static_cast<int>(found.size()));
        for (const auto& p : found)
        {
            std::printf("  - [%s] %s (%s) truc tiep=%s\n", qPrintable(p.clientName), qPrintable(p.name), qPrintable(p.server),
                        qPrintable(DirectVpn::protocolName(p.direct)));
            // v1.19.31: đúng loại kết nối trực tiếp theo phần mềm/kiểu tunnel.
            if (p.clientName == "FortiClient")
                CHECK(p.direct == DirectProtocol::FortinetSsl);
            else if (p.clientName == "FortiClient (IPsec)") // từ v1.19.36: client IKEv1 tự viết
                CHECK(p.direct == DirectProtocol::FortinetIpsec && p.canConnectDirectly());
            else if (p.clientName == "GlobalProtect" && !p.server.isEmpty())
                CHECK(p.direct == DirectProtocol::GlobalProtect && p.canConnectDirectly());
            else if (p.clientName == "OpenVPN" || p.clientName == "Cisco Secure Client")
                CHECK(p.direct == DirectProtocol::None);
        }
        CHECK(true); // tới đây không crash là đạt
    }

    // ---- VpnGateServer::sstpHost: tên miền SSTP <HostName>.opengw.net (lọc chặt dữ liệu từ mạng) ----
    {
        auto host = [](const QString& h) { VpnGateServer s; s.hostName = h; return s.sstpHost(); };
        CHECK(host("public-vpn-119") == "public-vpn-119.opengw.net"); // dạng thật trong API
        CHECK(host("vpn788502559") == "vpn788502559.opengw.net");
        CHECK(host("VPN123") == "vpn123.opengw.net");                 // chuẩn hóa chữ thường
        CHECK(host("").isEmpty());
        CHECK(host("-abc").isEmpty());                                // nhãn DNS không bắt đầu bằng '-'
        CHECK(host("abc-").isEmpty());
        CHECK(host("a.b").isEmpty());                                 // không cho chèn thêm nhãn/tên miền khác
        CHECK(host("evil.com/x").isEmpty());
        CHECK(host("x' ; Remove-Item C:\\").isEmpty());               // ký tự lạ (sẽ đi vào lệnh PowerShell) bị chặn
        CHECK(host(QString(64, 'a')).isEmpty());                      // > 63 ký tự
        CHECK(host(QString(63, 'a')) == QString(63, 'a') + ".opengw.net");
        CHECK(host(QString::fromUtf8("vpn\xc3\xa9")).isEmpty());       // ký tự ngoài ASCII
    }

    // ---- DirectProfileStore: hồ sơ GlobalProtect/FortiClient người dùng tự thêm (chế độ của hộp thoại "Thêm hồ sơ VPN") ----
    {
        using namespace DirectProfileStore;
        CHECK(clientName(DirectProtocol::GlobalProtect) == "GlobalProtect");
        CHECK(clientName(DirectProtocol::FortinetSsl) == "FortiClient");
        CHECK(clientName(DirectProtocol::FortinetIpsec) == "FortiClient (IPsec)");
        CHECK(clientName(DirectProtocol::None).isEmpty());

        // Chuẩn hóa địa chỉ nhập tay.
        CHECK(normalizeServer("  https://VPN.Congty.vn/global-protect/portal ") == "vpn.congty.vn");
        CHECK(normalizeServer("vpn.cty.vn:10443/remote/login") == "vpn.cty.vn:10443");
        CHECK(normalizeServer("119.17.206.59") == "119.17.206.59");
        CHECK(normalizeServer("  vpn cong ty ") == "vpn cong ty"); // sai định dạng: giữ nguyên (đã cắt) để validate báo lỗi

        // Kiểm định.
        QString err;
        CHECK(validate({"VPN cty", "vpn.cty.vn", DirectProtocol::GlobalProtect}, &err));
        CHECK(validate({"VPN cty", "vpn.cty.vn:10443", DirectProtocol::FortinetSsl}, &err));
        CHECK(validate({"VPN_CVTLT", "119.17.206.59", DirectProtocol::FortinetIpsec}, &err));
        CHECK(!validate({"", "vpn.cty.vn", DirectProtocol::GlobalProtect}, &err) && !err.isEmpty());           // thiếu tên
        CHECK(!validate({QString(81, 'a'), "vpn.cty.vn", DirectProtocol::GlobalProtect}, &err));                // tên quá dài
        CHECK(!validate({"x", "vpn.cty.vn", DirectProtocol::None}, &err));                                      // không phải loại trực tiếp
        CHECK(!validate({"x", "", DirectProtocol::FortinetSsl}, &err));                                         // thiếu máy chủ
        CHECK(!validate({"x", "vpn cty", DirectProtocol::FortinetSsl}, &err));                                  // có khoảng trắng
        CHECK(!validate({"x", "vpn.cty.vn:99999", DirectProtocol::FortinetSsl}, &err));                         // cổng sai
        CHECK(!validate({"x", "vpn;calc.exe", DirectProtocol::GlobalProtect}, &err));                           // ký tự lạ
        CHECK(!validate({"x", "119.17.206.59:500", DirectProtocol::FortinetIpsec}, &err));                      // IPsec không nhận cổng

        // JSON: khứ hồi + bỏ mục sai/hỏng.
        const QList<DirectVpnProfile> sample{{"A", "gp.a.vn", DirectProtocol::GlobalProtect},
                                             {"B", "ft.b.vn:8443", DirectProtocol::FortinetSsl},
                                             {"C", "1.2.3.4", DirectProtocol::FortinetIpsec}};
        CHECK(internal::parse(internal::serialize(sample)) == sample);
        CHECK(internal::parse("rác không phải JSON").isEmpty());
        CHECK(internal::parse("{\"a\":1}").isEmpty()); // không phải mảng
        const QByteArray mixed =
            R"([{"name":"Tot","server":"ok.vn","protocol":"globalprotect"},
                {"name":"Sai giao thuc","server":"ok.vn","protocol":"cisco"},
                {"name":"","server":"ok.vn","protocol":"fortigate-ssl"},
                {"name":"Sai may chu","server":"vpn cty","protocol":"fortigate-ssl"},
                42, "chuoi"])";
        const auto parsed = internal::parse(mixed);
        CHECK(parsed.size() == 1 && parsed[0].name == "Tot");

        // Lưu/đọc/trùng/xóa thật (vào thư mục dữ liệu TẠM của test).
        CHECK(load().isEmpty());
        QString addErr;
        CHECK(add({" VPN GP ", "https://GP.Congty.vn/portal", DirectProtocol::GlobalProtect}, &addErr));
        auto loaded = load();
        CHECK(loaded.size() == 1 && loaded[0].name == "VPN GP" && loaded[0].server == "gp.congty.vn");   // đã cắt + chuẩn hóa
        CHECK(!add({"vpn gp", "khac.vn", DirectProtocol::GlobalProtect}, &addErr) && addErr.contains("Đã có")); // trùng tên (không phân biệt hoa/thường)
        CHECK(add({"VPN GP", "ft.congty.vn", DirectProtocol::FortinetSsl}, &addErr));                    // cùng tên, khác giao thức: được
        CHECK(add({"VPN_CVTLT", "119.17.206.59", DirectProtocol::FortinetIpsec}, &addErr));
        CHECK(!add({"", "x.vn", DirectProtocol::GlobalProtect}, &addErr));                               // không hợp lệ: không lưu
        CHECK(load().size() == 3);

        // Chuyển sang dạng danh sách chung.
        const ThirdPartyVpnProfile tp = toThirdParty(load().last());
        CHECK(tp.custom && tp.clientName == "FortiClient (IPsec)" && tp.name == "VPN_CVTLT" && tp.server == "119.17.206.59");
        CHECK(tp.canConnectDirectly() && !tp.canLaunch() && tp.direct == DirectProtocol::FortinetIpsec);

        CHECK(remove(DirectProtocol::GlobalProtect, "VPN GP"));
        CHECK(!remove(DirectProtocol::GlobalProtect, "VPN GP")); // đã xóa
        CHECK(load().size() == 2);
        CHECK(remove(DirectProtocol::FortinetSsl, "VPN GP") && remove(DirectProtocol::FortinetIpsec, "VPN_CVTLT"));
        CHECK(load().isEmpty());
    }

    // ================= Chặn rò rỉ IPv6 (Ipv6LeakGuard / Ipv6GuardHelper / Ipv6GuardSession / PublicIpv6Checker) =================
    // KHÔNG đặt bộ lọc WFP thật nào: WFP cần quyền Administrator; test chạy KHÔNG quyền Admin nên mọi lời gọi thật chỉ được
    // phép thất bại sạch. Nếu lỡ chạy test với quyền Admin thì các ca gọi thật bị BỎ QUA (quy tắc 5 - không đổi mạng thật).
    {
        using namespace Ipv6LeakGuard;
        auto addr = [](const char* text) {
            Addr16 a{};
            const bool ok = parseAddress(QString::fromLatin1(text), &a);
            if (!ok)
                std::printf("  (parseAddress that bai: %s)\n", text);
            return a;
        };
        // ---- Địa chỉ / dải ----
        Addr16 a{};
        CHECK(parseAddress("2001:db8::1", &a) && formatAddress(a) == "2001:db8::1");
        CHECK(!parseAddress("203.0.113.7", &a));   // IPv4 - không nhận
        CHECK(!parseAddress("not-an-ip", &a));
        CHECK(!parseAddress("", &a));
        Prefix p;
        CHECK(parsePrefix("2000::/3", &p) && p.length == 3);
        CHECK(parsePrefix("::1/128", &p) && p.length == 128 && formatAddress(p.addr) == "::1");
        CHECK(!parsePrefix("2000::", &p));      // thiếu độ dài
        CHECK(!parsePrefix("2000::/129", &p));  // quá 128
        CHECK(!parsePrefix("2000::/-1", &p));
        CHECK(!parsePrefix("10.0.0.0/8", &p));  // IPv4
        CHECK(!parsePrefix("/3", &p));

        CHECK(isGlobalUnicast(addr("2001:db8::1")));
        CHECK(isGlobalUnicast(addr("2405:4802:1::1")));       // dạng IPv6 nhà mạng VN
        CHECK(isGlobalUnicast(addr("3fff:ffff:ffff::1")));    // mép trên 2000::/3
        CHECK(!isGlobalUnicast(addr("4000::1")));
        CHECK(!isGlobalUnicast(addr("1fff:ffff::1")));
        CHECK(!isGlobalUnicast(addr("fe80::1")));
        CHECK(!isGlobalUnicast(addr("fd7a:115c:a1e0::53"))); // ULA (Tailscale)
        CHECK(!isGlobalUnicast(addr("::1")));
        CHECK(isBlockedDestination(addr("2001::1")));         // Teredo
        CHECK(isBlockedDestination(addr("2002:c000:204::1"))); // 6to4
        CHECK(isBlockedDestination(addr("64:ff9b::808:808"))); // NAT64 tới 8.8.8.8
        CHECK(isBlockedDestination(addr("64:ff9b:1::1")));
        CHECK(!isBlockedDestination(addr("64:ff9b:2::1")));
        CHECK(!isBlockedDestination(addr("::1")));
        CHECK(!isBlockedDestination(addr("fe80::1234")));
        CHECK(!isBlockedDestination(addr("ff02::1")));          // multicast (khám phá hàng xóm)
        CHECK(!isBlockedDestination(addr("fd00::1")));
        CHECK(!isBlockedDestination(addr("::ffff:8.8.8.8")));   // IPv4 ánh xạ - không phải IPv6 ra Internet
        Prefix all;
        CHECK(parsePrefix("::/0", &all) && inPrefix(addr("fe80::1"), all) && inPrefix(addr("2001::1"), all));
        Prefix exact;
        CHECK(parsePrefix("2001:db8::5/128", &exact) && inPrefix(addr("2001:db8::5"), exact) &&
              !inPrefix(addr("2001:db8::6"), exact));
        Prefix odd;
        CHECK(parsePrefix("2001:db8:8000::/33", &odd) && inPrefix(addr("2001:db8:8000::1"), odd) &&
              inPrefix(addr("2001:db8:ffff::1"), odd) && !inPrefix(addr("2001:db8:7fff::1"), odd));
        Prefix bad;
        bad.length = 200;
        CHECK(!inPrefix(addr("2001::1"), bad));

        // ---- assess(): đánh giá "máy có lộ IPv6 không" trên dữ liệu dựng sẵn ----
        auto itf = [&](const char* alias, quint64 luid, quint32 type, bool up, QList<Addr16> globals, bool route) {
            InterfaceInfo i;
            i.alias = QString::fromUtf8(alias);
            i.luid = luid;
            i.ifType = type;
            i.up = up;
            i.globalAddresses = globals;
            i.hasDefaultRoute = route;
            return i;
        };
        const Addr16 g1 = addr("2405:4802:1:2::10");
        const Addr16 g2 = addr("2001:ee0:4f1::7");
        Assessment r = assess({}, QString());
        CHECK(r.exposure == Exposure::NoIpv6 && r.exposedInterfaces.isEmpty() && r.tunnelLuid == 0);
        r = assess({itf("Wi-Fi", 1, 71, true, {g1}, true)}, QString());
        CHECK(r.exposure == Exposure::Exposed && r.exposedInterfaces == QStringList{"Wi-Fi"});
        r = assess({itf("Wi-Fi", 1, 71, true, {g1}, false)}, QString()); // có địa chỉ nhưng không có tuyến ra
        CHECK(r.exposure == Exposure::NoIpv6);
        r = assess({itf("Ethernet", 2, 6, true, {}, true)}, QString()); // có tuyến nhưng chỉ có địa chỉ nội bộ
        CHECK(r.exposure == Exposure::NoIpv6);
        r = assess({itf("Ethernet", 2, 6, false, {g1}, true)}, QString()); // card đang tắt
        CHECK(r.exposure == Exposure::NoIpv6);
        r = assess({itf("Loopback Pseudo-Interface 1", 3, 24, true, {g1}, true)}, QString());
        CHECK(r.exposure == Exposure::NoIpv6);
        // Đường hầm VPN (tên kết nối RAS, so không phân biệt hoa thường) KHÔNG tính là "lộ"
        r = assess({itf("Wi-Fi", 1, 71, true, {g1}, true), itf("OneForAll-VPNGate", 77, 23, true, {g2}, true)},
                   "oneforall-vpngate");
        CHECK(r.exposure == Exposure::Exposed && r.exposedInterfaces == QStringList{"Wi-Fi"});
        CHECK(r.tunnelLuid == 77 && r.tunnelHasIpv6);
        r = assess({itf("OneForAll-VPNGate", 78, 23, true, {}, false)}, "OneForAll-VPNGate");
        CHECK(r.exposure == Exposure::NoIpv6 && r.tunnelLuid == 78 && !r.tunnelHasIpv6);
        r = assess({itf("OneForAll-VPNGate", 79, 23, false, {g2}, true)}, "OneForAll-VPNGate"); // đường hầm chưa lên
        CHECK(r.tunnelLuid == 0 && !r.tunnelHasIpv6);
        r = assess({itf("Wi-Fi", 1, 71, true, {g1}, true), itf("Ethernet", 2, 6, true, {g2}, true),
                    itf("Tailscale", 9, 53, true, {}, true)},
                   QString());
        CHECK(r.exposedInterfaces == (QStringList{"Wi-Fi", "Ethernet"}));

        // ---- snapshot() THẬT (chỉ đọc, không cần Admin) ----
        QString snapError;
        const QList<InterfaceInfo> real = snapshot(&snapError);
        CHECK(snapError.isEmpty());
        CHECK(!real.isEmpty());
        bool sawLoopback = false;
        for (const InterfaceInfo& i : real)
            if (i.ifType == 24)
                sawLoopback = true;
        CHECK(sawLoopback);
        const Assessment realA = assess(real, QString());
        std::printf("IPv6 that cua may: %d card, %s%s\n", static_cast<int>(real.size()),
                    realA.exposure == Exposure::Exposed ? "CO IPv6 ra Internet qua: " : "khong co IPv6 ra Internet",
                    qPrintable(realA.exposedInterfaces.join(", ")));

        // ---- describeError ----
        CHECK(describeError(5).contains("Administrator"));
        CHECK(describeError(0x80320009u).contains("0x80320009"));
        CHECK(describeError(2).contains("2"));

        // ---- WfpBlock THẬT khi KHÔNG có quyền Admin: phải từ chối sạch, không để lại gì ----
        if (!WinElevation::isElevated())
        {
            WfpBlock block;
            QString err;
            CHECK(!block.apply(WfpBlock::Config{}, &err));
            CHECK(err.contains("Administrator") || err.contains("WFP") || err.contains("Filtering"));
            CHECK(!block.isActive() && block.filterCount() == 0);
            block.close();
            block.close(); // đóng hai lần không sao
            CHECK(!block.isActive());
            std::printf("WfpBlock khong quyen Admin: %s\n", qPrintable(err));
        }
        else
        {
            std::printf("BO QUA ca WfpBlock that: test dang chay voi quyen Admin (se chan IPv6 that).\n");
        }

        // ---- Vòng đời tiến trình phụ ----
        using Ipv6GuardHelper::internal::shouldKeepGuarding;
        CHECK(!shouldKeepGuarding(true, true, 0));          // ứng dụng báo gỡ -> thôi ngay
        CHECK(!shouldKeepGuarding(true, false, 0));
        CHECK(shouldKeepGuarding(false, true, 0));          // ứng dụng còn
        CHECK(shouldKeepGuarding(false, true, 999999));     // ứng dụng còn thì không tự bỏ dù VPN vắng
        CHECK(shouldKeepGuarding(false, false, 0));         // ứng dụng đóng nhưng VPN còn -> VẪN chặn
        CHECK(shouldKeepGuarding(false, false, Ipv6GuardHelper::kGoneGraceMs - 1));
        CHECK(!shouldKeepGuarding(false, false, Ipv6GuardHelper::kGoneGraceMs)); // mất cả hai đủ lâu -> thoát

        // ---- Chữ hiển thị ----
        using S = Ipv6GuardSession::State;
        CHECK(Ipv6GuardSession::describe(S::Idle, false, {}, {}).isEmpty());
        CHECK(Ipv6GuardSession::describe(S::NotNeeded, false, {}, {}).contains("không có IPv6"));
        CHECK(Ipv6GuardSession::describe(S::Starting, false, {}, {}).contains("UAC"));
        CHECK(Ipv6GuardSession::describe(S::Active, false, {"Wi-Fi"}, {}).contains("Wi-Fi"));
        CHECK(Ipv6GuardSession::describe(S::Active, true, {"Wi-Fi"}, {}).contains("qua VPN"));
        CHECK(Ipv6GuardSession::describe(S::Failed, false, {}, "bạn đã từ chối").contains("bạn đã từ chối"));
        CHECK(Ipv6GuardSession::describe(S::Failed, false, {}, {}).contains("KHÔNG"));
        PublicIpv6Info v6;
        bool warn = true;
        CHECK(Ipv6GuardSession::describePublicIpv6(v6, true, S::Active, false, &warn).contains("không lộ") && !warn);
        CHECK(Ipv6GuardSession::describePublicIpv6(v6, false, S::Idle, false, &warn).contains("không ra Internet") && !warn);
        v6.reachable = true;
        v6.ip = "2405:4802:1::10";
        v6.country = "Vietnam";
        v6.city = "Hanoi";
        QString line = Ipv6GuardSession::describePublicIpv6(v6, false, S::Idle, false, &warn);
        CHECK(line.contains("2405:4802:1::10") && line.contains("Hanoi, Vietnam") && line.contains("thật") && !warn);
        line = Ipv6GuardSession::describePublicIpv6(v6, true, S::NotNeeded, false, &warn);
        CHECK(line.contains("vẫn lộ") && warn); // VPN đã nối mà IPv6 vẫn ra Internet -> cảnh báo đỏ
        line = Ipv6GuardSession::describePublicIpv6(v6, true, S::Active, false, &warn);
        CHECK(line.contains("vẫn lộ") && warn); // đã "chặn" mà vẫn ra được -> bộ lọc không tác dụng, phải báo
        line = Ipv6GuardSession::describePublicIpv6(v6, true, S::Active, true, &warn);
        CHECK(line.contains("qua VPN") && !warn);
        v6.city.clear();
        v6.country.clear();
        line = Ipv6GuardSession::describePublicIpv6(v6, false, S::Idle, false, &warn);
        CHECK(!line.contains("📍"));
    }

    // ---- Ipv6GuardSession: luồng quyết định với ảnh chụp card mạng GIẢ (không UAC, không bộ lọc thật) ----
    {
        using namespace Ipv6LeakGuard;
        Addr16 g{};
        parseAddress("2405:4802:1:2::10", &g);
        InterfaceInfo wifi;
        wifi.alias = "Wi-Fi";
        wifi.luid = 1;
        wifi.ifType = 71;
        wifi.up = true;
        wifi.globalAddresses = {g};
        wifi.hasDefaultRoute = true;
        bool exposed = false;

        auto waitReady = [](Ipv6GuardSession& s, int ms, bool* ok, QString* msg) {
            QEventLoop loop;
            bool got = false;
            const auto c = QObject::connect(&s, &Ipv6GuardSession::ready, &loop, [&](bool k, QString m) {
                *ok = k;
                *msg = m;
                got = true;
                loop.quit();
            });
            QTimer::singleShot(ms, &loop, &QEventLoop::quit);
            if (!got)
                loop.exec();
            QObject::disconnect(c);
            return got;
        };
        auto spin = [](int ms) {
            QEventLoop loop;
            QTimer::singleShot(ms, &loop, &QEventLoop::quit);
            loop.exec();
        };
        const QString missingHelper = QDir(dataDir.path()).filePath("khong-ton-tai.exe");

        // 1) Không có IPv6 -> sẵn sàng ngay, KHÔNG chạy tiến trình phụ (đường dẫn giả mà chạy thì sẽ báo lỗi)
        {
            Ipv6GuardSession s;
            Ipv6GuardSession::TestHooks h;
            h.snapshot = [&]() { return exposed ? QList<InterfaceInfo>{wifi} : QList<InterfaceInfo>{}; };
            h.helperProgram = missingHelper;
            h.launchWithoutElevation = true;
            h.monitorIntervalMs = 150;
            s.setTestHooks(h);
            QList<Ipv6GuardSession::State> states;
            QObject::connect(&s, &Ipv6GuardSession::stateChanged, [&](Ipv6GuardSession::State st, QString) { states << st; });
            exposed = false;
            bool ok = false;
            QString msg;
            s.begin("OneForAll-VPNGate", {});
            CHECK(waitReady(s, 2000, &ok, &msg) && ok && msg.isEmpty());
            CHECK(s.state() == Ipv6GuardSession::State::NotNeeded);
            // 2) IPv6 xuất hiện GIỮA PHIÊN -> tự chặn (ở đây tiến trình phụ không tồn tại -> Failed, có báo)
            exposed = true;
            spin(600);
            CHECK(states.contains(Ipv6GuardSession::State::Starting));
            CHECK(s.state() == Ipv6GuardSession::State::Failed);
            CHECK(s.statusText().contains("KHÔNG"));
            s.release();
            CHECK(s.state() == Ipv6GuardSession::State::Idle);
            const int before = static_cast<int>(states.size());
            spin(400); // đã gỡ: bộ dò không còn chạy
            CHECK(states.size() == before);
        }
        // 3) Có IPv6 + không khởi chạy được tiến trình phụ -> ready(false) (nơi gọi KHÔNG được quay số)
        {
            Ipv6GuardSession s;
            Ipv6GuardSession::TestHooks h;
            h.snapshot = [&]() { return QList<InterfaceInfo>{wifi}; };
            h.helperProgram = missingHelper;
            h.launchWithoutElevation = true;
            s.setTestHooks(h);
            bool ok = true;
            QString msg;
            s.begin("OneForAll-VPNGate", {});
            CHECK(waitReady(s, 2000, &ok, &msg) && !ok && msg.contains("IPv6"));
            CHECK(s.state() == Ipv6GuardSession::State::Failed);
        }
        // 4) release() khi đang chờ: không phát ready() nữa
        {
            Ipv6GuardSession s;
            Ipv6GuardSession::TestHooks h;
            h.snapshot = [&]() { return QList<InterfaceInfo>{}; };
            s.setTestHooks(h);
            bool ok = false;
            QString msg;
            s.begin("X", {});
            s.release(); // trước khi ready() kịp phát (singleShot 0)
            CHECK(!waitReady(s, 300, &ok, &msg));
            CHECK(s.state() == Ipv6GuardSession::State::Idle);
        }
        // 5) Tiến trình phụ THẬT (OneForAll.exe --ipv6-guard) KHÔNG nâng quyền: kênh điều khiển + mã xác thực + báo lỗi
        //    thiếu quyền về đúng ready(false) - trọn đường đi trừ đúng bước WFP cần Admin.
        const QString exe = QCoreApplication::applicationDirPath() + "/OneForAll.exe";
        if (WinElevation::isElevated())
        {
            std::printf("BO QUA ca tien trinh phu that: dang co quyen Admin (se chan IPv6 that).\n");
        }
        else if (!QFile::exists(exe))
        {
            std::printf("BO QUA ca tien trinh phu that: chua build %s\n", qPrintable(exe));
        }
        else
        {
            Ipv6GuardSession s;
            Ipv6GuardSession::TestHooks h;
            h.snapshot = [&]() { return QList<InterfaceInfo>{wifi}; };
            h.helperProgram = exe;
            h.launchWithoutElevation = true;
            h.helperTimeoutMs = 15000;
            s.setTestHooks(h);
            bool ok = true;
            QString msg;
            QElapsedTimer t;
            t.start();
            s.begin("OneForAll-VPNGate-test", {"2001:db8::1"});
            CHECK(waitReady(s, 20000, &ok, &msg));
            CHECK(!ok);
            CHECK(msg.contains("Administrator"));
            CHECK(s.state() == Ipv6GuardSession::State::Failed);
            std::printf("Tien trinh phu --ipv6-guard khong Admin: %lld ms -> %s\n", static_cast<long long>(t.elapsed()),
                        qPrintable(msg));
            // Tiến trình phụ tự thoát sau khi báo không chặn được (không ở lại vô ích): đếm tiến trình còn chạy với đúng
            // tham số này qua WMI là quá nặng - kiểm gián tiếp: kênh điều khiển đã đóng phía nó.
            spin(800);
            CHECK(s.state() == Ipv6GuardSession::State::Failed);
        }
    }

    // ---- PublicIpv6Checker: máy chủ HTTP GIẢ trên 127.0.0.1 (định tuyến theo đường dẫn) ----
    {
        CHECK(PublicIpv6Checker::parseEchoBody(R"({"ip":"2001:db8::7"})") == "2001:db8::7");
        CHECK(PublicIpv6Checker::parseEchoBody("2001:db8::8\n") == "2001:db8::8");
        CHECK(PublicIpv6Checker::parseEchoBody("  2001:DB8::9  ") == "2001:db8::9");
        CHECK(PublicIpv6Checker::parseEchoBody(R"({"ip":"203.0.113.7"})").isEmpty()); // IPv4 - không phải IPv6
        CHECK(PublicIpv6Checker::parseEchoBody("203.0.113.7").isEmpty());
        CHECK(PublicIpv6Checker::parseEchoBody("<html>error</html>").isEmpty());
        CHECK(PublicIpv6Checker::parseEchoBody("").isEmpty());
        CHECK(PublicIpv6Checker::parseEchoBody(QByteArray(5000, 'a')).isEmpty());

        QTcpServer server;
        CHECK(server.listen(QHostAddress::LocalHost, 0));
        QStringList paths;
        QObject::connect(&server, &QTcpServer::newConnection, [&] {
            while (QTcpSocket* socket = server.nextPendingConnection())
            {
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [socket, &paths] {
                    if (socket->property("answered").toBool() || !socket->peek(65536).contains("\r\n\r\n"))
                        return;
                    socket->setProperty("answered", true);
                    const QByteArray head = socket->readAll();
                    const QString path = QString::fromLatin1(head.split(' ').value(1));
                    paths << path;
                    int code = 200;
                    QByteArray body;
                    if (path.startsWith("/json"))
                        body = R"({"ip":"2001:db8::7"})";
                    else if (path.startsWith("/plain"))
                        body = "2001:db8::8\n";
                    else if (path.startsWith("/v4"))
                        body = "203.0.113.7";
                    else if (path.startsWith("/geo/"))
                        body = R"({"success":true,"country":"Testland","city":"Thu"})";
                    else if (path.startsWith("/geofail/"))
                        code = 500;
                    else
                        code = 500;
                    socket->write("HTTP/1.1 " + QByteArray::number(code) + (code == 200 ? " OK" : " ERR") +
                                  "\r\nContent-Type: text/plain\r\nConnection: close\r\nContent-Length: " +
                                  QByteArray::number(body.size()) + "\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
        // Cổng chắc chắn đóng: mở rồi đóng một máy chủ để lấy số cổng trống.
        quint16 closedPort = 0;
        {
            QTcpServer tmp;
            tmp.listen(QHostAddress::LocalHost, 0);
            closedPort = tmp.serverPort();
        }
        const QString base = QString("http://127.0.0.1:%1").arg(server.serverPort());
        const QUrl closed(QString("http://127.0.0.1:%1/json").arg(closedPort));
        auto run = [](PublicIpv6Checker& c) {
            PublicIpv6Info out;
            QEventLoop loop;
            bool got = false;
            const auto conn = QObject::connect(&c, &PublicIpv6Checker::result, &loop, [&](PublicIpv6Info i) {
                out = i;
                got = true;
                loop.quit();
            });
            QTimer::singleShot(8000, &loop, &QEventLoop::quit);
            c.check();
            if (!got)
                loop.exec();
            QObject::disconnect(conn);
            if (!got)
                out.error = "TIMEOUT";
            return out;
        };
        {
            PublicIpv6Checker c;
            c.setEndpointsForTest({QUrl(base + "/json")}, QUrl(base + "/geo/"), 2000);
            const PublicIpv6Info i = run(c);
            CHECK(i.reachable && i.ip == "2001:db8::7" && i.country == "Testland" && i.city == "Thu");
            CHECK(paths.contains("/geo/2001:db8::7"));
        }
        {
            // Điểm thứ nhất không tới được (cổng đóng) -> dùng điểm dự phòng (văn bản trơn)
            PublicIpv6Checker c;
            c.setEndpointsForTest({closed, QUrl(base + "/plain")}, QUrl(base + "/geo/"), 2000);
            const PublicIpv6Info i = run(c);
            CHECK(i.reachable && i.ip == "2001:db8::8" && i.country == "Testland");
        }
        {
            // Trả về IPv4 (không phải IPv6) ở mọi điểm -> coi là không có IPv6
            PublicIpv6Checker c;
            c.setEndpointsForTest({QUrl(base + "/v4"), QUrl(base + "/v4")}, QUrl(base + "/geo/"), 2000);
            const PublicIpv6Info i = run(c);
            CHECK(!i.reachable && i.ip.isEmpty() && !i.error.isEmpty() && i.error != "TIMEOUT");
        }
        {
            // Không điểm nào tới được (giống khi IPv6 bị chặn / máy không có IPv6) -> reachable=false, có lỗi
            PublicIpv6Checker c;
            c.setEndpointsForTest({closed, closed}, QUrl(base + "/geo/"), 2000);
            const PublicIpv6Info i = run(c);
            CHECK(!i.reachable && !i.error.isEmpty() && i.error != "TIMEOUT");
        }
        {
            // Tra vị trí lỗi: vẫn báo có IPv6 (đó mới là điều quan trọng), quốc gia để trống
            PublicIpv6Checker c;
            c.setEndpointsForTest({QUrl(base + "/json")}, QUrl(base + "/geofail/"), 2000);
            const PublicIpv6Info i = run(c);
            CHECK(i.reachable && i.ip == "2001:db8::7" && i.country.isEmpty());
        }
        {
            // Gọi check() hai lần liền: lần hai bị bỏ qua (đang chạy), chỉ một kết quả
            PublicIpv6Checker c;
            c.setEndpointsForTest({QUrl(base + "/json")}, QUrl(base + "/geo/"), 2000);
            int results = 0;
            QObject::connect(&c, &PublicIpv6Checker::result, [&](PublicIpv6Info) { ++results; });
            c.check();
            CHECK(c.isRunning());
            c.check();
            QEventLoop loop;
            QTimer::singleShot(1500, &loop, &QEventLoop::quit);
            loop.exec();
            CHECK(results == 1 && !c.isRunning());
        }
    }

    // ---- SplitTunneling trong danh sách VPN của Windows ----
    {
        QString err;
        const auto list = VpnController::internal::parseConnectionsJson(
            R"([{"Name":"A","ServerAddress":"a","TunnelType":"Sstp","ConnectionStatus":"Connected","SplitTunneling":true},
                {"Name":"B","ServerAddress":"b","TunnelType":"Ikev2","ConnectionStatus":"Disconnected","SplitTunneling":false},
                {"Name":"C","ServerAddress":"c","TunnelType":"Ikev2","ConnectionStatus":"Disconnected"}])",
            &err);
        CHECK(err.isEmpty() && list.size() == 3);
        if (list.size() == 3)
        {
            CHECK(list[0].splitTunneling);
            CHECK(!list[1].splitTunneling);
            CHECK(!list[2].splitTunneling); // thiếu trường (bản cũ) -> mặc định toàn bộ lưu lượng (an toàn: có chặn)
        }
        CHECK(VpnController::internal::buildListConnectionsScript().contains("SplitTunneling"));
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: khong co VPN that trong moi truong nay - cac ham goi PowerShell/RAS API that\n");
    std::printf("(listConnections/addConnection/removeConnection/VpnConnector::run thuc su chay) can\n");
    std::printf("duoc tu kiem tra tay tren may that co tai khoan VPN that.\n");
    std::printf("WireGuard: CHUA kiem chung importTunnel/removeTunnel/startTunnel/stopTunnel/listTunnels\n");
    std::printf("thuc su goi wireguard.exe/sc.exe - can may THAT da cai WireGuard for Windows + mot\n");
    std::printf("tunnel/may chu WireGuard thuc de tu kiem tra tay.\n");
    return g_fail == 0 ? 0 : 1;
}
