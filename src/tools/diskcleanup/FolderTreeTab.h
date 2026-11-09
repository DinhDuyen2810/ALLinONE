#pragma once

#include "engine/FolderTreeScanner.h"

#include <QWidget>

#include <memory>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTreeView;
class FolderTreeModel;

/// Tab "Cây thư mục": quét TOÀN BỘ một ổ đĩa/thư mục rồi hiện cây thư mục sắp theo dung lượng giảm dần (kèm % của
/// thư mục cha) để thấy ngay thư mục nào chiếm nhiều chỗ. Chỉ XEM - không xóa/sửa gì (muốn dọn thì "Mở trong
/// Explorer" hoặc dùng tab "Tìm tệp lớn").
class FolderTreeTab : public QWidget
{
    Q_OBJECT

public:
    explicit FolderTreeTab(QWidget* parent = nullptr);
    ~FolderTreeTab() override;

    bool isScanningNow() const;
    /// Yêu cầu dừng lượt quét đang chạy (an toàn: quét chỉ đọc).
    void stopScanIfRunning();

    /// Đặt thư mục sẽ quét (thay cho hộp thoại "Chọn thư mục khác...") - dùng cho test.
    void setScanRoot(const QString& path);

private slots:
    void onScanClicked();
    void onStopClicked();
    void onBrowseClicked();
    void onProgressTick(qint64 files, qint64 folders, qint64 bytes, QString currentFolder);
    void onScanFinished(std::shared_ptr<FolderTree> tree);
    void onScanStopped();
    void onSelectionChanged();
    void onContextMenu(const QPoint& pos);
    void onExpandHeaviestClicked();
    void onOpenInExplorerClicked();
    void onCopyPathClicked();

private:
    void buildUi();
    QString selectedPath() const;
    void updateDriveInfo();
    void setScanningUi(bool scanning);
    QString buildSummary(const FolderTree& tree) const;

    QComboBox* m_driveCombo{nullptr};
    QPushButton* m_browseBtn{nullptr};
    QLabel* m_driveLabel{nullptr};
    QProgressBar* m_driveBar{nullptr};
    QLabel* m_pathLabel{nullptr};
    QTreeView* m_view{nullptr};
    FolderTreeModel* m_model{nullptr};
    QProgressBar* m_busyBar{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_summaryLabel{nullptr};
    QPushButton* m_scanBtn{nullptr};
    QPushButton* m_stopBtn{nullptr};
    QPushButton* m_expandBtn{nullptr};
    QPushButton* m_collapseBtn{nullptr};
    QPushButton* m_heaviestBtn{nullptr};
    QPushButton* m_explorerBtn{nullptr};
    QPushButton* m_copyBtn{nullptr};

    FolderTreeScanner* m_scanner{nullptr};
    QString m_customRoot;
    QString m_scannedRoot;
};
