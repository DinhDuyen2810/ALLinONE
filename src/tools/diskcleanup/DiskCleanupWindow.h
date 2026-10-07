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
