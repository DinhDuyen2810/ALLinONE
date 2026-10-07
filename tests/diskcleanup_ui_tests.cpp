// Kiểm thử "khói" cho giao diện Disk Cleanup: dựng cửa sổ, không crash, các widget chính tồn tại.
// KHÔNG bấm nút Quét thật ở đây: hạng mục dọn dẹp trỏ vào %TEMP%/%WINDIR% thật và "Tìm tệp lớn" có
// thể quét cả ổ đĩa - cả hai đụng vào hệ thống thật của máy build, không phù hợp cho test tự động
// (giống nguyên tắc đã áp dụng cho wifi_ui_tests: quét/kết nối thật phải tự kiểm tra tay).
// Build: cmake --build build --target diskcleanup_ui_tests && build\diskcleanup_ui_tests.exe
#include <QApplication>
#include <QCheckBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <cstdio>

#include "tools/diskcleanup/CleanupTab.h"
#include "tools/diskcleanup/DiskCleanupWindow.h"
#include "tools/diskcleanup/LargeFilesTab.h"

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
    CHECK(tabs != nullptr && tabs->count() == 2);

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

    if (tabs)
    {
        tabs->setCurrentIndex(1);
        app.processEvents();
    }

    auto* largeFilesTab = win.findChild<LargeFilesTab*>();
    CHECK(largeFilesTab != nullptr);
    if (largeFilesTab)
    {
        auto* table = largeFilesTab->findChild<QTableWidget*>();
        CHECK(table != nullptr && table->rowCount() == 0);
    }

    if (tabs)
    {
        tabs->setCurrentIndex(0);
        app.processEvents();
    }

    if (qEnvironmentVariableIsSet("DISKCLEANUP_UI_SHOTS"))
    {
        const QString dir = qEnvironmentVariable("DISKCLEANUP_UI_SHOTS");
        const char* names[] = {"cleanup", "largefiles"};
        for (int i = 0; i < 2 && tabs; ++i)
        {
            tabs->setCurrentIndex(i);
            app.processEvents();
            win.grab().save(dir + "/" + names[i] + ".png");
        }
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: quet hang muc thuc & tim tep lon thuc can tu kiem tra tay tren may that,\n");
    std::printf("vi chung dung vao %%TEMP%%/%%WINDIR%% va co the ca o dia thuc cua may build.\n");
    return g_fail == 0 ? 0 : 1;
}
