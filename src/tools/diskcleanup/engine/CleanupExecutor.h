#pragma once

#include <QList>
#include <QStringList>
#include <QThread>

#include <atomic>

#include "../model/CleanupCategory.h"
#include "../model/CleanupItem.h"

/// Một việc xóa giao cho CleanupExecutor (v1.32.0).
struct CleanupTask
{
    enum class Mode
    {
        /// Dữ liệu của người dùng: qua Windows Shell - Thùng rác (mặc định) hoặc xóa vĩnh viễn theo ô "Xóa vĩnh viễn".
        Shell,
        /// Dữ liệu tự tạo lại (cache/tạm/dump/nhật ký): xóa HẲN từng tệp bằng SafePurge trong trustedRoot - bỏ qua tệp
        /// đang mở, giải phóng dung lượng thật.
        Purge,
        /// Làm trống Thùng rác (SHEmptyRecycleBinW) - luôn chạy TRƯỚC mọi việc khác để không xóa luôn thứ vừa đưa vào.
        EmptyRecycleBin,
        /// Tắt ngủ đông (powercfg /hibernate off, nâng quyền) - Windows tự gỡ hiberfil.sys.
        TurnOffHibernation,
    };
    QString path;
    qint64 sizeBytes{0};
    int minAgeSeconds{0};
    Mode mode{Mode::Shell};
    QString trustedRoot; // Purge: mọi thứ bị xóa phải thật sự nằm trong thư mục (hoặc là tệp) này

    /// Việc xóa tương ứng MỘT mục đã quét của một hạng mục: Thùng rác -> làm trống; ngủ đông -> powercfg; dữ liệu tự
    /// tạo lại có thư mục gốc -> xóa hẳn từng tệp trong gốc đó; còn lại (dữ liệu của người dùng) -> qua Shell.
    static CleanupTask forItem(const CleanupCategory& category, const CleanupItem& item);
};

/**
 * @brief Thực hiện xóa các mục đã chọn, trên QThread riêng (có thể mất thời gian với thư mục lớn).
 *
 * Từ v1.32.0 (tham khảo roomy): dữ liệu TỰ TẠO LẠI (bộ nhớ đệm, tệp tạm...) xóa HẲN từng tệp (SafePurge) - đưa
 * chúng vào Thùng rác không giải phóng byte nào; dữ liệu của NGƯỜI DÙNG (bộ cài cũ, tệp tải về, Windows.old,
 * thư mục mồ côi) vẫn mặc định vào Thùng rác qua RecycleBinOps, xóa vĩnh viễn chỉ khi người dùng bật tùy chọn đó.
 *
 * Giữa lúc quét và lúc người dùng bấm Dọn dẹp, một số tệp (đặc biệt là tệp tạm/cache) có thể đã tự
 * biến mất do Windows hoặc ứng dụng khác xóa - đây là chuyện BÌNH THƯỜNG, không phải lỗi, và mục tiêu
 * (tệp đó không còn chiếm dung lượng) coi như đã đạt được. CleanupExecutor tự kiểm tra tồn tại trước
 * khi xóa và sau khi xóa (không tin mù quáng vào mã lỗi của SHFileOperationW, vốn không cho biết
 * CHÍNH XÁC mục nào thất bại khi có lỗi giữa chừng) để báo cáo freedBytes/deletedCount THẬT.
 */
class CleanupExecutor : public QThread
{
    Q_OBJECT

public:
    explicit CleanupExecutor(QObject* parent = nullptr);
    ~CleanupExecutor() override;

    /// 'paths' và 'sizes' tương ứng theo chỉ số (sizes[i] là kích thước ghi nhận lúc quét của paths[i]).
    /// 'minAgeSeconds' (tùy chọn, cùng chỉ số): tuổi tối thiểu của mục theo hạng mục của nó
    /// (CleanupCategory::minAgeSeconds; 0/thiếu = không giới hạn). Bộ lọc "vừa sửa gần đây" của máy quét chỉ
    /// đúng tại THỜI ĐIỂM QUÉT - người dùng có thể bấm Dọn dẹp nhiều phút sau, khi một chương trình đã ghi
    /// tiếp vào tệp tạm đó. Mục có giới hạn được kiểm lại ngay trước khi xóa; mục vừa được sửa thì GIỮ LẠI.
    /// Mọi mục giao qua hàm này là việc Mode::Shell (giữ tương thích).
    void setItems(const QStringList& paths, const QList<qint64>& sizes, const QList<int>& minAgeSeconds = {});
    /// Đặt lại cờ dừng - gọi setTasks()/setItems() trước MỖI lần start().
    void setTasks(const QList<CleanupTask>& tasks)
    {
        m_tasks = tasks;
        m_stopRequested = false;
    }
    void setPermanentDelete(bool permanent) { m_permanent = permanent; }
    /// Dừng sớm (chỉ phần xóa từng tệp - lệnh Shell đang chạy thì chạy hết lô hiện tại).
    void requestStop() { m_stopRequested = true; }

signals:
    /// 'success' = false CHỈ KHI không có mục nào được giải phóng dù có mục để xóa (thất bại thật sự).
    /// Thành công một phần (vd 349/350 mục) vẫn báo success=true, kèm 'error' là một GHI CHÚ (không
    /// phải lỗi nghiêm trọng) mô tả số mục không xóa được - UI tự quyết định có hiển thị ghi chú đó
    /// hay không, không coi đó là một thất bại cần cảnh báo đỏ.
    /// freedBytes: tổng đã RỜI khỏi vị trí cũ (gồm cả phần vào Thùng rác); recycledBytes: phần đã vào Thùng rác
    /// (chưa thật sự giải phóng trên đĩa cho tới khi làm trống Thùng rác). deletedCount: số tệp/mục đã xóa.
    void executionFinished(bool success, QString error, qint64 freedBytes, int deletedCount, qint64 recycledBytes);
    /// Tiến độ (byte đã xử lý / tổng byte dự kiến) - phát thưa (~5 lần/giây).
    void progress(qint64 doneBytes, qint64 totalBytes);

protected:
    void run() override;

private:
    QList<CleanupTask> m_tasks;
    bool m_permanent{false};
    std::atomic_bool m_stopRequested{false};
};
