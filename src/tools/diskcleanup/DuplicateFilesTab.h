#pragma once

#include <QWidget>

#include "engine/DuplicateFinder.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTreeWidget;
class QTreeWidgetItem;

/// Tab "Tìm tệp trùng lặp": chọn thư mục gốc, quét tìm các tệp có NỘI DUNG giống hệt nhau
/// (DuplicateFinder), hiển thị theo nhóm (QTreeWidget: nhóm -> từng tệp), cho chọn các bản để xóa
/// (mặc định GIỮ LẠI bản đầu tiên của mỗi nhóm, tick sẵn các bản còn lại) và xóa vào Thùng rác.
class DuplicateFilesTab : public QWidget
{
    Q_OBJECT

public:
    explicit DuplicateFilesTab(QWidget* parent = nullptr);
    ~DuplicateFilesTab() override;

    /// Đang quét (QThread DuplicateFinder) - dùng để DiskCleanupWindow::isAnyOtherTabBusy() chặn tab
    /// khác bắt đầu thao tác chồng chéo trên cùng ổ đĩa (an toàn hủy giữa chừng, khác resize/xóa thật).
    bool isScanningNow() const;

    /// Yêu cầu dừng quét đang chạy (nếu có), KHÔNG chờ (không chặn luồng giao diện) - gọi từ
    /// DiskCleanupWindow::closeEvent() khi đóng cửa sổ (chỉ ẨN, không hủy tab - xem closeEvent()).
    void stopScanIfRunning();

private slots:
    void onBrowseClicked();
    void onScanClicked();
    void onStopClicked();
    void onProgressTick(qint64 filesScanned, qint64 filesHashed, QString currentPath);
    void onScanFinished(QList<DuplicateGroup> groups, qint64 wastedBytes, int totalGroupsFound);
    void onScanStopped();
    void onDeleteSelectedClicked();
    void onItemChanged(QTreeWidgetItem* item, int column);

private:
    void buildUi();
    void addGroupToTree(const DuplicateGroup& group);
    void updateSelectedSummary();

    DuplicateFinder* m_finder{nullptr};

    QPushButton* m_browseBtn{nullptr};
    QLabel* m_pathLabel{nullptr};
    QSpinBox* m_minSizeSpin{nullptr}; // đơn vị KB
    QPushButton* m_scanBtn{nullptr};
    QPushButton* m_stopBtn{nullptr};
    QPushButton* m_deleteBtn{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_summaryLabel{nullptr};
    QTreeWidget* m_tree{nullptr}; // cấp 1 = nhóm, cấp 2 = từng tệp (có checkbox)

    QString m_rootPath;
    bool m_updatingTree{false}; // tránh đệ quy khi tự set checkbox bằng code
};
