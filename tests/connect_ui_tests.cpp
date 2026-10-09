// Kiểm thử "khói" cho giao diện Connect Together: dựng cửa sổ, không crash, đóng rồi mở lại vẫn chạy.
// KHÔNG kiểm thử chia sẻ chuột/bàn phím thật hay ghép đôi qua 2 máy thật - những phần đó cần tự kiểm tra
// tay. Cửa sổ thật có cài hook toàn cục nhưng luôn ở trạng thái trong suốt (không active) trong test này.
// Build: cmake --build build --target connect_ui_tests && build\connect_ui_tests.exe
#include <QApplication>
#include <QTabWidget>
#include <QTemporaryDir>
#include "TestDataDir.h"
#include <QThread>
#include <cstdio>

#include "core/AppPaths.h"
#include "tools/connect/ConnectWindow.h"
#include "tools/connect/engine/ConnectSessionController.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // TRƯỚC KHI dựng cửa sổ (nó khởi tạo LocalIdentityStore/PeerStore/Logger): ép dữ liệu vào thư mục tạm -
    // test không được đọc/ghi danh tính + khóa ghép đôi thật của người dùng, cũng không tự kết nối tới các
    // máy họ đã ghép đôi.
    TestDataDir dataDir;
    CHECK(dataDir.isValid());
    AppPaths::setDataDirOverride(dataDir.path());

    ConnectWindow win;
    win.setAttribute(Qt::WA_DontShowOnScreen, true);
    win.show();
    app.processEvents();
    CHECK(true); // tới được đây tức là không crash

    auto* tabs = win.findChild<QTabWidget*>();
    CHECK(tabs != nullptr && tabs->count() == 2);
    if (tabs)
    {
        tabs->setCurrentIndex(1);
        app.processEvents();
        tabs->setCurrentIndex(0);
        app.processEvents();
    }

    // Đóng cửa sổ dừng controller; MỞ LẠI phải chạy lại (cửa sổ được ConnectTool tái dùng, không tạo mới -
    // trước đây mở lại thì cửa sổ hiện ra bình thường nhưng controller đã chết cho tới khi khởi động lại
    // cả ứng dụng).
    auto* controller = win.findChild<ConnectSessionController*>();
    CHECK(controller != nullptr);
    if (controller)
    {
        CHECK(controller->isRunning());
        CHECK(controller->listenPort() != 0);
        CHECK(!controller->isHookActive());
        CHECK(controller->pairedPeers().isEmpty()); // đang dùng thư mục dữ liệu tạm, không phải dữ liệu thật

        win.close();
        app.processEvents();
        CHECK(!controller->isRunning());
        CHECK(controller->listenPort() == 0);

        win.show();
        app.processEvents();
        CHECK(controller->isRunning());
        CHECK(controller->listenPort() != 0);
        CHECK(!controller->isHookActive());
    }

    if (qEnvironmentVariableIsSet("CONNECT_UI_SHOTS"))
    {
        const QString dir = qEnvironmentVariable("CONNECT_UI_SHOTS");
        const char* names[] = {"pairing", "peers"};
        for (int i = 0; i < 2 && tabs; ++i)
        {
            tabs->setCurrentIndex(i);
            for (int k = 0; k < 10; ++k)
            {
                app.processEvents();
                QThread::msleep(20);
            }
            win.grab().save(dir + "/" + names[i] + ".png");
        }
    }

    win.close();
    app.processEvents();

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
