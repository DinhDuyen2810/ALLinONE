// Kiểm thử lõi VPN & Location. KHÔNG cần kết nối VPN thật hay quyền Administrator - chỉ kiểm thử lõi
// THUẦN (phân tích output Get-VpnConnection mẫu dựng sẵn, dựng script PowerShell) tách riêng khỏi phần
// gọi PowerShell/RAS API thật, giống mẫu đã dùng cho PartitionManager/AdbController.
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>

#include "core/AppPaths.h"
#include "core/PowerShellRunner.h"
#include "tools/vpn/engine/VpnConnector.h"
#include "tools/vpn/engine/VpnController.h"

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
    QTemporaryDir dataDir;
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

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: khong co VPN that trong moi truong nay - cac ham goi PowerShell/RAS API that\n");
    std::printf("(listConnections/addConnection/removeConnection/VpnConnector::run thuc su chay) can\n");
    std::printf("duoc tu kiem tra tay tren may that co tai khoan VPN that.\n");
    return g_fail == 0 ? 0 : 1;
}
