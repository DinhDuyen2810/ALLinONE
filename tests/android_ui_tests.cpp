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
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include "TestDataDir.h"
#include <QTimer>
#include <cstdio>

#include "core/AppPaths.h"
#include "tools/android/AndroidControlWindow.h"
#include "tools/android/DevicesTab.h"
#include "tools/android/WirelessPairDialog.h"
#include "tools/android/engine/AdbController.h"
#include "tools/android/engine/AdbDeviceLister.h"
#include "tools/android/engine/AdbPairer.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // Cửa sổ ghi log lúc mở (Logger) - ép dữ liệu vào thư mục tạm, không đụng %LOCALAPPDATA%\OneForAll thật.
    TestDataDir dataDir;
    AppPaths::setDataDirOverride(dataDir.path());

    // ---- Hộp thoại ghép đôi: chỉ đóng (Accepted) khi IP:Cổng + mã 6 số đúng định dạng ----
    {
        WirelessPairDialog dlg;
        const auto edits = dlg.findChildren<QLineEdit*>();
        CHECK(edits.size() == 2);
        if (edits.size() == 2)
        {
            edits[0]->setText("--help");
            edits[1]->setText("123456");
            dlg.accept();
            CHECK(dlg.result() != QDialog::Accepted); // bị từ chối, hộp thoại giữ nguyên

            edits[0]->setText("192.168.1.23:41234");
            edits[1]->setText("12345"); // thiếu 1 số
            dlg.accept();
            CHECK(dlg.result() != QDialog::Accepted);

            edits[1]->setText("123456");
            dlg.accept();
            CHECK(dlg.result() == QDialog::Accepted);
            CHECK(dlg.ipAndPort() == "192.168.1.23:41234" && dlg.pairingCode() == "123456");
        }
    }

    AndroidControlWindow win;
    win.setAttribute(Qt::WA_DontShowOnScreen, true);

    // Timer tự làm mới CHỈ chạy khi cửa sổ đang hiện (trước đây chạy từ constructor, không bao giờ dừng -
    // cửa sổ đã đóng/ẩn vẫn gọi adb.exe mỗi 3 giây).
    auto* refreshTimer = win.findChild<QTimer*>("autoRefreshTimer");
    CHECK(refreshTimer != nullptr);
    if (refreshTimer)
        CHECK(!refreshTimer->isActive()); // chưa hiện -> chưa chạy

    // Ghép đôi không dây chạy BẤT ĐỒNG BỘ qua AdbPairer (không còn gọi AdbController::pairWireless() đồng bộ
    // trên luồng giao diện); lúc mới dựng không có lệnh nào đang chạy và nút ghép đôi không bị khóa oan
    // (nút chỉ tắt khi thiếu adb.exe/scrcpy.exe đóng gói kèm).
    auto* pairer = win.findChild<AdbPairer*>("adbPairer");
    CHECK(pairer != nullptr);
    if (pairer)
        CHECK(!pairer->isBusy());
    {
        QPushButton* pairBtn = nullptr;
        for (QPushButton* b : win.findChildren<QPushButton*>())
            if (b->text().contains(QString::fromUtf8("Ghép đôi không dây")))
                pairBtn = b;
        CHECK(pairBtn != nullptr);
        if (pairBtn)
            CHECK(pairBtn->isEnabled() == AdbController::isBundleAvailable());
    }

    QElapsedTimer showTimer;
    showTimer.start();
    win.show();
    app.processEvents();
    // Hiện cửa sổ KHÔNG được chặn luồng giao diện chờ adb (trước đây `adb devices -l` chạy đồng bộ ngay
    // trong constructor - lần đầu phải chờ daemon adb khởi động vài giây).
    CHECK(showTimer.elapsed() < 3000);
    if (refreshTimer)
        CHECK(refreshTimer->isActive());
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

        // Nhãn trạng thái hiện chuỗi từ adb/thiết bị phải ở chế độ văn bản thuần (nhãn duy nhất của tab
        // đặt Qt::PlainText tường minh).
        bool hasPlainStatusLabel = false;
        for (auto* label : devicesTab->findChildren<QLabel*>())
            if (label->textFormat() == Qt::PlainText)
                hasPlainStatusLabel = true;
        CHECK(hasPlainStatusLabel);
    }

    // Làm mới danh sách: lựa chọn đi theo THIẾT BỊ (số serial), không theo số hàng. Trước đây rút thiết bị
    // đang chọn thì lựa chọn âm thầm nhảy sang thiết bị vừa dồn lên đúng hàng đó.
    if (devicesTab)
    {
        auto* table = devicesTab->findChild<QTableWidget*>();
        auto* lister = devicesTab->findChild<AdbDeviceLister*>();
        CHECK(table && lister);
        if (table && lister)
        {
            const auto device = [](const QString& serial) {
                AndroidDeviceInfo d;
                d.serial = serial;
                d.state = "device";
                return d;
            };
            const auto selectedSerial = [table]() {
                const auto rows = table->selectionModel()->selectedRows();
                return rows.size() == 1 ? table->item(rows.first().row(), 0)->text() : QString();
            };
            emit lister->listed({device("AAA"), device("BBB")}, QString());
            table->selectRow(0);
            CHECK(selectedSerial() == "AAA");
            emit lister->listed({device("BBB"), device("AAA")}, QString()); // đổi thứ tự
            CHECK(selectedSerial() == "AAA");
            emit lister->listed({device("BBB")}, QString());                // AAA bị rút
            CHECK(selectedSerial().isEmpty());
            emit lister->listed({}, QString());
            CHECK(table->rowCount() == 0);
        }
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

    // Ẩn/đóng cửa sổ: timer dừng; mở lại: chạy tiếp.
    if (refreshTimer)
    {
        win.hide();
        app.processEvents();
        CHECK(!refreshTimer->isActive());
        win.show();
        app.processEvents();
        CHECK(refreshTimer->isActive());
        win.close(); // closeEvent -> stopBackgroundWork(): dừng timer + lệnh adb dở + daemon adb đóng gói kèm
        app.processEvents();
        CHECK(!refreshTimer->isActive());
        if (bundleAvailable)
            CHECK(AdbController::stopBundledAdbServer() == 0); // closeEvent đã dọn hết, không còn gì để dừng
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: ghep doi khong day / chuyen sang khong day / khoi chay dieu khien (scrcpy) can\n");
    std::printf("thiet bi Android that va phai tu kiem tra tay - khong phu hop de chay trong test tu dong.\n");
    return g_fail == 0 ? 0 : 1;
}
