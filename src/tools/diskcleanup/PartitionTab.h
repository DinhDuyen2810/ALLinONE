#pragma once

#include <QWidget>

#include <functional>

#include "engine/PartitionManager.h"

class QThread;

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

    /// Đang đổi kích thước phân vùng THẬT - KHÔNG an toàn để hủy giữa chừng (có thể làm hỏng hệ thống
    /// tệp), khác hẳn các thao tác khác trong ứng dụng (quét/dọn dẹp/kết nối VPN) vốn hủy được an toàn.
    bool isResizingNow() const;

    /// Đang chờ Windows trả lời một truy vấn CHỈ ĐỌC (liệt kê phân vùng/tra khoảng kích thước) trên luồng
    /// nền - bảng và các nút bị khóa trong lúc đó.
    bool isLoading() const;

    /// Rỗng nếu trạng thái giao diện HIỆN TẠI cho phép đi tiếp tới hộp xác nhận đổi kích thước; ngược lại
    /// là lý do chặn (chưa chọn, chưa tra, phân vùng đang chọn KHÁC phân vùng đã tra, chưa gõ xác nhận).
    /// Chỉ đọc trạng thái, không làm gì - bộ test UI dùng để kiểm tra lớp chặn mà không chạy resize.
    QString resizeRequestBlockReason() const;

    /// CHỈ dành cho bộ test UI: gán một khoảng kích thước giả cho phân vùng đang chọn như thể vừa tra
    /// xong (tra thật cần quyền Administrator). Không gọi PowerShell, không đụng tới đĩa.
    void applySupportedRangeForTest(const PartitionManager::SupportedSizeRange& range);

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
    void populatePartitions(const QList<PartitionManager::PartitionInfo>& partitions, const QString& error);
    void updateElevationBanner();
    void showSelectionControls(bool visible);
    void resetResizeState();
    void applySupportedRange(int disk, int partition, qint64 sizeBytes,
                             const PartitionManager::SupportedSizeRange& range, const QString& error);
    void setLoading(bool loading, const QString& statusText = QString());
    void runInBackground(const QString& statusText, std::function<void()> work, std::function<void()> done);
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
    // Phân vùng mà m_supportedRange/ô nhập/ô xác nhận THUỘC VỀ (-1 = chưa tra) - xem resetResizeState().
    int m_queriedDisk{-1};
    int m_queriedPartition{-1};
    qint64 m_queriedSizeBytes{-1};
    bool m_loading{false};
    QThread* m_queryThread{nullptr};
    QString m_statusAfterReload;
    PartitionResizer* m_resizer{nullptr};
};
