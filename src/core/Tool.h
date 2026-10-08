#pragma once

#include <QString>
#include <QIcon>
#include <QWidget>
#include <memory>

/**
 * @brief Interface đại diện cho một Tool độc lập trong One for ALL.
 * Tuân thủ mục 50 trong tài liệu thiết kế OneForAll_AutoClick_Design.md.
 */
class ITool
{
public:
    virtual ~ITool() = default;

    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QString iconPath() const = 0;
    virtual QIcon icon() const = 0;
    virtual bool isAvailable() const { return true; }

    /**
     * @brief Tạo cửa sổ riêng cho tool.
     * Cửa sổ này có thể được hiển thị độc lập hoặc nhúng theo nhu cầu.
     */
    virtual QWidget* createWindow() = 0;

    /// Dừng NGAY, KHÔNG hỏi xác nhận, mọi công việc nền có nguy cơ mồ côi tiến trình ngoài (xem
    /// core/WinProcessTree.h) nếu cửa sổ của tool này đang mở - gọi từ ToolManager::stopAllBackgroundWorkForQuit()
    /// khi ứng dụng sắp thoát theo BẤT KỲ đường nào (qApp->quit() trực tiếp KHÔNG tự gọi closeEvent() của
    /// các cửa sổ khác đang mở - xem ghi chú ở các điểm gọi qApp->quit()). Mặc định không làm gì - chỉ
    /// ghi đè ở tool có tiến trình ngoài DÀI HẠN (adb.exe/scrcpy.exe, yt-dlp.exe, rasdial.exe...) VÀ có
    /// cách dừng không cần hộp thoại xác nhận (khác isWindowBusy() bên dưới - dành cho thao tác KHÔNG
    /// được phép bị buộc dừng giữa chừng).
    virtual void stopBackgroundWorkForQuit() {}

    /// Cửa sổ của tool này (nếu đang mở) có đang thực hiện một thao tác KHÔNG AN TOÀN để bị buộc dừng
    /// giữa chừng không (vd đổi kích thước phân vùng đĩa thật - có thể hỏng hệ thống tệp) - dùng để CHẶN
    /// hẳn các điểm gọi qApp->quit() trực tiếp từ nơi khác trong ứng dụng (xem WebProtectionTab.cpp/
    /// MalwareScanTab.cpp/PartitionTab.cpp/MainWindow.cpp), khác stopBackgroundWorkForQuit() ở trên vốn
    /// dành cho việc có thể dừng êm được. Mặc định false (an toàn).
    virtual bool isWindowBusy() const { return false; }
};
