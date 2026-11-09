#pragma once

#include <QString>
#include <QStringList>
#include <QThread>

#include <QMutex>

#include <atomic>
#include <vector>

#include "DiskLayout.h"
#include "PartitionManager.h"
#include "PartitionMoveCore.h"

/**
 * @brief DỜI một phân vùng NTFS dữ liệu trên đĩa THẬT (cần quyền Administrator): khóa + tháo ổ, chép dữ liệu sang vị trí mới bằng
 * PartitionMoveCore (có nhật ký phục hồi), vá boot sector, đổi bảng phân vùng bằng IOCTL_DISK_SET_DRIVE_LAYOUT_EX rồi chờ Windows gắn lại.
 *
 * Windows không có API dời phân vùng (Resize-Partition chỉ đổi mép phải) nên đây là PHẦN DUY NHẤT của ứng dụng tự ghi thẳng xuống đĩa. Vì vậy
 * giới hạn rất chặt: chỉ phân vùng dữ liệu NTFS (không phải hệ thống/khởi động/ổ Windows, không BitLocker, không đĩa động, không phân vùng
 * logic MBR), ổ phải khóa được (không có chương trình nào đang mở) và không "bẩn" (chưa cần chkdsk), và đích phải là vùng CHƯA PHÂN BỔ.
 * Mọi điều kiện được kiểm lại từ Windows ngay trước khi ghi. Chi tiết bất biến an toàn: PartitionMoveCore.h.
 */
namespace PartitionMover
{
struct MoveRequest
{
    int diskNumber{-1};
    int partitionNumber{-1};
    qint64 expectedStart{0};  // vị trí hiện tại mà người dùng đã thấy + xác nhận (byte từ đầu đĩa)
    qint64 expectedLength{0}; // độ dài hiện tại của phân vùng (byte)
    qint64 newStart{0};       // vị trí bắt đầu mới
    QString driveLetter;      // "D" hoặc rỗng - để gán lại chữ ổ nếu Windows làm mất
};

/// Rỗng nếu phân vùng này CÓ THỂ dời; ngược lại là lý do (loại, hệ thống tệp, khởi động/hệ thống/ổ Windows...). Chỉ đọc thông tin đã có.
QString notMovableReason(const PartitionManager::PartitionInfo& partition);

/// Tên các tiến trình đang chạy từ ổ có chữ này ("D"; không phân biệt hoa thường) - dùng để báo cho người dùng biết cái gì đang giữ ổ khi không khóa được.
/// Chỉ đọc danh sách tiến trình. Rỗng nếu không có (hoặc không đọc được đường dẫn - tiến trình hệ thống khi không có quyền Admin).
QStringList processesRunningFromDrive(const QString& driveLetter);
/// Lý do KHÔNG thể tháo ổ này vì chính ứng dụng đang chạy từ đó (thư mục chứa exe hoặc thư mục dữ liệu nằm trên ổ); rỗng nếu không. Thư mục làm việc
/// hiện tại nằm trên ổ thì KHÔNG phải lý do (ứng dụng tự chuyển nó sang thư mục tạm khi chuẩn bị dời - xem preflightMove).
QString runningFromDriveReason(const QString& driveLetter);
/// Kiểm tra TRƯỚC khi đổi bất cứ thứ gì (kể cả bước thu nhỏ của Windows): ổ có khóa được không (thử khóa rồi mở khóa ngay, KHÔNG tháo), cờ bẩn,
/// ứng dụng không chạy từ ổ này. Rỗng nếu ổn; ngược lại là lý do rõ ràng (kèm danh sách chương trình đang giữ ổ). Cần quyền Administrator; không ghi gì xuống đĩa.
QString preflightMove(int diskNumber, int partitionNumber, const QString& driveLetter);

/// Tệp nhật ký (một lần dời tại một thời điểm): %LOCALAPPDATA%\OneForAll\partition_move_journal.json.
QString journalPath();
/// Đọc nhật ký. false + error rỗng = không có nhật ký; false + error = có nhưng hỏng (KHÔNG dùng).
bool loadJournal(PartitionMove::JournalState* out, QString* error = nullptr);
/// Xóa nhật ký - CHỈ khi chắc chắn không còn gì phải phục hồi (xong, hoặc chưa ghi gì lên vùng nguồn).
void discardJournal();
/// Mô tả nhật ký cho người đọc (tiếng Việt): đang dời đĩa/phân vùng nào, tới đâu.
QString describeJournal(const PartitionMove::JournalState& state);
/// true nếu hủy bỏ hẳn lần dời này KHÔNG làm mất dữ liệu (vùng đích không chồng lên nguồn, hoặc chưa ghi gì) - nếu false thì chỉ có "Tiếp tục".
bool canAbandon(const PartitionMove::JournalState& state);

// ---- Lõi thuần của việc đổi bảng phân vùng (test được bằng bộ đệm dựng tay, không cần đĩa) ----
namespace internal
{
/// Bộ đệm DRIVE_LAYOUT_INFORMATION_EX do IOCTL_DISK_GET_DRIVE_LAYOUT_EX trả về.
struct LayoutView
{
    bool ok{false};
    bool gpt{false};
    int count{0};
    qint64 usableStart{0};  // GPT: StartingUsableOffset; MBR: 0
    qint64 usableLength{0}; // GPT: UsableLength; MBR: 0 (không giới hạn)
    QString diskId;
};
LayoutView inspectLayout(const std::vector<char>& raw);
/// Vị trí/độ dài của phân vùng số 'partitionNumber'; false nếu không có.
bool findEntry(const std::vector<char>& raw, int partitionNumber, qint64* start, qint64* length, QString* partitionGuid);
/// Rỗng nếu hình học đích (newStart, độ dài length) của phân vùng này nằm gọn trong khoảng dùng được của đĩa và KHÔNG chồng lên phân vùng nào khác.
QString checkDestination(const std::vector<char>& raw, int partitionNumber, qint64 newStart, qint64 length, qint64 diskSizeBytes);
/// Sửa TRỰC TIẾP bộ đệm: chỉ đổi vị trí bắt đầu của phân vùng này (và HiddenSectors nếu MBR), đặt RewritePartition cho đúng mục đó. false + error nếu không tìm thấy.
bool patchStart(std::vector<char>* raw, int partitionNumber, qint64 newStart, qint64 sectorBytes, QString* error);
/// Kích thước hợp lệ (byte) của bộ đệm cho IOCTL_DISK_SET_DRIVE_LAYOUT_EX (bỏ phần dư của bộ đệm GET).
size_t layoutByteSize(const std::vector<char>& raw);
} // namespace internal

/// Luồng thực hiện dời. Một đối tượng chạy được nhiều lần (mỗi lần start() sau khi đặt lại tham số).
class MoveWorker : public QThread
{
    Q_OBJECT

public:
    explicit MoveWorker(QObject* parent = nullptr);

    /// Dời mới theo yêu cầu.
    void setRequest(const MoveRequest& request);
    /// Chạy tiếp một lần dời dở dang theo nhật ký (không cần yêu cầu).
    void setResume(const PartitionMove::JournalState& journal, const QString& driveLetter);
    /// Tạm dừng (dừng đúng ranh giới an toàn, nhật ký giữ chỗ dừng). Đặt lại ở mỗi lần bắt đầu.
    void requestPause() { m_pause = true; }

signals:
    /// phase: mô tả bước đang làm; done/total: số byte đã chép/tổng (0/0 khi không phải bước chép).
    void progress(qint64 doneBytes, qint64 totalBytes, QString phase);
    /// ok = đổi xong hoàn toàn. resumable = chưa xong nhưng nhật ký còn, "Tiếp tục" được.
    void moveFinished(bool ok, bool resumable, QString message);

protected:
    void run() override;

private:
    MoveRequest m_request;
    PartitionMove::JournalState m_journal;
    QString m_resumeLetter;
    bool m_resume{false};
    std::atomic_bool m_pause{false};
};

/// Thực hiện CẢ KẾ HOẠCH (DiskLayout::planChange) lần lượt từng bước trên một luồng: thu nhỏ/mở rộng mép phải qua Windows (Resize-Partition, đọc lại + đối chiếu
/// từ Windows ngay trước mỗi bước - như PartitionResizer) và dời dữ liệu qua MoveWorker. Dừng ngay ở bước đầu tiên lỗi; các bước đã xong KHÔNG bị hoàn tác
/// (mỗi bước đều để đĩa ở trạng thái nhất quán). Chỉ bước chép dữ liệu mới tạm dừng/chạy tiếp được.
class PlanWorker : public QThread
{
    Q_OBJECT

public:
    explicit PlanWorker(QObject* parent = nullptr);

    void setPlan(int diskNumber, int partitionNumber, const QList<DiskLayout::PlanStep>& steps, const QString& driveLetter);
    void requestPause();

signals:
    /// stepIndex: bước đang chạy (từ 0). done/total: byte đã chép/tổng (0/0 ngoài bước chép).
    void progress(int stepIndex, int stepCount, qint64 doneBytes, qint64 totalBytes, QString phase);
    /// ok = xong cả kế hoạch. resumable = dừng giữa bước chép, nhật ký còn (xem loadJournal) - "Tiếp tục" được. failedStep = chỉ số bước lỗi (từ 0); -1 = ok; -2 = KIỂM TRA TRƯỚC không đạt (chưa đổi gì - thử lại an toàn).
    void planFinished(bool ok, bool resumable, QString message, int failedStep);

protected:
    void run() override;

private:
    int m_disk{-1};
    int m_partition{-1};
    QList<DiskLayout::PlanStep> m_steps;
    QString m_letter;
    std::atomic_bool m_pause{false};
    QMutex m_mutex;
    MoveWorker* m_current{nullptr};
};
} // namespace PartitionMover
