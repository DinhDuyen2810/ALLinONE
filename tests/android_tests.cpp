// Kiểm thử lõi Android Phone Control. KHÔNG cần thiết bị Android thật hay chạy adb/scrcpy thật - chỉ
// kiểm thử lõi THUẦN (phân tích output adb mẫu dựng sẵn, dựng danh sách đối số dòng lệnh scrcpy) tách
// riêng khỏi phần gọi tiến trình thật, giống mẫu đã dùng cho PartitionManager (parsePartitionsJson).
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include "TestDataDir.h"
#include <cstdio>

#include "core/AppPaths.h"
#include "tools/android/engine/AdbController.h"
#include "tools/android/engine/AdbDeviceLister.h"
#include "tools/android/engine/AdbPairer.h"
#include "tools/android/engine/ScrcpyLauncher.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // Không để bất cứ thứ gì trong test đọc/ghi dữ liệu thật của người dùng (%LOCALAPPDATA%\OneForAll).
    TestDataDir dataDir;
    AppPaths::setDataDirOverride(dataDir.path());

    // ---- AdbController::internal::parseDevicesOutput: dữ liệu mẫu dựng sẵn (không gọi adb thật) ----
    {
        const QString sample =
            "List of devices attached\n"
            "R58M80ABCDE            device usb:1-1 product:beyond2qlte model:SM_G975N device:beyond2q transport_id:1\n"
            "192.168.1.23:5555      device product:redfin model:Pixel_5 device:redfin transport_id:2\n"
            "R9ZT10FGHIJ            unauthorized usb:2-1 transport_id:3\n"
            "emulator-5554          offline transport_id:4\n"
            "\n";
        const auto devices = AdbController::internal::parseDevicesOutput(sample);
        CHECK(devices.size() == 4);
        if (devices.size() == 4)
        {
            CHECK(devices[0].serial == "R58M80ABCDE");
            CHECK(devices[0].state == "device");
            CHECK(devices[0].model == "SM_G975N");
            CHECK(devices[0].isReady());
            CHECK(!devices[0].isWireless());
            CHECK(!devices[0].isUnauthorized());

            CHECK(devices[1].serial == "192.168.1.23:5555");
            CHECK(devices[1].model == "Pixel_5");
            CHECK(devices[1].isReady());
            CHECK(devices[1].isWireless()); // chứa dấu ':' -> kết nối qua mạng

            CHECK(devices[2].serial == "R9ZT10FGHIJ");
            CHECK(devices[2].state == "unauthorized");
            CHECK(devices[2].model.isEmpty()); // chưa cấp quyền nên adb chưa đọc được model
            CHECK(!devices[2].isReady());
            CHECK(devices[2].isUnauthorized());

            CHECK(devices[3].serial == "emulator-5554");
            CHECK(devices[3].state == "offline");
            CHECK(!devices[3].isReady());
        }

        // Rỗng / chỉ có dòng tiêu đề -> không có thiết bị nào, không crash
        CHECK(AdbController::internal::parseDevicesOutput("").isEmpty());
        CHECK(AdbController::internal::parseDevicesOutput("List of devices attached\n").isEmpty());

        // Có dòng cảnh báo daemon xen vào đầu (thường gặp lần chạy adb đầu tiên) - vẫn parse đúng, không lẫn vào danh sách
        const QString withDaemonWarning =
            "* daemon not running; starting now at tcp:5037\n"
            "* daemon started successfully\n"
            "List of devices attached\n"
            "R58M80ABCDE            device product:beyond2qlte model:SM_G975N device:beyond2q transport_id:1\n";
        const auto devices2 = AdbController::internal::parseDevicesOutput(withDaemonWarning);
        CHECK(devices2.size() == 1);
        if (devices2.size() == 1)
            CHECK(devices2[0].serial == "R58M80ABCDE");

        // Dòng CHẨN ĐOÁN/LỖI của adb (thường ở stderr, hoặc lẫn vào stdout) không được thành thiết bị giả:
        // trước đây mọi dòng có >= 2 từ đều được nhận, "serial" là từ đầu tiên của câu.
        const QString withNoise =
            "adb.exe: failed to check server version: cannot connect to daemon\n"
            "error: cannot connect to daemon at tcp:5037: cannot connect to 127.0.0.1:5037\n"
            "adb server version (41) doesn't match this client (36); killing...\n"
            "List of devices attached\n"
            "R58M80ABCDE            device product:beyond2qlte model:SM_G975N device:beyond2q transport_id:1\n"
            "192.168.1.50:5555      connecting\n"
            "ZY22ABCDEF             no permissions (user in plugdev group; are your udev rules wrong?); see [http://developer.android.com/tools/device.html] usb:1-2 transport_id:7\n"
            "QUAIDI                 trang-thai-la transport_id:9\n";
        const auto devices3 = AdbController::internal::parseDevicesOutput(withNoise);
        CHECK(devices3.size() == 3);
        if (devices3.size() == 3)
        {
            CHECK(devices3[0].serial == "R58M80ABCDE" && devices3[0].isReady());
            CHECK(devices3[1].serial == "192.168.1.50:5555" && devices3[1].state == "connecting");
            CHECK(devices3[2].serial == "ZY22ABCDEF" && devices3[2].state == "no permissions");
            CHECK(devices3[2].transportId == "7");
            CHECK(!devices3[2].isReady());
        }
        CHECK(AdbController::internal::isKnownDeviceState("unauthorized"));
        CHECK(!AdbController::internal::isKnownDeviceState("failed"));
        CHECK(!AdbController::internal::isKnownDeviceState(""));
    }

    // ---- Kiểm định dạng IP:Cổng / mã ghép đôi (trở thành đối số dòng lệnh của adb.exe) ----
    {
        CHECK(AdbController::isValidIpAndPort("192.168.1.23:41234"));
        CHECK(AdbController::isValidIpAndPort("10.0.0.5:5555"));
        CHECK(AdbController::isValidIpAndPort("pixel-7.local:37011"));
        CHECK(AdbController::isValidIpAndPort("[fe80::1]:5555"));
        CHECK(AdbController::isValidIpAndPort("192.168.1.23:65535"));

        CHECK(!AdbController::isValidIpAndPort(""));
        CHECK(!AdbController::isValidIpAndPort("192.168.1.23"));          // thiếu cổng
        CHECK(!AdbController::isValidIpAndPort("192.168.1.23:"));
        CHECK(!AdbController::isValidIpAndPort("192.168.1.23:0"));
        CHECK(!AdbController::isValidIpAndPort("192.168.1.23:65536"));
        CHECK(!AdbController::isValidIpAndPort("192.168.1.23:12a4"));
        CHECK(!AdbController::isValidIpAndPort("-s:5555"));               // bắt đầu bằng '-' = tùy chọn adb
        CHECK(!AdbController::isValidIpAndPort("--help"));
        CHECK(!AdbController::isValidIpAndPort("-a 192.168.1.23:5555"));
        CHECK(!AdbController::isValidIpAndPort("192.168.1.23:5555 extra"));
        CHECK(!AdbController::isValidIpAndPort("192.168.1.23:5555\n-s"));
        CHECK(!AdbController::isValidIpAndPort(":5555"));
        CHECK(!AdbController::isValidIpAndPort("192.168.1.23:5555\n")); // '$' khớp cả trước '\n' cuối chuỗi - phải neo bằng \z

        CHECK(AdbController::isValidPairingCode("123456"));
        CHECK(AdbController::isValidPairingCode("000000"));
        CHECK(!AdbController::isValidPairingCode("12345"));
        CHECK(!AdbController::isValidPairingCode("1234567"));
        CHECK(!AdbController::isValidPairingCode("12345a"));
        CHECK(!AdbController::isValidPairingCode("-12345"));
        CHECK(!AdbController::isValidPairingCode(" 123456"));
        CHECK(!AdbController::isValidPairingCode("123456\n"));
        CHECK(!AdbController::isValidPairingCode(""));

        // Các hàm gọi adb phải TỪ CHỐI đầu vào sai định dạng trước khi khởi chạy bất kỳ tiến trình nào
        QString error;
        CHECK(!AdbController::pairWireless("--help", "123456", &error));
        CHECK(!error.isEmpty());
        error.clear();
        CHECK(!AdbController::pairWireless("192.168.1.23:41234", "-x", &error));
        CHECK(!error.isEmpty());
        error.clear();
        CHECK(!AdbController::connectWireless("-s", &error));
        CHECK(!error.isEmpty());
        error.clear();
        CHECK(!AdbController::disconnectWireless("", &error));
        CHECK(!error.isEmpty());
    }

    // ---- So đường dẫn tệp ảnh tiến trình + dừng theo đường dẫn (không đụng adb của công cụ khác) ----
    {
        using AdbController::internal::isSameExecutablePath;
        CHECK(isSameExecutablePath("C:/Apps/OneForAll/scrcpy/adb.exe", "C:\\Apps\\OneForAll\\scrcpy\\adb.exe"));
        CHECK(isSameExecutablePath("C:/Apps/OneForAll/scrcpy/adb.exe", "c:/apps/oneforall/SCRCPY/ADB.EXE"));
        CHECK(isSameExecutablePath("C:/Apps/OneForAll/scrcpy/adb.exe", "\\\\?\\C:\\Apps\\OneForAll\\scrcpy\\adb.exe"));
        CHECK(isSameExecutablePath("C:/Apps/OneForAll/scrcpy/../scrcpy/adb.exe", "C:/Apps/OneForAll/scrcpy/adb.exe"));
        // Cùng TÊN tệp nhưng khác thư mục = adb của công cụ khác -> KHÔNG được coi là của ta
        CHECK(!isSameExecutablePath("C:/Apps/OneForAll/scrcpy/adb.exe", "C:/Android/Sdk/platform-tools/adb.exe"));
        CHECK(!isSameExecutablePath("C:/Apps/OneForAll/scrcpy/adb.exe", ""));
        CHECK(!isSameExecutablePath("", ""));

        // Chính tệp test đang chạy so với chính nó (đường dẫn thật, có tồn tại)
        const QString self = QCoreApplication::applicationFilePath();
        CHECK(isSameExecutablePath(self, QDir::toNativeSeparators(self).toUpper()));

        // Hai tệp THẬT khác nhau cùng tên: không bao giờ là một. Cùng một tệp viết theo tên ngắn 8.3 (nếu ổ
        // chứa thư mục tạm có bật 8.3): là một - so chuỗi thuần bỏ sót trường hợp này.
        {
            const QString oursDir = dataDir.path() + "/thu muc ten rat dai cua ta";
            const QString otherDir = dataDir.path() + "/cong cu khac";
            CHECK(QDir().mkpath(oursDir) && QDir().mkpath(otherDir));
            for (const QString& dir : {oursDir, otherDir})
            {
                QFile f(dir + "/adb.exe");
                CHECK(f.open(QIODevice::WriteOnly));
                f.write("khong phai exe that");
            }
            CHECK(!isSameExecutablePath(oursDir + "/adb.exe", otherDir + "/adb.exe"));
            CHECK(isSameExecutablePath(oursDir + "/adb.exe", oursDir + "/../" + QFileInfo(oursDir).fileName() + "/ADB.EXE"));

            const QString viaShortName = dataDir.path() + "/THUMUC~1/adb.exe"; // tên 8.3 Windows sinh cho oursDir
            if (QFileInfo::exists(viaShortName))
                CHECK(isSameExecutablePath(oursDir + "/adb.exe", viaShortName));
            else
                std::printf("LUU Y: o dia chua thu muc tam khong sinh ten ngan 8.3 - bo qua ca so ten ngan\n");
        }

        // Không có tiến trình nào mang đường dẫn này -> không dừng gì, không crash. (Tiến trình đang gọi
        // luôn bị loại trừ, nên truyền chính đường dẫn của test cũng không tự dừng mình.)
        CHECK(AdbController::internal::terminateProcessesByImagePath(
                  dataDir.path() + "/khong-ton-tai/adb.exe") == 0);
        CHECK(AdbController::internal::terminateProcessesByImagePath(self) == 0);
        CHECK(AdbController::internal::terminateProcessesByImagePath("") == 0);
    }

    // ---- AdbDeviceLister: bất đồng bộ, luôn báo kết quả đúng MỘT lần cho mỗi lần refresh() ----
    {
        AdbDeviceLister lister;
        int results = 0;
        QString lastError;
        QObject::connect(&lister, &AdbDeviceLister::listed, &app,
                         [&](const QList<AndroidDeviceInfo>&, const QString& error) {
                             ++results;
                             lastError = error;
                         });
        CHECK(!lister.isBusy());

        if (AdbController::adbExecutablePath().isEmpty())
        {
            // Không có gói adb cạnh file test: báo lỗi rõ ràng (qua tín hiệu), không treo, không crash
            CHECK(lister.refresh());
            CHECK(results == 1);
            CHECK(!lastError.isEmpty());
            CHECK(!lister.isBusy());
        }
        else
        {
            // Có adb thật (chỉ đọc - `adb devices -l`): refresh() trả về NGAY (không chặn), lần gọi chồng
            // bị từ chối, kết quả tới sau qua vòng lặp sự kiện.
            QElapsedTimer startTimer;
            startTimer.start();
            CHECK(lister.refresh());
            CHECK(startTimer.elapsed() < 2000);
            CHECK(lister.isBusy());
            CHECK(!lister.refresh()); // không chồng lệnh
            QElapsedTimer waitTimer;
            waitTimer.start();
            while (results == 0 && waitTimer.elapsed() < 25000)
                app.processEvents(QEventLoop::AllEvents, 50);
            CHECK(results == 1);
            CHECK(!lister.isBusy());
            std::printf("AdbDeviceLister that: %s\n", lastError.isEmpty() ? "OK" : qPrintable("loi: " + lastError));

            // cancel() giữa chừng: không phát kết quả cho lần bị hủy
            CHECK(lister.refresh());
            lister.cancel();
            app.processEvents();
            CHECK(results == 1);
            CHECK(!lister.isBusy());

            // Dọn daemon adb mà chính lần chạy trên đã dựng (đúng tệp adb.exe cạnh file test) - sau đó
            // không còn tiến trình nào mang đường dẫn đó.
            AdbController::stopBundledAdbServer();
            CHECK(AdbController::stopBundledAdbServer() == 0);
        }
    }

    // ---- ScrcpyLauncherInternal::buildArguments: dựng đối số dòng lệnh thuần (không chạy scrcpy thật) ----
    {
        ScrcpyOptions opts;
        opts.serial = "R58M80ABCDE";
        opts.maxSize = 1280;
        opts.bitRateMbps = 12;
        opts.maxFps = 30;
        opts.stayAwake = true;
        opts.turnScreenOff = true;
        opts.alwaysOnTop = true;
        opts.fullscreen = false;
        opts.disableAudio = false;
        opts.recordFilePath.clear();
        opts.windowTitle = "Test Window";

        const QStringList args = ScrcpyLauncherInternal::buildArguments(opts);
        CHECK(args.contains("-s"));
        CHECK(args.contains("R58M80ABCDE"));
        CHECK(args.contains("--max-size=1280"));
        CHECK(args.contains("--video-bit-rate=12M"));
        CHECK(args.contains("--max-fps=30"));
        CHECK(args.contains("--stay-awake"));
        CHECK(args.contains("--turn-screen-off"));
        CHECK(args.contains("--always-on-top"));
        CHECK(!args.contains("--fullscreen"));
        CHECK(!args.contains("--no-audio"));
        CHECK(!args.contains("--tcpip"));
        CHECK(args.contains("--window-title=Test Window"));
        // Không ghi hình -> không có cờ --record nào trong danh sách
        bool hasRecordFlag = false;
        for (const auto& a : args)
            if (a.startsWith("--record=")) hasRecordFlag = true;
        CHECK(!hasRecordFlag);

        // Mặc định (ScrcpyOptions{}) - các cờ giá trị 0/false không được thêm vào, chỉ có stay-awake
        // (mặc định true) + window-title (mặc định có sẵn chuỗi)
        ScrcpyOptions defaults;
        const QStringList defaultArgs = ScrcpyLauncherInternal::buildArguments(defaults);
        CHECK(!defaultArgs.contains("-s")); // serial rỗng -> để scrcpy tự chọn, không truyền -s
        CHECK(defaultArgs.contains("--stay-awake"));
        bool hasMaxSizeFlag = false, hasMaxFpsFlag = false;
        for (const auto& a : defaultArgs)
        {
            if (a.startsWith("--max-size=")) hasMaxSizeFlag = true;
            if (a.startsWith("--max-fps=")) hasMaxFpsFlag = true;
        }
        CHECK(!hasMaxSizeFlag); // maxSize=0 -> độ phân giải gốc, không giới hạn gì cả
        CHECK(!hasMaxFpsFlag);

        // Chuyển sang không dây + có ghi hình
        ScrcpyOptions wireless;
        wireless.serial = "R58M80ABCDE";
        wireless.switchToWireless = true;
        wireless.recordFilePath = "C:/Users/test/record.mp4";
        const QStringList wirelessArgs = ScrcpyLauncherInternal::buildArguments(wireless);
        CHECK(wirelessArgs.contains("--tcpip"));
        CHECK(wirelessArgs.contains("-s")); // vẫn chỉ định đúng thiết bị USB cần chuyển, không để mơ hồ
        CHECK(wirelessArgs.contains("--record=C:/Users/test/record.mp4"));
    }

    // ---- Đường dẫn adb/scrcpy: không có file thật trong môi trường test -> rỗng, không crash ----
    {
        // Không khẳng định cụ thể rỗng hay không (máy chạy test CÓ THỂ đã build/deploy thật), chỉ xác
        // nhận gọi được mà không crash và isBundleAvailable() nhất quán với 2 đường dẫn kia.
        const QString adbPath = AdbController::adbExecutablePath();
        const QString scrcpyPath = AdbController::scrcpyExecutablePath();
        QString missing;
        const bool available = AdbController::isBundleAvailable(&missing);
        CHECK(available == (!adbPath.isEmpty() && !scrcpyPath.isEmpty()));
        CHECK(true); // tới được đây tức là không crash
    }

    // ---- interpretPairResult: diễn giải kết quả `adb pair` (thuần, không gọi adb) ----
    {
        using AdbController::internal::interpretPairResult;
        QString e;
        CHECK(interpretPairResult(true, 0, "Successfully paired to 192.168.1.23:41234 [guid=adb-R58M-abc]\r\n", "", &e));
        // adb trả mã thoát 0 cả khi sai mã - phải đọc nội dung
        CHECK(!interpretPairResult(true, 0, "Failed: Wrong password or connection was dropped.\r\n", "", &e));
        CHECK(e == "Failed: Wrong password or connection was dropped.");
        CHECK(!interpretPairResult(true, 0, "", "  \r\n", &e));
        CHECK(e.startsWith(QString::fromUtf8("Ghép đôi thất bại")));
        CHECK(!interpretPairResult(true, 1, "", "error: unknown host service\r\n", &e));
        CHECK(e == "error: unknown host service");
        CHECK(!interpretPairResult(true, 1, "Successfully paired", "", &e)); // mã thoát khác 0: không tin stdout
        CHECK(e == QString::fromUtf8("adb thoát với mã lỗi 1"));
        CHECK(!interpretPairResult(false, 0, "Successfully paired", "", &e)); // adb crash/bị kill
        CHECK(interpretPairResult(true, 0, "", "successfully PAIRED to x", nullptr)); // nằm ở stderr, error = nullptr
        CHECK(!interpretPairResult(true, 0, "", "", nullptr));
    }

    // ---- AdbPairer (bất đồng bộ): lỗi định dạng/thiếu adb báo NGAY qua tín hiệu, không chạy tiến trình nào ----
    {
        AdbPairer pairer;
        int calls = 0;
        bool lastOk = true;
        QString lastError;
        QObject::connect(&pairer, &AdbPairer::finished, &pairer, [&](bool ok, const QString& error) {
            ++calls;
            lastOk = ok;
            lastError = error;
        });
        CHECK(!pairer.isBusy());
        pairer.cancel(); // không có gì đang chạy: không crash, không phát tín hiệu
        CHECK(calls == 0);

        CHECK(pairer.pair("--help", "123456")); // chuỗi bắt đầu bằng '-' không bao giờ tới được adb
        CHECK(calls == 1 && !lastOk && lastError.contains(QString::fromUtf8("IP:Cổng")));
        CHECK(pairer.pair("192.168.1.23:41234\n", "123456"));
        CHECK(calls == 2 && !lastOk);
        CHECK(pairer.pair("192.168.1.23:41234", "12345"));
        CHECK(calls == 3 && !lastOk && lastError.contains(QString::fromUtf8("6 chữ số")));
        CHECK(pairer.pair("192.168.1.23:41234", "-12345"));
        CHECK(calls == 4 && !lastOk);
        CHECK(!pairer.isBusy());

        // KHÔNG BAO GIỜ chạy `adb pair` thật trong test: chỉ đi tiếp khi không có adb.exe cạnh file test.
        if (AdbController::adbExecutablePath().isEmpty())
        {
            CHECK(pairer.pair("192.168.1.23:41234", "123456"));
            CHECK(calls == 5 && !lastOk && lastError.contains("adb.exe"));
            CHECK(!pairer.isBusy());
        }
        else
        {
            std::printf("  (co adb.exe canh file test - bo qua duong loi 'khong tim thay adb.exe' cua AdbPairer)\n");
        }
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: khong co thiet bi Android that trong moi truong nay - cac ham goi adb/scrcpy\n");
    std::printf("that (listDevices/pairWireless/connectWireless/ScrcpyLauncher::start thuc su chay) can\n");
    std::printf("duoc tu kiem tra tay tren may that co cam dien thoai Android.\n");
    return g_fail == 0 ? 0 : 1;
}
