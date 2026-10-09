// Kiểm thử "khói" cho giao diện WiFi: dựng cửa sổ, không crash, xử lý đúng khi có/không có
// dịch vụ WLAN hoặc adapter. KHÔNG kiểm thử quét/kết nối/đọc mật khẩu thật (cần phần cứng WiFi
// thật + có thể cần quyền Administrator) - những phần đó cần tự kiểm tra tay trên máy đích.
// Build: cmake --build build --target wifi_ui_tests && build\wifi_ui_tests.exe
#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include <cstdio>

#include "core/AppPaths.h"
#include "tools/wifi/NetworksTab.h"
#include "tools/wifi/ProfilesTab.h"
#include "tools/wifi/WifiUiStyle.h"
#include "tools/wifi/WifiWindow.h"
#include "tools/wifi/engine/ConnectionWatcher.h"
#include "tools/wifi/engine/SpeedTestRunner.h"
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

    // PHẢI đứng trước mọi thứ đụng tới Logger (WifiWindow ghi log lúc mở): ép dữ liệu vào thư mục tạm để
    // bộ test không ghi vào log/hồ sơ thật của người dùng.
    QTemporaryDir dataDir;
    CHECK(dataDir.isValid());
    AppPaths::setDataDirOverride(dataDir.path());

    // LƯU Ý AN TOÀN: bộ test này CHỈ gọi các API WLAN đọc (liệt kê adapter/mạng/hồ sơ, trạng thái). Tuyệt
    // đối không gọi connectWithPassword/connectToSavedProfile/disconnect/deleteProfile/importProfileXml/
    // rollbackProfile - chúng đổi cấu hình WiFi thật của máy đang chạy test. (Ngoại lệ có kiểm soát ở mục 1:
    // connectWithPassword với GUID adapter KHÔNG hợp lệ, chỉ để kiểm các bước từ chối đứng trước mọi lệnh Wlan*.)

    // ---- 0. Logic thuần (không cần phần cứng/mạng) ----
    {
        // Thống kê ping: mẫu khởi động đã bị loại TRƯỚC khi vào đây, hàm chỉ tính trên các mẫu được giữ.
        const SpeedTestRunner::PingStats empty = SpeedTestRunner::computePingStats({});
        CHECK(empty.minMs == 0 && empty.avgMs == 0 && empty.maxMs == 0 && empty.jitterMs == 0);
        const SpeedTestRunner::PingStats one = SpeedTestRunner::computePingStats({20.0});
        CHECK(one.minMs == 20 && one.avgMs == 20 && one.maxMs == 20 && one.jitterMs == 0);
        const SpeedTestRunner::PingStats s = SpeedTestRunner::computePingStats({20.0, 30.0, 10.0, 20.0});
        CHECK(s.minMs == 10 && s.maxMs == 30 && s.avgMs == 20);
        CHECK(s.jitterMs == (10.0 + 20.0 + 10.0) / 3.0);
        // Minh họa lý do bỏ mẫu đầu (gồm DNS/TCP/TLS): chỉ một mẫu 400 ms đã kéo trung bình lên gấp nhiều lần.
        const SpeedTestRunner::PingStats skewed = SpeedTestRunner::computePingStats({400.0, 20.0, 30.0, 10.0, 20.0});
        CHECK(skewed.avgMs > 4 * s.avgMs && skewed.jitterMs > 5 * s.jitterMs);

        // Runner mới tạo: không chạy, chưa bị dừng; stop() khi không chạy là vô hại (không phát tín hiệu).
        SpeedTestRunner runner;
        int finishedCount = 0;
        QObject::connect(&runner, &SpeedTestRunner::finished, [&finishedCount] { ++finishedCount; });
        CHECK(!runner.isRunning() && !runner.wasStoppedByUser());
        runner.stop();
        CHECK(finishedCount == 0 && !runner.wasStoppedByUser());

        // Quy tắc dừng chờ kết nối: tối thiểu 15 s; còn đang liên kết/xác thực thì chờ tiếp, tối đa 30 s.
        CHECK(!ConnectionWatcher::shouldGiveUp(0, false));
        CHECK(!ConnectionWatcher::shouldGiveUp(14500, false));
        CHECK(ConnectionWatcher::shouldGiveUp(15000, false));
        CHECK(!ConnectionWatcher::shouldGiveUp(15000, true));
        CHECK(!ConnectionWatcher::shouldGiveUp(29500, true));
        CHECK(ConnectionWatcher::shouldGiveUp(30000, true));
    }

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
            // Hồi quy: SSID có ký tự điều khiển bị từ chối TRƯỚC mọi thao tác hồ sơ (xem
            // WlanProfileXml::isSafeProfileName). Ngoại lệ DUY NHẤT của lưu ý an toàn ở trên, và vẫn an toàn:
            // GUID adapter cố ý không hợp lệ, nên dù kiểm tra tên có hỏng thì lời gọi cũng dừng ở bước đổi
            // GUID (đứng trước mọi lệnh Wlan*) - không lệnh nào tới được hệ thống.
            WifiNetwork evil;
            evil.ssid = QString("Nha") + QChar(0x01);
            evil.ssidBytes = evil.ssid.toUtf8();
            evil.security = WifiSecurity::Wpa2Psk;
            WifiProfileBackup backup;
            backup.valid = true;
            QString connectErr;
            CHECK(!ctrl.connectWithPassword("khong-phai-guid", evil, "matkhau123", WifiSecurity::Wpa2Psk, true, &backup, &connectErr));
            CHECK(connectErr.contains("ký tự điều khiển"));
            CHECK(!backup.valid); // không có gì để hoàn tác: chưa hồ sơ nào bị đụng tới
            WifiNetwork normal;
            normal.ssid = "OneForAll-khong-ton-tai-7f3a91c2";
            normal.ssidBytes = normal.ssid.toUtf8();
            connectErr.clear();
            CHECK(!ctrl.connectWithPassword("khong-phai-guid", normal, "matkhau123", WifiSecurity::Wpa2Psk, true, &backup, &connectErr));
            CHECK(connectErr.contains("GUID") && !backup.valid);

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

                // Bất biến của danh sách mạng: ssid không bao giờ mang chuỗi trang trí "(Mạng ẩn)"; mạng có
                // cờ hasProfile phải kèm tên hồ sơ (để "Quên mạng này" làm được việc).
                for (const WifiNetwork& n : nets)
                {
                    CHECK(!n.ssid.startsWith("(Mạng ẩn"));
                    CHECK(n.hidden == n.ssid.isEmpty());
                    CHECK(!n.displayName().isEmpty());
                    CHECK(!n.hasProfile || !n.profileName.isEmpty());
                }
                // hasProfile() (chỉ đọc) khớp với danh sách hồ sơ; tên không tồn tại -> false.
                for (const WifiProfile& p : profs)
                    CHECK(ctrl.hasProfile(list.first().guidString, p.name));
                CHECK(!ctrl.hasProfile(list.first().guidString, "OneForAll-khong-ton-tai-7f3a91c2"));
                CHECK(!ctrl.hasProfile("khong-phai-guid", "x"));
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

        // Hồi quy: cửa sổ được TÁI DÙNG sau khi đóng - đóng rồi hiện lại (showEvent nạp lại adapter + bật
        // lại polling) không được crash, không nhân đôi mục trong combo adapter, vẫn giữ adapter đang chọn.
        if (combo)
        {
            const int adapterCount = combo->count();
            const QString selected = combo->currentData().toString();
            for (int round = 0; round < 2; ++round)
            {
                win.close();
                app.processEvents();
                CHECK(!win.isVisible());
                win.show();
                app.processEvents();
                CHECK(win.isVisible());
                CHECK(combo->count() == adapterCount);
                CHECK(combo->currentData().toString() == selected);
            }
        }

        // Nút "Mật khẩu khác..." tồn tại và (chưa chọn mạng nào) đang khóa cùng nút "Kết nối".
        if (tabs)
        {
            QPushButton* newPassword = nullptr;
            QPushButton* connectBtn = nullptr;
            for (QPushButton* b : tabs->widget(0)->findChildren<QPushButton*>())
            {
                if (b->text().contains("Mật khẩu khác")) newPassword = b;
                if (b->text() == "🔗 Kết nối") connectBtn = b;
            }
            CHECK(newPassword != nullptr && connectBtn != nullptr);
            if (newPassword && connectBtn)
                CHECK(!newPassword->isEnabled() && !connectBtn->isEnabled());
        }

        // Mọi QLabel hiển thị SSID/tên hồ sơ (banner, dòng trạng thái) phải ở chế độ văn bản thuần.
        if (tabs)
        {
            int plainLabels = 0;
            for (int i = 0; i < tabs->count(); ++i)
                for (QLabel* l : tabs->widget(i)->findChildren<QLabel*>())
                    if (l->textFormat() == Qt::PlainText)
                        ++plainLabels;
            CHECK(plainLabels >= 4); // banner + trạng thái (Mạng), trạng thái (Hồ sơ), liên kết (Đo tốc độ)
        }

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
