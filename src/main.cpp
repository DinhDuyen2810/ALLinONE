#include <QApplication>
#include <cstring>
#include "ui/AppTheme.h"
#include "ui/MainWindow.h"
#include "core/Logger.h"
#include "core/Version.h"
#include "core/ToolManager.h"
#include "tools/diskcleanup/PartitionRunMain.h"
#include "tools/diskcleanup/engine/PartitionRelocator.h"
#include "tools/security/engine/ConsolePaste.h"
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

    // Tiến trình phụ của Security Gateway: dán lệnh (Ctrl+V vào bộ đệm nhập) vào ĐÚNG cửa sổ PowerShell vừa
    // mở rồi đọc lại màn hình để xác nhận - không dựng giao diện, không giữ mutex; xem ConsolePaste.h.
    if (argc >= 3 && std::strcmp(argv[1], ConsolePaste::kHelperSwitch) == 0)
        return ConsolePaste::runHelperMode(argc, argv);

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

    // Tiến trình CHẠY KẾ HOẠCH DỜI PHÂN VÙNG (do chính ứng dụng khởi chạy qua UAC từ một bản sao trong thư mục tạm, để ổ đang chứa ứng dụng tháo được) - xem PartitionRunMain.h.
    if (argc >= 2 && std::strcmp(argv[1], "--run-partition-plan") == 0)
        return runPartitionPlanMode(app, argc, argv);

    // Dọn các bản sao tạm cũ còn sót của chế độ dời phân vùng (best-effort, bỏ qua cái đang dùng).
    PartitionRelocator::cleanupStaleRuntimes();

    // "--open-tool <id>" (do relaunch với quyền Administrator truyền vào): chỉ mở ĐÚNG tiện ích đó, không dựng cửa sổ chính/kiểm tra cập nhật. Đóng cửa sổ
    // tiện ích là ứng dụng thoát (không còn cửa sổ nào hiện). Không có/không mở được tiện ích đó thì rơi về cửa sổ chính như bình thường.
    QString openTool;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::strcmp(argv[i], "--open-tool") == 0)
            openTool = QString::fromLocal8Bit(argv[i + 1]);
    if (!openTool.isEmpty())
    {
        MainWindow::registerAllTools();
        if (ITool* tool = ToolManager::instance().getTool(openTool))
        {
            QObject::connect(&app, &QApplication::aboutToQuit, [] { ToolManager::instance().stopAllBackgroundWorkForQuit(); });
            if (QWidget* window = tool->createWindow())
            {
                Logger::instance().info("Application", QString("Chế độ một tiện ích: %1").arg(openTool));
                window->showNormal();
                window->raise();
                window->activateWindow();
                return app.exec();
            }
        }
    }

    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
