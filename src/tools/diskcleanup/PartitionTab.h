#pragma once

#include <QWidget>

#include <QElapsedTimer>

#include <functional>

#include "engine/DiskLayout.h"
#include "engine/PartitionManager.h"
#include "engine/PartitionRelocator.h"

class QThread;

namespace PartitionMover
{
class MoveWorker;
class PlanWorker;
}

class QDoubleSpinBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class DiskMapWidget;
class PartitionResizer;

/// Từ v1.25.0 tab còn DỜI phân vùng và kéo MÉP TRÁI (Windows không có API cho việc này nên ứng dụng tự chép dữ liệu - PartitionMover): chỉ phân vùng dữ liệu
/// NTFS (không hệ thống/khởi động/ổ Windows), ổ phải khóa được. Kế hoạch (DiskLayout::planChange) hiện ngay dưới sơ đồ; thao tác thật vẫn phải gõ lại tên
/// ổ + xác nhận, có nhật ký phục hồi nếu mất điện giữa chừng (banner "Tiếp tục" ở đầu tab).
///
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

    /// Hình học (vị trí + kích thước, byte) mà thay đổi hiện tại sẽ đưa phân vùng đang chọn tới; {-1,-1} nếu chưa chọn/chưa tra.
    DiskLayout::Geometry plannedGeometry() const;
    /// Kế hoạch cho hình học hiện tại (chỉ đọc). blockReason không rỗng = chưa thực hiện được.
    DiskLayout::ChangePlan currentPlan() const;
    /// Rỗng nếu phân vùng đang chọn dời được (Admin, NTFS dữ liệu, không hệ thống...); ngược lại là lý do. Dùng cho giao diện + test.
    QString moveSupportReason() const;
    /// true nếu chính ứng dụng đang chạy từ ổ của phân vùng đang chọn: khi dời, ứng dụng tự chép mình sang thư mục tạm, chạy lại ở đó (UAC) rồi đóng để nhả ổ.
    bool needsRelocation() const;
    /// Có nhật ký dời dở dang (banner "Tiếp tục") không.
    bool hasPendingJournal() const { return m_pendingJournal; }
    /// CHỈ cho test UI: ép coi mọi phân vùng đã tra là dời được (bỏ qua Admin/loại phân vùng) - KHÔNG bao giờ chạy thật (nút Áp dụng vẫn kiểm Admin).
    void setMoveSupportForTest(bool forced) { m_moveForcedForTest = forced; updateMoveSupport(); }
    /// CHỈ cho test UI: nạp dữ liệu đĩa/phân vùng GIẢ vào bảng + sơ đồ (không PowerShell, không đụng đĩa).
    void setDataForTest(const QList<PartitionManager::DiskInfo>& disks, const QList<PartitionManager::PartitionInfo>& partitions);
    /// CHỈ cho test UI: đọc lại nhật ký dời dở dang (banner) sau khi test tự ghi/xóa tệp nhật ký.
    void refreshPendingJournalForTest() { checkPendingJournal(); updateMoveSupport(); }

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
    void onMapGeometryChanged(qint64 startBytes, qint64 sizeBytes);
    void onNewStartChanged(double startGb);
    void onPlanProgress(int stepIndex, int stepCount, qint64 doneBytes, qint64 totalBytes, QString phase);
    void onPlanFinished(bool ok, bool resumable, QString message, int failedStep);
    void onPauseClicked();
    void onResumeClicked();
    void onAbandonClicked();

private:
    void onDirectionButtonClicked(DiskLayout::ResizeDirection direction); // gọi từ lambda của 3 nút hướng
    void buildUi();
    void updateMoveSupport();
    void checkPendingJournal();
    void setOperationUi(bool running);
    void startPlan(const DiskLayout::ChangePlan& plan, const QString& name, int disk, int partition, const QString& letter);
    /// Ứng dụng đang chạy từ ổ cần tháo: chép bộ chạy tối thiểu sang %TEMP%, chạy lại ở đó với quyền Admin (UAC) rồi thoát. false nếu thất bại (đã báo người dùng).
    bool startRelocated(const PartitionRelocator::Plan& plan);
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
    QLabel* m_startLabel{nullptr};
    QDoubleSpinBox* m_startSpin{nullptr}; // vị trí bắt đầu mới (GB từ đầu đĩa) - chỉ hiện khi dời được
    QCheckBox* m_experimentalCheck{nullptr}; // ô xác nhận THỬ NGHIỆM: dời/kéo mép trái chỉ bật khi tích (mặc định tắt)
    QLabel* m_pendingBanner{nullptr};
    QPushButton* m_resumeBtn{nullptr};
    QPushButton* m_abandonBtn{nullptr};
    QPushButton* m_pauseBtn{nullptr};
    QLabel* m_progressLabel{nullptr};

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
    PartitionMover::PlanWorker* m_plan{nullptr};
    PartitionMover::MoveWorker* m_resumeWorker{nullptr};
    bool m_planActive{false};
    bool m_pendingJournal{false};
    bool m_moveForcedForTest{false};
    qint64 m_queriedStartBytes{-1};
    QElapsedTimer m_copyTimer;
    qint64 m_copyBaseBytes{0};
    QString m_planName;
    QString m_planLetter;
};
