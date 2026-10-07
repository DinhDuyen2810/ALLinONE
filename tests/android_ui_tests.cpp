// Kiểm thử "khói" cho giao diện Android Phone Control: dựng cửa sổ, không crash, widget chính tồn
// tại. Nếu thư mục "scrcpy" (adb.exe/scrcpy.exe đóng gói) có cạnh file test đang chạy, CÓ gọi
// AdbController::listDevices() thật (chỉ đọc, an toàn - giống DiskSpaceInfo::listDrives()/
// PartitionManager::listPartitions()) để xác nhận plumbing adb thật hoạt động được trên máy build;
// không có thiết bị Android thật thì danh sách rỗng vẫn là kết quả HỢP LỆ, không phải lỗi. KHÔNG BAO
// GIỜ tự động ghép đôi/kết nối/khởi chạy scrcpy thật trong test - đó là thao tác cần thiết bị thật và
// cần người dùng tự kiểm tra tay.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <cstdio>

#include "tools/android/AndroidControlWindow.h"
#include "tools/android/DevicesTab.h"
#include "tools/android/engine/AdbController.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    AndroidControlWindow win;
    win.setAttribute(Qt::WA_DontShowOnScreen, true);
    win.show();
    app.processEvents();
    CHECK(true); // tới được đây tức là dựng cửa sổ không crash

    auto* devicesTab = win.findChild<DevicesTab*>();
    CHECK(devicesTab != nullptr);
    if (devicesTab)
    {
        auto* table = devicesTab->findChild<QTableWidget*>();
        CHECK(table != nullptr && table->columnCount() == 4);

        const auto buttons = devicesTab->findChildren<QPushButton*>();
        CHECK(buttons.size() >= 4); // Làm mới + Ghép đôi + Chuyển không dây + Điều khiển (ít nhất)

        auto* stayAwakeCheck = devicesTab->findChild<QCheckBox*>();
        CHECK(stayAwakeCheck != nullptr && stayAwakeCheck->isChecked()); // "Giữ màn hình sáng" mặc định bật

        const auto combos = devicesTab->findChildren<QComboBox*>();
        CHECK(combos.size() >= 2); // Độ phân giải + FPS tối đa
    }

    QString missing;
    const bool bundleAvailable = AdbController::isBundleAvailable(&missing);
    std::printf("Goi scrcpy (adb.exe/scrcpy.exe) %s%s\n", bundleAvailable ? "DA SAN SANG" : "CHUA CO - thieu: ",
               bundleAvailable ? "" : qPrintable(missing));

    if (bundleAvailable)
    {
        // Chỉ đọc, an toàn - liệt kê thiết bị adb thấy THẬT trên máy build (có thể rỗng nếu không cắm
        // điện thoại, đó vẫn là kết quả hợp lệ).
        QString error;
        const auto devices = AdbController::listDevices(&error);
        std::printf("adb devices that: tim thay %d thiet bi%s%s\n", static_cast<int>(devices.size()),
                   error.isEmpty() ? "" : " (loi: ", error.isEmpty() ? "" : qPrintable(error + ")"));
        for (const auto& d : devices)
            std::printf("  - %s [%s] %s\n", qPrintable(d.serial), qPrintable(d.state), qPrintable(d.model));
        CHECK(true); // tới được đây tức là gọi adb thật không crash
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: ghep doi khong day / chuyen sang khong day / khoi chay dieu khien (scrcpy) can\n");
    std::printf("thiet bi Android that va phai tu kiem tra tay - khong phu hop de chay trong test tu dong.\n");
    return g_fail == 0 ? 0 : 1;
}
