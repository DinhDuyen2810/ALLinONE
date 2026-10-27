// Kiểm thử "khói" cho giao diện VPN & Location: dựng cửa sổ, không crash, widget chính tồn tại. CÓ gọi
// VpnController::listConnections() THẬT (chỉ đọc, an toàn - giống DiskSpaceInfo::listDrives()/
// PartitionManager::listPartitions()/AdbController::listDevices()) để xác nhận plumbing PowerShell
// (Get-VpnConnection) hoạt động được trên máy build; không có VPN nào đã khai báo thì danh sách rỗng
// vẫn là kết quả HỢP LỆ, không phải lỗi. KHÔNG BAO GIỜ tự động thêm/xóa/kết nối VPN thật trong test -
// đó là thao tác cần tài khoản VPN thật và cần người dùng tự kiểm tra tay.
#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include "TestDataDir.h"
#include <cstdio>

#include "core/AppPaths.h"

#include "tools/vpn/AddVpnProfileDialog.h"
#include "tools/vpn/ThirdPartyVpnTab.h"
#include "tools/vpn/VpnControlWindow.h"
#include "tools/vpn/VpnTab.h"
#include "tools/vpn/engine/DirectProfileStore.h"
#include "tools/vpn/engine/VpnController.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // Log + vpn_profiles.json ghi vào thư mục tạm - KHÔNG đụng dữ liệu thật của người dùng.
    TestDataDir dataDir;
    AppPaths::setDataDirOverride(dataDir.path());

    VpnControlWindow win;
    win.setAttribute(Qt::WA_DontShowOnScreen, true);
    win.show();
    app.processEvents();
    CHECK(true); // tới được đây tức là dựng cửa sổ không crash

    auto* vpnTab = win.findChild<VpnTab*>();
    CHECK(vpnTab != nullptr);
    if (vpnTab)
    {
        auto* table = vpnTab->findChild<QTableWidget*>();
        CHECK(table != nullptr && table->columnCount() == 4);

        // Danh sách hồ sơ nay được đọc trên luồng NỀN (Get-VpnConnection không còn làm đứng giao diện lúc
        // mở cửa sổ) - bảng bị khóa trong lúc chờ và phải được mở lại khi xong.
        QElapsedTimer loadTimer;
        loadTimer.start();
        while (table && !table->isEnabled() && loadTimer.elapsed() < 60000)
            app.processEvents(QEventLoop::AllEvents, 50);
        CHECK(table != nullptr && table->isEnabled());

        const auto buttons = vpnTab->findChildren<QPushButton*>();
        CHECK(buttons.size() >= 5); // Làm mới x2, Thêm, Xóa, Kết nối (ít nhất)

        const auto labels = vpnTab->findChildren<QLabel*>();
        CHECK(labels.size() >= 2); // vị trí IP + thành phố/quốc gia
    }

    // ---- Hộp thoại "Thêm hồ sơ VPN" có 3 chế độ: Thường / GlobalProtect / FortiClient ----
    {
        AddVpnProfileDialog dlg;
        dlg.setAttribute(Qt::WA_DontShowOnScreen, true);
        dlg.show();
        app.processEvents();
        auto* modeCombo = dlg.findChild<QComboBox*>("modeCombo");
        auto* nameEdit = dlg.findChild<QLineEdit*>("nameEdit");
        auto* gpPortal = dlg.findChild<QLineEdit*>("gpPortalEdit");
        auto* fortiType = dlg.findChild<QComboBox*>("fortiTypeCombo");
        auto* fortiServer = dlg.findChild<QLineEdit*>("fortiServerEdit");
        auto* l2tpPsk = dlg.findChild<QLineEdit*>("l2tpPskEdit");
        auto* errorLabel = dlg.findChild<QLabel*>("errorLabel");
        CHECK(modeCombo && nameEdit && gpPortal && fortiType && fortiServer && l2tpPsk && errorLabel);
        if (modeCombo && nameEdit && gpPortal && fortiType && fortiServer && l2tpPsk && errorLabel)
        {
            CHECK(modeCombo->count() == 3); // đúng 3 chế độ
            CHECK(modeCombo->itemText(1) == "GlobalProtect" && modeCombo->itemText(2) == "FortiClient");
            // Mặc định: Thường - chỉ hiện ô của VPN Windows.
            CHECK(dlg.mode() == AddVpnProfileDialog::Mode::Normal);
            CHECK(l2tpPsk->isVisible() && !gpPortal->isVisible() && !fortiServer->isVisible() && !fortiType->isVisible());
            // Thường: để trống thì KHÔNG đóng được, báo lỗi ngay trong hộp thoại.
            dlg.accept();
            CHECK(dlg.result() != QDialog::Accepted && errorLabel->isVisible());

            // GlobalProtect: chỉ hiện ô portal.
            modeCombo->setCurrentIndex(1);
            app.processEvents();
            CHECK(dlg.mode() == AddVpnProfileDialog::Mode::GlobalProtect);
            CHECK(gpPortal->isVisible() && !l2tpPsk->isVisible() && !fortiServer->isVisible() && !fortiType->isVisible());
            CHECK(!errorLabel->isVisible()); // đổi chế độ xóa lỗi cũ
            nameEdit->setText("  Cong ty GP ");
            gpPortal->setText("  https://VPN.Congty.vn/global-protect ");
            const DirectVpnProfile gp = dlg.directProfile();
            CHECK(gp.protocol == DirectProtocol::GlobalProtect && gp.name == "Cong ty GP");
            CHECK(DirectProfileStore::normalizeServer(gp.server) == "vpn.congty.vn");
            // Sai địa chỉ -> không đóng.
            gpPortal->setText("vpn cong ty");
            dlg.accept();
            CHECK(dlg.result() != QDialog::Accepted && errorLabel->isVisible());
            gpPortal->setText("vpn.congty.vn");
            dlg.accept();
            CHECK(dlg.result() == QDialog::Accepted);
        }

        // FortiClient: SSL-VPN (mặc định) và IPsec.
        AddVpnProfileDialog dlg2;
        dlg2.setAttribute(Qt::WA_DontShowOnScreen, true);
        dlg2.show();
        app.processEvents();
        auto* mode2 = dlg2.findChild<QComboBox*>("modeCombo");
        auto* name2 = dlg2.findChild<QLineEdit*>("nameEdit");
        auto* type2 = dlg2.findChild<QComboBox*>("fortiTypeCombo");
        auto* server2 = dlg2.findChild<QLineEdit*>("fortiServerEdit");
        auto* gp2 = dlg2.findChild<QLineEdit*>("gpPortalEdit");
        CHECK(mode2 && name2 && type2 && server2 && gp2);
        if (mode2 && name2 && type2 && server2 && gp2)
        {
            mode2->setCurrentIndex(2);
            app.processEvents();
            CHECK(dlg2.mode() == AddVpnProfileDialog::Mode::Forticlient);
            CHECK(server2->isVisible() && type2->isVisible() && !gp2->isVisible());
            name2->setText("VPN_CVTLT");
            server2->setText("vpn.congty.vn:10443");
            CHECK(dlg2.directProfile().protocol == DirectProtocol::FortinetSsl);
            CHECK(server2->placeholderText().contains("10443")); // gợi ý cổng cho SSL-VPN
            type2->setCurrentIndex(1);
            app.processEvents();
            CHECK(dlg2.directProfile().protocol == DirectProtocol::FortinetIpsec);
            CHECK(!server2->placeholderText().contains("10443")); // IPsec: không có cổng
            // IPsec không nhận cổng -> không đóng; bỏ cổng thì đóng được.
            dlg2.accept();
            CHECK(dlg2.result() != QDialog::Accepted);
            server2->setText("119.17.206.59");
            dlg2.accept();
            CHECK(dlg2.result() == QDialog::Accepted);
        }
    }

    // ---- Hồ sơ tự thêm hiện ở tab "VPN đã cài sẵn"; thêm ở tab "VPN hệ thống" thì cửa sổ chuyển tab + chọn hồ sơ ----
    {
        QString addErr;
        CHECK(DirectProfileStore::add({"Test GP tu them", "gp.test.example", DirectProtocol::GlobalProtect}, &addErr));
        VpnControlWindow win2;
        win2.setAttribute(Qt::WA_DontShowOnScreen, true);
        win2.show();
        app.processEvents();
        auto* tabs = win2.findChild<QTabWidget*>();
        auto* third = win2.findChild<ThirdPartyVpnTab*>();
        auto* vpnTab2 = win2.findChild<VpnTab*>();
        CHECK(tabs && third && vpnTab2);
        if (tabs && third && vpnTab2)
        {
            auto* table = third->findChild<QTableWidget*>();
            CHECK(table != nullptr);
            int row = -1;
            for (int r = 0; table && r < table->rowCount(); ++r)
                if (table->item(r, 1) && table->item(r, 1)->text() == "Test GP tu them")
                    row = r;
            CHECK(row >= 0);
            if (table && row >= 0)
            {
                CHECK(table->item(row, 0)->text().contains("tự thêm") && table->item(row, 0)->text().contains("GlobalProtect"));
                CHECK(table->item(row, 2)->text() == "gp.test.example");
            }
            // Tín hiệu từ tab "VPN hệ thống" -> chuyển sang tab "VPN đã cài sẵn" và chọn đúng hồ sơ; nút xóa hiện ra.
            tabs->setCurrentWidget(vpnTab2);
            emit vpnTab2->directProfileAdded("Test GP tu them", static_cast<int>(DirectProtocol::GlobalProtect));
            app.processEvents();
            CHECK(tabs->currentWidget() == third);
            CHECK(table && table->currentRow() == row && row >= 0);
            QPushButton* removeBtn = nullptr;
            for (QPushButton* b : third->findChildren<QPushButton*>())
                if (b->text().contains("Xóa hồ sơ tự thêm"))
                    removeBtn = b;
            CHECK(removeBtn != nullptr && removeBtn->isVisible());
            // Chọn một dòng KHÔNG phải tự thêm (nếu máy có hồ sơ quét được) thì nút xóa ẩn.
            for (int r = 0; table && removeBtn && r < table->rowCount(); ++r)
                if (table->item(r, 0) && !table->item(r, 0)->text().contains("tự thêm"))
                {
                    table->selectRow(r);
                    app.processEvents();
                    CHECK(!removeBtn->isVisible());
                    break;
                }
        }
        CHECK(DirectProfileStore::remove(DirectProtocol::GlobalProtect, "Test GP tu them"));
    }

    // Chỉ đọc, an toàn - liệt kê kết nối VPN Windows THẬT trên máy build (có thể rỗng nếu chưa khai
    // báo VPN nào, đó vẫn là kết quả hợp lệ).
    QString error;
    const auto connections = VpnController::listConnections(&error);
    std::printf("Get-VpnConnection that: tim thay %d ket noi%s%s\n", static_cast<int>(connections.size()),
               error.isEmpty() ? "" : " (loi: ", error.isEmpty() ? "" : qPrintable(error + ")"));
    for (const auto& c : connections)
        std::printf("  - %s [%s] %s\n", qPrintable(c.name), qPrintable(c.connectionStatus), qPrintable(c.tunnelType));
    CHECK(true); // tới được đây tức là gọi PowerShell thật không crash

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: them/xoa/ket noi/ngat ket noi VPN that can tai khoan VPN that va phai tu kiem\n");
    std::printf("tra tay - khong phu hop de chay trong test tu dong.\n");
    return g_fail == 0 ? 0 : 1;
}
