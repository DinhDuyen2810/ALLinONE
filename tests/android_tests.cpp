// Kiểm thử lõi Android Phone Control. KHÔNG cần thiết bị Android thật hay chạy adb/scrcpy thật - chỉ
// kiểm thử lõi THUẦN (phân tích output adb mẫu dựng sẵn, dựng danh sách đối số dòng lệnh scrcpy) tách
// riêng khỏi phần gọi tiến trình thật, giống mẫu đã dùng cho PartitionManager (parsePartitionsJson).
#include <QCoreApplication>
#include <cstdio>

#include "tools/android/engine/AdbController.h"
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

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: khong co thiet bi Android that trong moi truong nay - cac ham goi adb/scrcpy\n");
    std::printf("that (listDevices/pairWireless/connectWireless/ScrcpyLauncher::start thuc su chay) can\n");
    std::printf("duoc tu kiem tra tay tren may that co cam dien thoai Android.\n");
    return g_fail == 0 ? 0 : 1;
}
