#pragma once

#include <QHash>
#include <QWidget>

#include "engine/CleanupScanner.h"

class CleanupExecutor;
class FlowLayout;
class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;

/// Tab chính Disk Cleanup: tổng quan dung lượng ổ đĩa, quét theo hạng mục, chọn và dọn dẹp.
class CleanupTab : public QWidget
{
    Q_OBJECT

public:
    explicit CleanupTab(QWidget* parent = nullptr);
    ~CleanupTab() override;

    /// Đang xóa THẬT (qua Thùng rác) - CleanupExecutor không hỗ trợ hủy giữa chừng (một lệnh
    /// SHFileOperationW xử lý cả lô), nên KHÔNG an toàn để đóng cửa sổ lúc này.
    bool isCleaningNow() const;

    /// Đang quét HOẶC đang dọn dẹp - dùng để DiskCleanupWindow::isAnyOtherTabBusy() chặn tab khác bắt
    /// đầu thao tác chồng chéo trên cùng ổ đĩa (rộng hơn isCleaningNow(): quét cũng tính "bận" dù an toàn
    /// hủy giữa chừng, vì vẫn đang đọc đĩa).
    bool isBusy() const;

    /// Yêu cầu dừng lượt quét đang chạy (nếu có), KHÔNG chờ - gọi từ DiskCleanupWindow khi đóng cửa sổ
    /// (chỉ ẨN, tab không bị hủy) hoặc khi ứng dụng thoát, để luồng quét không chạy ngầm vô ích.
    void stopScanIfRunning();

private slots:
    void onScanClicked();
    void onCleanClicked();
    void onCategoryStarted(QString name);
    void onItemFound(CleanupItem item);
    void onCategoryFinished(CleanupCategoryId id, qint64 bytes, int count);
    void onScanFinished(qint64 totalBytes, int totalItems);
    void onScanStopped();
    void onExecutionFinished(bool success, QString error, qint64 freedBytes, int deletedCount);
    void onTableCellChanged(int row, int column);

private:
    void buildUi();
    void reloadDriveOverview();
    void updateSelectedSummary();
    qint64 selectedTotalBytes() const;

    CleanupScanner* m_scanner{nullptr};
    CleanupExecutor* m_executor{nullptr};

    QWidget* m_driveOverview{nullptr};
    // Số thẻ đổi theo số ổ đã mount trên máy (ổ vật lý + ổ ảo như Google Drive for Desktop) - không giới
    // hạn trước được, nên dùng FlowLayout tự xuống dòng thay vì QHBoxLayout (cắt mất thẻ ngoài tầm nhìn
    // khi nhiều ổ, xem CLAUDE.md mục 5 / PROJECT_OVERVIEW.md mục 4r, v1.19.4).
    FlowLayout* m_driveOverviewLayout{nullptr};

    QTableWidget* m_table{nullptr}; // hàng = hạng mục: [checkbox][Tên][Rủi ro][Số mục][Dung lượng]
    QPushButton* m_scanBtn{nullptr};
    QPushButton* m_cleanBtn{nullptr};
    QCheckBox* m_permanentCheck{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_summaryLabel{nullptr};

    QList<CleanupCategory> m_categories; // hạng mục khả dụng trên máy này (rootPaths không rỗng)
    QHash<CleanupCategoryId, QList<CleanupItem>> m_itemsByCategory;
    QHash<CleanupCategoryId, int> m_rowByCategory;
    bool m_scanning{false};
    bool m_updatingTable{false}; // tránh đệ quy khi tự set trạng thái checkbox bằng code
};
