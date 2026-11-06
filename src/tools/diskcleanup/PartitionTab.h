#pragma once

#include <QWidget>

#include <functional>

#include "engine/DiskLayout.h"
#include "engine/PartitionManager.h"

class QThread;

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class DiskMapWidget;
class PartitionResizer;

/// Tab "Quản lý phân vùng": liệt kê phân vùng đĩa thật, đổi kích thước qua Resize-Partition - CÙNG
/// cơ chế an toàn mà "Shrink Volume"/"Extend Volume" của Disk Management (Windows) dùng, xem
/// PartitionManager.h. Đây là THAO TÁC ĐĨA THẬT, có rủi ro mất dữ liệu thật nếu có sự cố giữa chừng
/// (mất điện, tắt máy đột ngột) - luôn hiện cảnh báo rõ ràng và yêu cầu gõ đúng tên ổ đĩa để xác nhận
/// trước khi thực hiện, không cho xác nhận bằng một cú bấm duy nhất.
///
/// Sơ đồ đĩa (DiskMapWidget) kiểu Disk Management nằm trên bảng: bấm khối để chọn phân vùng; sau khi tra
/// khoảng kích thước cho phép, KÉO TAY NẮM ở mép phải khối: sang TRÁI = thu nhỏ, sang PHẢI = mở rộng. Hàng
/// "Hướng thay đổi" cho phép KHÓA hướng (chỉ thu nhỏ / chỉ mở rộng / cả hai) để không kéo nhầm. Kéo chỉ đổi
/// kích thước XEM TRƯỚC (đồng bộ với ô nhập) - thao tác thật vẫn phải gõ xác nhận rồi bấm "Đổi kích thước".
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

    /// Sơ đồ đĩa + hướng đang khóa - bộ test UI dùng để kiểm tra đồng bộ kéo tay nắm <-> ô nhập <-> nút hướng.
    DiskMapWidget* diskMap() const { return m_diskMap; }
    DiskLayout::ResizeDirection resizeDirection() const { return m_direction; }

private slots:
    void onRefreshClicked();
    void onRelaunchElevatedClicked();
    void onRowSelectionChanged();
    void onQuerySupportedSizeClicked();
    void onResizeClicked();
    void onResizeFinished(bool success, QString error);
    void onConfirmTextChanged(const QString& text);
    void onMapPartitionClicked(int diskNumber, int partitionNumber);
    void onMapPreviewChanged(qint64 bytes);
    void onNewSizeChanged(double sizeGb);

private:
    void onDirectionButtonClicked(DiskLayout::ResizeDirection direction); // gọi từ lambda của 3 nút hướng
    void buildUi();
    void reloadPartitions();
    void populatePartitions(const QList<PartitionManager::PartitionInfo>& partitions,
                            const QList<PartitionManager::DiskInfo>& disks, const QString& error);
    /// Áp hướng đã khóa lên ô nhập (khoảng giá trị) và sơ đồ; 'resetToCurrent' = đặt lại kích thước mới = hiện tại.
    void applyDirectionBounds(bool resetToCurrent);
    void updateChangePreview();
    QString selectedDisplayName() const;
    /// Kích thước mới (byte) mà giá trị hiện trong ô nhập sẽ áp dụng: = kích thước hiện tại nếu chưa sửa ở độ chính
    /// xác của ô, ngược lại là giá trị đã kẹp vào khoảng Windows cho phép. 0 nếu chưa có khoảng hợp lệ.
    qint64 effectiveNewSizeBytes(const PartitionManager::PartitionInfo& p) const;
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
    DiskMapWidget* m_diskMap{nullptr};
    QTableWidget* m_table{nullptr};
    QPushButton* m_refreshBtn{nullptr};
    QLabel* m_statusLabel{nullptr};

    // Khu vực đổi kích thước - chỉ hiện sau khi chọn 1 phân vùng và tra được kích thước cho phép
    QLabel* m_detailLabel{nullptr};
    QPushButton* m_queryBtn{nullptr};
    QLabel* m_warningLabel{nullptr};
    QWidget* m_directionRow{nullptr};
    QPushButton* m_dirShrinkBtn{nullptr};
    QPushButton* m_dirFreeBtn{nullptr};
    QPushButton* m_dirGrowBtn{nullptr};
    QLabel* m_sizeLabel{nullptr};
    QDoubleSpinBox* m_newSizeSpin{nullptr}; // đơn vị GB
    QLabel* m_changeLabel{nullptr};   // mô tả thay đổi dự kiến (thu nhỏ/mở rộng bao nhiêu, vùng giải phóng nằm phía nào)
    QLabel* m_edgeNoteLabel{nullptr}; // nhắc: Windows chỉ đổi kích thước bằng mép PHẢI
    QLabel* m_confirmHintLabel{nullptr};
    QLineEdit* m_confirmEdit{nullptr};
    QPushButton* m_resizeBtn{nullptr};
    QProgressBar* m_progressBar{nullptr};

    QList<PartitionManager::PartitionInfo> m_partitions;
    QList<PartitionManager::DiskInfo> m_disks;
    DiskLayout::ResizeDirection m_direction{DiskLayout::ResizeDirection::Free};
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
