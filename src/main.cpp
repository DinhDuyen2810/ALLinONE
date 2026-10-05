#include <QApplication>
#include "ui/MainWindow.h"
#include "core/Logger.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

int main(int argc, char* argv[])
{
#ifdef _WIN32
    // Thiết lập Per-Monitor DPI Awareness V2 theo mục 68 trong tài liệu thiết kế
    // Đảm bảo tọa độ GetCursorPos/SetCursorPos khớp 1:1 với màn hình vật lý
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
#endif

    QApplication app(argc, argv);
    app.setApplicationName("OneForAll");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("OneForAll");

    Logger::instance().info("Application", "Khởi động One for ALL v1.0.0 (C++20, Qt6)");

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
