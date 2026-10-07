// Kiểm thử lõi VPN & Location. KHÔNG cần kết nối VPN thật hay quyền Administrator - chỉ kiểm thử lõi
// THUẦN (phân tích output Get-VpnConnection mẫu dựng sẵn, dựng script PowerShell) tách riêng khỏi phần
// gọi PowerShell/rasdial thật, giống mẫu đã dùng cho PartitionManager/AdbController.
#include <QCoreApplication>
#include <cstdio>

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

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: khong co VPN that trong moi truong nay - cac ham goi PowerShell/rasdial that\n");
    std::printf("(listConnections/addConnection/removeConnection/VpnConnector::run thuc su chay) can\n");
    std::printf("duoc tu kiem tra tay tren may that co tai khoan VPN that.\n");
    return g_fail == 0 ? 0 : 1;
}
