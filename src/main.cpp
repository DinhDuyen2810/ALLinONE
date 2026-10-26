#include <QApplication>
#include <cstring>
#include "ui/AppTheme.h"
#include "ui/MainWindow.h"
#include "core/Logger.h"
#include "core/Version.h"
#include "tools/vpn/direct/VpnHelperMain.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
    // Tiến trình phụ NÂNG QUYỀN của kết nối VPN trực tiếp (do chính ứng dụng khởi chạy qua UAC) - không dựng
    // giao diện, không giữ mutex của ứng dụng; tự thoát khi ứng dụng chính đóng kênh điều khiển.
    if (argc >= 4 && std::strcmp(argv[1], "--vpn-helper") == 0)
        return runVpnHelper(argc, argv);

#ifdef _WIN32
    // Thiết lập Per-Monitor DPI Awareness V2 theo mục 68 trong tài liệu thiết kế
    // Đảm bảo tọa độ GetCursorPos/SetCursorPos khớp 1:1 với màn hình vật lý
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Giữ một Mutex có tên trong suốt vòng đời tiến trình - KHÔNG dùng để chặn chạy nhiều bản sao (giữ
    // nguyên hành vi cũ), CHỈ để Windows Restart Manager + Inno Setup (AppMutex trong
    // installer/OneForAll.iss) tự phát hiện OneForAll.exe đang chạy khi UpdateInstaller âm thầm chạy lại
    // trình cài đặt (/CLOSEAPPLICATIONS /RESTARTAPPLICATIONS) - xem src/core/update/UpdateInstaller.cpp.
    CreateMutexW(nullptr, FALSE, L"OneForAllRunningMutex");
#endif

    QApplication app(argc, argv);
    app.setApplicationName("OneForAll");
    app.setApplicationVersion(APP_VERSION);
    app.setOrganizationName("OneForAll");
    AppTheme::apply(app); // kiểu Fusion + bảng màu sáng + stylesheet toàn ứng dụng (hover/nhấn/focus, bo góc, con trỏ tay)

    Logger::instance().info("Application", QString("Khởi động One for ALL v%1 (C++20, Qt6)").arg(APP_VERSION));

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
