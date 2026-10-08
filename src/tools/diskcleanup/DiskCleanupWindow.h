#pragma once

#include <QWidget>

class QTabWidget;
class CleanupTab;
class LargeFilesTab;
class DuplicateFilesTab;
class PartitionTab;

/// Cửa sổ Disk Cleanup: dọn dẹp rác hệ thống theo hạng mục, tìm/xóa tệp lớn, tìm tệp trùng lặp, và
/// quản lý/đổi kích thước phân vùng đĩa.
class DiskCleanupWindow : public QWidget
{
    Q_OBJECT

public:
    explicit DiskCleanupWindow(QWidget* parent = nullptr);
    ~DiskCleanupWindow() override;

    /// Đang đổi kích thước phân vùng HOẶC đang xóa tệp thật (qua Thùng rác) - 2 thao tác KHÔNG an toàn
    /// để bị buộc dừng giữa chừng (xem closeEvent() bên dưới). Dùng cho ITool::isWindowBusy() - CHẶN các
    /// hành động khiến tiến trình hiện tại kết thúc ngay từ một cửa sổ KHÁC (relaunch elevated ở Security
    /// Gateway/chính tab Quản lý phân vùng, hoặc tự cập nhật) trong lúc đang chạy 1 trong 2 thao tác này.
    bool hasUnsafeOperationInProgress() const;

    /// Có tab NÀO KHÁC (không phải `exceptTab`) trong cùng cửa sổ đang quét/dọn dẹp/đổi kích thước
    /// không - dùng để mỗi tab tự chặn BẮT ĐẦU thao tác mới khi tab khác đang động vào đĩa, tránh quét/
    /// xóa/resize chồng chéo trên cùng ổ đĩa (vd quét tệp lớn trên ổ C: trong lúc đang đổi kích thước
    /// chính ổ C: ở tab Quản lý phân vùng).
    bool isAnyOtherTabBusy(const QWidget* exceptTab) const;

    /// Dừng NGAY các thao tác AN TOÀN để hủy giữa chừng (quét tệp lớn/tệp trùng lặp) - KHÔNG đụng tới
    /// resize/dọn dẹp (những thao tác đó được bảo vệ ở lớp khác: ITool::isWindowBusy() CHẶN hẳn mọi hành
    /// động dẫn tới thoát ứng dụng trong lúc chúng đang chạy - xem ToolManager::anyToolWindowBusy()). Gọi
    /// từ DiskCleanupTool::stopBackgroundWorkForQuit() khi ứng dụng thoát qua qApp->quit() ở nơi khác -
    /// KHÔNG gọi close()/closeEvent() ở đây vì closeEvent() có thể hiện hộp thoại cảnh báo (vô nghĩa giữa
    /// một luồng thoát không thể hủy).
    void stopSafelyCancellableWorkForQuit();

protected:
    /// CHẶN HẲN việc đóng cửa sổ (không cho lựa chọn) nếu đang đổi kích thước phân vùng HOẶC đang xóa
    /// tệp thật (qua Thùng rác) - cả 2 thao tác này đều KHÔNG hỗ trợ hủy giữa chừng an toàn (resize có
    /// thể hỏng hệ thống tệp; CleanupExecutor xử lý cả lô bằng một lệnh SHFileOperationW, không có móc
    /// dừng sớm) - khác các tác vụ quét khác trong ứng dụng vốn hủy giữa chừng được an toàn.
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();

    QTabWidget* m_tabs{nullptr};
    CleanupTab* m_cleanupTab{nullptr};
    LargeFilesTab* m_largeFilesTab{nullptr};
    DuplicateFilesTab* m_duplicateFilesTab{nullptr};
    PartitionTab* m_partitionTab{nullptr};
};
