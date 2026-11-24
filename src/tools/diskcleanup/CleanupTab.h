#pragma once

#include <QHash>
#include <QSet>
#include <QWidget>

#include "engine/CleanupScanner.h"

class CleanupExecutor;
class FlowLayout;
class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

/// Tab chính Disk Cleanup: tổng quan dung lượng ổ đĩa, quét theo hạng mục, chọn và dọn dẹp.
/// v1.32.0: cây hạng mục theo NHÓM (Windows / Trình duyệt / Ứng dụng / Shader / Lập trình / Tệp của bạn); hạng
/// mục "xem kỹ" hiện từng mục để tự tích; ghi chú "đang chạy" / "cần quyền Quản trị" ngay trên dòng.
class CleanupTab : public QWidget
{
    Q_OBJECT

public:
    explicit CleanupTab(QWidget* parent = nullptr);
    ~CleanupTab() override;

    /// Đang xóa THẬT - KHÔNG an toàn để đóng cửa sổ lúc này (lệnh Shell xử lý theo lô, không hủy giữa chừng được).
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
    void onExecutionFinished(bool success, QString error, qint64 freedBytes, int deletedCount, qint64 recycledBytes);
    void onTreeItemChanged(QTreeWidgetItem* item, int column);
    void onRelaunchElevatedClicked();

private:
    void buildUi();
    void reloadDriveOverview();
    void updateSelectedSummary();
    void updateGroupTotals();
    qint64 selectedTotalBytes() const;
    const CleanupCategory* categoryById(CleanupCategoryId id) const;
    /// Ghi chú hiển thị ở cột "Ghi chú" của một hạng mục (đang chạy / cần quyền / note cố định).
    QString categoryNote(const CleanupCategory& c) const;
    /// Tên ứng dụng sở hữu hạng mục đang chạy (rỗng nếu không).
    QString runningLabel(const CleanupCategory& c) const;

    CleanupScanner* m_scanner{nullptr};
    CleanupExecutor* m_executor{nullptr};

    QWidget* m_driveOverview{nullptr};
    // Số thẻ đổi theo số ổ đã mount trên máy (ổ vật lý + ổ ảo như Google Drive for Desktop) - không giới
    // hạn trước được, nên dùng FlowLayout tự xuống dòng thay vì QHBoxLayout (cắt mất thẻ ngoài tầm nhìn
    // khi nhiều ổ, xem CLAUDE.md mục 5 / PROJECT_OVERVIEW.md mục 4r, v1.19.4).
    FlowLayout* m_driveOverviewLayout{nullptr};

    QLabel* m_adminBanner{nullptr};
    QPushButton* m_relaunchBtn{nullptr};
    // Cây: nhóm (không tích) -> hạng mục (tích) -> nguồn (thư mục gốc, chỉ xem) hoặc từng mục (hạng mục "xem kỹ").
    QTreeWidget* m_tree{nullptr};
    QPushButton* m_scanBtn{nullptr};
    QPushButton* m_cleanBtn{nullptr};
    QCheckBox* m_permanentCheck{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_summaryLabel{nullptr};

    QList<CleanupCategory> m_categories; // hạng mục khả dụng trên máy này
    QHash<CleanupCategoryId, QList<CleanupItem>> m_itemsByCategory;
    QHash<CleanupCategoryId, QTreeWidgetItem*> m_categoryNode;
    QHash<QString, QTreeWidgetItem*> m_groupNode;
    QSet<QString> m_runningExes; // tên tệp chạy (chữ thường) lúc bắt đầu quét
    bool m_elevated{false};
    bool m_scanning{false};
    bool m_updatingTree{false}; // tránh đệ quy khi tự set trạng thái ô tích bằng code
};
