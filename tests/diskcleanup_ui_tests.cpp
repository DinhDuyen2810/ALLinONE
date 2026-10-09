// Kiểm thử "khói" cho giao diện Disk Cleanup: dựng cửa sổ, không crash, các widget chính tồn tại.
// KHÔNG bấm nút Quét thật ở CleanupTab/LargeFilesTab/DuplicateFilesTab: hạng mục dọn dẹp trỏ vào
// %TEMP%/%WINDIR% thật và "Tìm tệp lớn"/"Tìm tệp trùng lặp" cần người dùng chọn thư mục trước - đụng
// vào hệ thống thật của máy build, không phù hợp cho test tự động (giống nguyên tắc đã áp dụng cho
// wifi_ui_tests: quét/kết nối thật phải tự kiểm tra tay). PartitionTab thì khác: liệt kê phân vùng
// (listPartitions) chỉ ĐỌC và AN TOÀN (giống DiskSpaceInfo::listDrives()), nên được gọi thật ngay khi
// dựng UI - test xác nhận nó thực sự chạy được trên máy build. KHÔNG bao giờ test đổi kích thước thật.
// Build: cmake --build build --target diskcleanup_ui_tests && build\diskcleanup_ui_tests.exe
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include "TestDataDir.h"
#include <QTreeWidget>
#include <cstdio>

#include "core/AppPaths.h"

#include "tools/diskcleanup/CleanupTab.h"
#include "tools/diskcleanup/DiskCleanupWindow.h"
#include "tools/diskcleanup/DuplicateFilesTab.h"
#include "tools/diskcleanup/LargeFilesTab.h"
#include "tools/diskcleanup/PartitionTab.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // Log của ứng dụng (Logger) ghi vào thư mục tạm - KHÔNG đụng dữ liệu thật của người dùng.
    TestDataDir dataDir;
    AppPaths::setDataDirOverride(dataDir.path());

    DiskCleanupWindow win;
    win.setAttribute(Qt::WA_DontShowOnScreen, true);
    win.show();
    app.processEvents();
    CHECK(true); // tới được đây tức là dựng cửa sổ không crash

    auto* tabs = win.findChild<QTabWidget*>();
    CHECK(tabs != nullptr && tabs->count() == 4);

    auto* cleanupTab = win.findChild<CleanupTab*>();
    CHECK(cleanupTab != nullptr);
    if (cleanupTab)
    {
        auto* table = cleanupTab->findChild<QTableWidget*>();
        CHECK(table != nullptr);
        // Ngay sau khi dựng, bảng rỗng vì chưa bấm Quét (xem lý do ở đầu file)
        CHECK(table != nullptr && table->rowCount() == 0);

        const auto buttons = cleanupTab->findChildren<QPushButton*>();
        CHECK(buttons.size() >= 2); // ít nhất nút Quét + nút Dọn dẹp

        auto* permanentCheck = cleanupTab->findChild<QCheckBox*>();
        CHECK(permanentCheck != nullptr && !permanentCheck->isChecked()); // mặc định KHÔNG xóa vĩnh viễn

        auto* progress = cleanupTab->findChild<QProgressBar*>();
        CHECK(progress != nullptr && !progress->isVisible()); // chưa quét thì chưa hiện progress bar
    }

    if (tabs) { tabs->setCurrentIndex(1); app.processEvents(); }

    auto* largeFilesTab = win.findChild<LargeFilesTab*>();
    CHECK(largeFilesTab != nullptr);
    if (largeFilesTab)
    {
        auto* table = largeFilesTab->findChild<QTableWidget*>();
        CHECK(table != nullptr && table->rowCount() == 0);
    }

    if (tabs) { tabs->setCurrentIndex(2); app.processEvents(); }

    auto* dupTab = win.findChild<DuplicateFilesTab*>();
    CHECK(dupTab != nullptr);
    if (dupTab)
    {
        auto* tree = dupTab->findChild<QTreeWidget*>();
        CHECK(tree != nullptr && tree->topLevelItemCount() == 0); // chưa quét -> rỗng

        const auto buttons = dupTab->findChildren<QPushButton*>();
        CHECK(buttons.size() >= 2); // Chọn thư mục + Quét (+ Dừng/Xóa ẩn sẵn)

        // ---- Mỗi nhóm trùng lặp LUÔN phải giữ lại ít nhất 1 bản (chặn trong code, không chỉ lời nhắn) ----
        // Nạp một kết quả dựng sẵn (đường dẫn không tồn tại - KHÔNG có gì bị xóa, không bấm nút Xóa).
        if (tree)
        {
            DuplicateGroup group;
            group.paths = {"Z:/khong_ton_tai/a.bin", "Z:/khong_ton_tai/b.bin", "Z:/khong_ton_tai/c.bin"};
            group.lastWriteTimes = {1, 2, 3};
            group.sizeEachBytes = 5000;
            group.hashHex = "00";
            dupTab->onScanFinished({group}, group.wastedBytes(), 1);
            app.processEvents();

            CHECK(tree->topLevelItemCount() == 1);
            QTreeWidgetItem* groupItem = tree->topLevelItemCount() == 1 ? tree->topLevelItem(0) : nullptr;
            if (groupItem && groupItem->childCount() == 3)
            {
                QTreeWidgetItem* a = groupItem->child(0);
                QTreeWidgetItem* b = groupItem->child(1);
                QTreeWidgetItem* c = groupItem->child(2);
                // Mặc định: giữ bản đầu, tick sẵn các bản còn lại
                CHECK(a->checkState(0) == Qt::Unchecked);
                CHECK(b->checkState(0) == Qt::Checked && c->checkState(0) == Qt::Checked);

                // Tick nốt bản cuối cùng còn giữ -> bị hoàn lại ngay (không thể chọn xóa cả nhóm)
                a->setCheckState(0, Qt::Checked);
                app.processEvents();
                CHECK(a->checkState(0) == Qt::Unchecked);

                // Bỏ tick b (b thành bản giữ lại) thì tick a được; rồi tick lại b -> bị hoàn lại
                b->setCheckState(0, Qt::Unchecked);
                a->setCheckState(0, Qt::Checked);
                app.processEvents();
                CHECK(a->checkState(0) == Qt::Checked);
                b->setCheckState(0, Qt::Checked);
                app.processEvents();
                CHECK(b->checkState(0) == Qt::Unchecked);

                int kept = 0;
                for (int i = 0; i < groupItem->childCount(); ++i)
                    if (groupItem->child(i)->checkState(0) != Qt::Checked)
                        ++kept;
                CHECK(kept >= 1);
            }
            else
            {
                CHECK(false); // cây không dựng đúng 3 dòng con
            }
        }
    }

    if (tabs) { tabs->setCurrentIndex(3); app.processEvents(); }

    auto* partTab = win.findChild<PartitionTab*>();
    CHECK(partTab != nullptr);
    if (partTab)
    {
        // listPartitions() chạy THẬT ngay khi dựng UI (chỉ đọc, an toàn) - máy Windows nào cũng có
        // ít nhất phân vùng chứa Windows, nên bảng phải có dữ liệu.
        auto* table = partTab->findChild<QTableWidget*>();
        CHECK(table != nullptr);
        // Danh sách phân vùng nay được đọc trên luồng NỀN (không còn làm đứng giao diện lúc mở cửa sổ) -
        // chờ tới khi xong (tối đa 60 giây: lần đầu PowerShell nạp module Storage có thể chậm).
        QElapsedTimer loadTimer;
        loadTimer.start();
        while (partTab->isLoading() && loadTimer.elapsed() < 60000)
            app.processEvents(QEventLoop::AllEvents, 50);
        CHECK(!partTab->isLoading());
        if (table)
        {
            std::printf("PartitionTab: liet ke duoc %d phan vung\n", table->rowCount());
            CHECK(table->rowCount() > 0);
            CHECK(table->columnCount() == 7);
        }

        // Chưa chọn dòng nào -> khu vực đổi kích thước phải đang ẩn (không bao giờ lộ sẵn thao tác nguy hiểm)
        auto* progress = partTab->findChild<QProgressBar*>();
        CHECK(progress != nullptr && !progress->isVisible());

        const auto buttons = partTab->findChildren<QPushButton*>();
        CHECK(buttons.size() >= 2); // ít nhất Làm mới + (Chạy lại quyền Quản trị, nếu chưa elevated)

        // ---- Đổi dòng chọn phải XÓA SẠCH trạng thái của lần tra trước ----
        // Hồi quy lỗi thật: chọn ổ A -> tra -> gõ xác nhận (nút bật) -> bấm sang dòng ổ B: nút vẫn bật cùng
        // khoảng kích thước của A, bấm là đổi kích thước ổ B theo số liệu của ổ A. Test CHỈ gán một khoảng
        // kích thước GIẢ qua applySupportedRangeForTest() và đọc trạng thái widget - KHÔNG BAO GIỜ bấm nút
        // "Đổi kích thước" (không có lệnh PowerShell đổi phân vùng nào được dựng hay chạy ở đây).
        auto* resizeBtn = partTab->findChild<QPushButton*>("partitionResizeButton");
        auto* confirmEdit = partTab->findChild<QLineEdit*>("partitionConfirmEdit");
        auto* sizeSpin = partTab->findChild<QDoubleSpinBox*>("partitionNewSizeSpin");
        CHECK(resizeBtn && confirmEdit && sizeSpin);
        if (table && resizeBtn && confirmEdit && sizeSpin && table->rowCount() > 0)
        {
            CHECK(resizeBtn->isHidden() && !resizeBtn->isEnabled());
            CHECK(!partTab->resizeRequestBlockReason().isEmpty()); // chưa chọn gì

            table->selectRow(0);
            app.processEvents();
            CHECK(resizeBtn->isHidden());
            CHECK(!partTab->resizeRequestBlockReason().isEmpty()); // đã chọn nhưng chưa tra

            PartitionManager::SupportedSizeRange fakeRange;
            fakeRange.ok = true;
            fakeRange.minBytes = 64LL * 1024 * 1024;
            fakeRange.maxBytes = 1024LL * 1024 * 1024 * 1024;
            partTab->applySupportedRangeForTest(fakeRange);
            CHECK(!resizeBtn->isHidden());
            CHECK(!resizeBtn->isEnabled()); // chưa gõ xác nhận
            CHECK(!confirmEdit->isHidden() && !sizeSpin->isHidden());

            const QString token = confirmEdit->placeholderText();
            CHECK(!token.isEmpty());
            confirmEdit->setText(token + "x");
            CHECK(!resizeBtn->isEnabled()); // gõ sai
            confirmEdit->setText(token);
            CHECK(resizeBtn->isEnabled());
            CHECK(partTab->resizeRequestBlockReason().isEmpty());

            if (table->rowCount() >= 2)
            {
                table->selectRow(1); // sang phân vùng KHÁC
                app.processEvents();
                CHECK(!resizeBtn->isEnabled());
                CHECK(resizeBtn->isHidden());
                CHECK(confirmEdit->text().isEmpty() && confirmEdit->isHidden());
                CHECK(sizeSpin->isHidden());
                CHECK(!partTab->resizeRequestBlockReason().isEmpty());

                // Gõ lại đúng chuỗi cũ khi chưa tra cho phân vùng mới -> vẫn bị chặn
                confirmEdit->setText(token);
                CHECK(!resizeBtn->isEnabled());
                CHECK(!partTab->resizeRequestBlockReason().isEmpty());

                // Quay lại dòng cũ: cũng phải tra lại từ đầu
                table->selectRow(0);
                app.processEvents();
                CHECK(!resizeBtn->isEnabled() && resizeBtn->isHidden());
                CHECK(confirmEdit->text().isEmpty());
                CHECK(!partTab->resizeRequestBlockReason().isEmpty());
            }
            else
            {
                std::printf("LUU Y: may chi co 1 phan vung - bo qua kiem tra doi dong chon\n");
            }

            // Bỏ chọn hẳn cũng xóa trạng thái
            partTab->applySupportedRangeForTest(fakeRange);
            confirmEdit->setText(confirmEdit->placeholderText());
            CHECK(resizeBtn->isEnabled());
            table->clearSelection();
            app.processEvents();
            CHECK(!resizeBtn->isEnabled() && resizeBtn->isHidden());
            CHECK(!partTab->resizeRequestBlockReason().isEmpty());
            CHECK(!partTab->isResizingNow());
        }
    }

    if (tabs) { tabs->setCurrentIndex(0); app.processEvents(); }

    if (qEnvironmentVariableIsSet("DISKCLEANUP_UI_SHOTS"))
    {
        const QString dir = qEnvironmentVariable("DISKCLEANUP_UI_SHOTS");
        const char* names[] = {"cleanup", "largefiles", "duplicates", "partitions"};
        for (int i = 0; i < 4 && tabs; ++i)
        {
            tabs->setCurrentIndex(i);
            app.processEvents();
            win.grab().save(dir + "/" + names[i] + ".png");
        }
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: quet hang muc/tep lon/tep trung lap va DOI KICH THUOC PHAN VUNG that can\n");
    std::printf("tu kiem tra tay tren may that - khong phu hop de chay trong test tu dong.\n");
    return g_fail == 0 ? 0 : 1;
}
