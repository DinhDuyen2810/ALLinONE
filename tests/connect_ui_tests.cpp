// Kiểm thử "khói" cho giao diện Connect Together: dựng cửa sổ, không crash. KHÔNG kiểm thử chia sẻ
// chuột/bàn phím thật hay ghép đôi qua 2 máy thật - những phần đó cần tự kiểm tra tay.
// Build: cmake --build build --target connect_ui_tests && build\connect_ui_tests.exe
#include <QApplication>
#include <QTabWidget>
#include <QThread>
#include <cstdio>

#include "tools/connect/ConnectWindow.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

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

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
