#pragma once

#include <QWidget>

#include "engine/PartitionManager.h"

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class PartitionResizer;

/// Tab "Quản lý phân vùng": liệt kê phân vùng đĩa thật, đổi kích thước qua Resize-Partition - CÙNG
/// cơ chế an toàn mà "Shrink Volume"/"Extend Volume" của Disk Management (Windows) dùng, xem
/// PartitionManager.h. Đây là THAO TÁC ĐĨA THẬT, có rủi ro mất dữ liệu thật nếu có sự cố giữa chừng
/// (mất điện, tắt máy đột ngột) - luôn hiện cảnh báo rõ ràng và yêu cầu gõ đúng tên ổ đĩa để xác nhận
/// trước khi thực hiện, không cho xác nhận bằng một cú bấm duy nhất.
class PartitionTab : public QWidget
{
    Q_OBJECT

public:
    explicit PartitionTab(QWidget* parent = nullptr);
    ~PartitionTab() override;

private slots:
    void onRefreshClicked();
    void onRelaunchElevatedClicked();
    void onRowSelectionChanged();
    void onQuerySupportedSizeClicked();
    void onResizeClicked();
    void onResizeFinished(bool success, QString error);
    void onConfirmTextChanged(const QString& text);

private:
    void buildUi();
    void reloadPartitions();
    void updateElevationBanner();
    void showResizeControls(bool visible);
    const PartitionManager::PartitionInfo* selectedPartition() const;

    QLabel* m_elevationBanner{nullptr};
    QPushButton* m_relaunchBtn{nullptr};
    QTableWidget* m_table{nullptr};
    QPushButton* m_refreshBtn{nullptr};
    QLabel* m_statusLabel{nullptr};

    // Khu vực đổi kích thước - chỉ hiện sau khi chọn 1 phân vùng và tra được kích thước cho phép
    QLabel* m_detailLabel{nullptr};
    QPushButton* m_queryBtn{nullptr};
    QLabel* m_warningLabel{nullptr};
    QLabel* m_sizeLabel{nullptr};
    QDoubleSpinBox* m_newSizeSpin{nullptr}; // đơn vị GB
    QLabel* m_confirmHintLabel{nullptr};
    QLineEdit* m_confirmEdit{nullptr};
    QPushButton* m_resizeBtn{nullptr};
    QProgressBar* m_progressBar{nullptr};

    QList<PartitionManager::PartitionInfo> m_partitions;
    PartitionManager::SupportedSizeRange m_supportedRange;
    PartitionResizer* m_resizer{nullptr};
};
