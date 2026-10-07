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

private:
    void buildUi();

    QTabWidget* m_tabs{nullptr};
    CleanupTab* m_cleanupTab{nullptr};
    LargeFilesTab* m_largeFilesTab{nullptr};
    DuplicateFilesTab* m_duplicateFilesTab{nullptr};
    PartitionTab* m_partitionTab{nullptr};
};
