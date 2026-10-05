// Kiểm thử "khói" cho giao diện WiFi: dựng cửa sổ, không crash, xử lý đúng khi có/không có
// dịch vụ WLAN hoặc adapter. KHÔNG kiểm thử quét/kết nối/đọc mật khẩu thật (cần phần cứng WiFi
// thật + có thể cần quyền Administrator) - những phần đó cần tự kiểm tra tay trên máy đích.
// Build: cmake --build build --target wifi_ui_tests && build\wifi_ui_tests.exe
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QTabWidget>
#include <cstdio>

#include "tools/wifi/NetworksTab.h"
#include "tools/wifi/ProfilesTab.h"
#include "tools/wifi/WifiWindow.h"
#include "tools/wifi/engine/WlanController.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // ---- 1. WlanController mở được handle (hoặc báo lỗi rõ ràng, không crash) ----
    {
        WlanController ctrl;
        QString err;
        const bool opened = ctrl.open(&err);
        std::printf("WLAN service opened: %s%s%s\n", opened ? "yes" : "no", opened ? "" : " (",
                   opened ? "" : qPrintable(err + ")"));
        CHECK(opened || !err.isEmpty()); // mở được, hoặc có thông báo lỗi rõ ràng - không im lặng thất bại

        if (opened)
        {
            QString adaptersErr;
            const auto list = ctrl.adapters(&adaptersErr);
            std::printf("adapters found: %lld\n", static_cast<long long>(list.size()));
            for (const auto& a : list)
                std::printf("  - %s (%s)%s\n", qPrintable(a.description), qPrintable(a.guidString),
                           a.connected ? " [connected]" : "");

            // Gọi các API đọc (không kết nối/xóa/ghi gì) trên adapter đầu tiên nếu có, phải không crash
            if (!list.isEmpty())
            {
                QString e1, e2, e3;
                const auto nets = ctrl.availableNetworks(list.first().guidString, &e1);
                const auto profs = ctrl.profiles(list.first().guidString, /*withPlaintextPassword=*/false, &e2);
                const auto cur = ctrl.currentConnection(list.first().guidString, &e3);
                std::printf("networks=%lld profiles=%lld currentlyConnected=%s\n", static_cast<long long>(nets.size()),
                           static_cast<long long>(profs.size()), cur.isConnected ? "yes" : "no");
                CHECK(true); // tới được đây tức là không crash
            }
        }
    }

    // ---- 2. WifiWindow dựng được và không crash, bất kể có adapter hay không ----
    {
        WifiWindow win;
        win.setAttribute(Qt::WA_DontShowOnScreen, true);
        win.show();
        app.processEvents();
        CHECK(true);

        auto* tabs = win.findChild<QTabWidget*>();
        CHECK(tabs != nullptr && tabs->count() == 3);
        if (tabs)
        {
            tabs->setCurrentIndex(1);
            app.processEvents();
            tabs->setCurrentIndex(0);
            app.processEvents();
        }

        auto* combo = win.findChild<QComboBox*>();
        CHECK(combo != nullptr);

        if (qEnvironmentVariableIsSet("WIFI_UI_SHOTS"))
        {
            const QString dir = qEnvironmentVariable("WIFI_UI_SHOTS");
            const char* names[] = {"networks", "profiles", "speedtest"};
            for (int i = 0; i < 3 && tabs; ++i)
            {
                tabs->setCurrentIndex(i);
                app.processEvents();
                win.grab().save(dir + "/" + names[i] + ".png");
            }
        }
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: cac luong song (quet mang, ket noi that, doc mat khau da luu) can duoc\n");
    std::printf("tu kiem tra tay tren may that co WiFi, vi moi truong build nay khong dam bao co adapter.\n");
    return g_fail == 0 ? 0 : 1;
}
