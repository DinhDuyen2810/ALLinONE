// Kiểm thử "khói" cho giao diện VPN & Location: dựng cửa sổ, không crash, widget chính tồn tại. CÓ gọi
// VpnController::listConnections() THẬT (chỉ đọc, an toàn - giống DiskSpaceInfo::listDrives()/
// PartitionManager::listPartitions()/AdbController::listDevices()) để xác nhận plumbing PowerShell
// (Get-VpnConnection) hoạt động được trên máy build; không có VPN nào đã khai báo thì danh sách rỗng
// vẫn là kết quả HỢP LỆ, không phải lỗi. KHÔNG BAO GIỜ tự động thêm/xóa/kết nối VPN thật trong test -
// đó là thao tác cần tài khoản VPN thật và cần người dùng tự kiểm tra tay.
#include <QApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include "TestDataDir.h"
#include <cstdio>

#include "core/AppPaths.h"

#include "tools/vpn/VpnControlWindow.h"
#include "tools/vpn/VpnTab.h"
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
