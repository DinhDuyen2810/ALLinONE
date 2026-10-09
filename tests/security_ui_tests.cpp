// Kiểm thử "khói" cho giao diện Security Gateway: dựng cửa sổ, không crash, đủ 3 tab. CÓ gọi
// DefenderController::getStatus()/HostsBlocklist::listBlockedDomains() THẬT (chỉ đọc, an toàn - giống
// VpnController::listConnections()/PartitionManager::listPartitions()/AdbController::listDevices()) để
// xác nhận plumbing PowerShell/hosts file hoạt động được trên máy build. KHÔNG BAO GIỜ tự động bật/tắt
// bảo vệ thật, quét máy thật, xóa mối đe dọa thật, sửa hosts file thật, hay mở PowerShell/dán lệnh thật
// trong test tự động - những thao tác đó đổi trạng thái hệ thống thật và/hoặc cần quyền Administrator,
// phải được tự kiểm tra tay.
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <cstdio>

#include "core/AppPaths.h"

#include "tools/security/SecurityGatewayWindow.h"
#include "tools/security/engine/DefenderController.h"
#include "tools/security/engine/HostsBlocklist.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // Cửa sổ ghi log khi mở - ép dữ liệu vào thư mục tạm để test không ghi vào log thật của người dùng.
    QTemporaryDir dataDir;
    AppPaths::setDataDirOverride(dataDir.path());

    SecurityGatewayWindow win;
    win.setAttribute(Qt::WA_DontShowOnScreen, true);
    win.show();
    app.processEvents();
    CHECK(true); // tới được đây tức là dựng cửa sổ không crash

    auto* tabs = win.findChild<QTabWidget*>();
    CHECK(tabs != nullptr && tabs->count() == 3);

    const auto buttons = win.findChildren<QPushButton*>();
    CHECK(buttons.size() >= 5); // Kiểm tra lệnh, Mở PowerShell, Làm mới x2, bật/tắt bảo vệ, quét...

    const auto labels = win.findChildren<QLabel*>();
    CHECK(labels.size() >= 3);

    // Chỉ đọc, an toàn - đọc trạng thái Windows Defender THẬT trên máy build.
    QString error;
    const auto status = DefenderController::getStatus(&error);
    std::printf("Get-MpComputerStatus that: AntivirusEnabled=%d RealTime=%d AMRunningMode=%s%s%s\n",
               status.antivirusEnabled, status.realTimeProtectionEnabled, qPrintable(status.runningMode),
               error.isEmpty() ? "" : " loi=", error.isEmpty() ? "" : qPrintable(error));
    CHECK(true); // tới được đây tức là gọi PowerShell thật không crash

    // Chỉ đọc, an toàn - đọc hosts file THẬT trên máy build (danh sách chặn tùy chỉnh có thể rỗng).
    QString hostsError;
    const auto blocked = HostsBlocklist::listBlockedDomains(&hostsError);
    std::printf("Danh sach chan tuy chinh that: %d ten mien\n", static_cast<int>(blocked.size()));
    CHECK(true);

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: bat/tat bao ve that, quet may that, xoa moi de doa that, sua hosts file that,\n");
    std::printf("va mo PowerShell/dan lenh that KHONG duoc tu dong hoa trong test nay - can tu kiem tra tay.\n");
    return g_fail == 0 ? 0 : 1;
}
