// Kiểm thử lõi VPN & Location. KHÔNG cần kết nối VPN thật hay quyền Administrator - chỉ kiểm thử lõi
// THUẦN (phân tích output Get-VpnConnection mẫu dựng sẵn, dựng script PowerShell) tách riêng khỏi phần
// gọi PowerShell/RAS API thật, giống mẫu đã dùng cho PartitionManager/AdbController.
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include "TestDataDir.h"
#include <cstdio>

#include "core/AppPaths.h"
#include "core/PowerShellRunner.h"
#include "tools/vpn/engine/PublicIpChecker.h"
#include "tools/vpn/engine/VpnConnector.h"
#include "tools/vpn/engine/VpnController.h"
#include "tools/vpn/engine/VpnGateCache.h"
#include "tools/vpn/engine/VpnGateClient.h"
#include "tools/vpn/engine/ConnectivityTester.h"
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

        // Đã kết nối luôn thắng; phiên bị Windows đóng (ERROR_INVALID_HANDLE = 6) là thất bại
        poll = Poll{};
        poll.connState = VpnConnector::kRasStateConnected;
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Connected);
        poll = Poll{};
        poll.callbackConnected = true;
        poll.statusResult = 6;
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Connected);
        poll = Poll{};
        poll.statusResult = 6;
        CHECK(VpnConnector::evaluateDialPoll(poll, &msg) == Outcome::Failed);
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

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: khong co VPN that trong moi truong nay - cac ham goi PowerShell/RAS API that\n");
    std::printf("(listConnections/addConnection/removeConnection/VpnConnector::run thuc su chay) can\n");
    std::printf("duoc tu kiem tra tay tren may that co tai khoan VPN that.\n");
    std::printf("WireGuard: CHUA kiem chung importTunnel/removeTunnel/startTunnel/stopTunnel/listTunnels\n");
    std::printf("thuc su goi wireguard.exe/sc.exe - can may THAT da cai WireGuard for Windows + mot\n");
    std::printf("tunnel/may chu WireGuard thuc de tu kiem tra tay.\n");
    return g_fail == 0 ? 0 : 1;
}
