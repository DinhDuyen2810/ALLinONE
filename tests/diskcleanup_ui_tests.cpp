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
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <cstdio>

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
