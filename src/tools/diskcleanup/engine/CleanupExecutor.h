#pragma once

#include <QStringList>
#include <QThread>

/**
 * @brief Thực hiện xóa các mục đã chọn, trên QThread riêng (có thể mất thời gian với thư mục lớn).
 * Mặc định chuyển vào Thùng rác (có thể khôi phục) qua RecycleBinOps - xóa vĩnh viễn chỉ khi người
 * dùng chủ động bật tùy chọn đó.
 *
 * Giữa lúc quét và lúc người dùng bấm Dọn dẹp, một số tệp (đặc biệt là tệp tạm/cache) có thể đã tự
 * biến mất do Windows hoặc ứng dụng khác xóa - đây là chuyện BÌNH THƯỜNG, không phải lỗi, và mục tiêu
 * (tệp đó không còn chiếm dung lượng) coi như đã đạt được. CleanupExecutor tự kiểm tra tồn tại trước
 * khi xóa và sau khi xóa (không tin mù quáng vào mã lỗi của SHFileOperationW, vốn không cho biết
 * CHÍNH XÁC mục nào thất bại khi có lỗi giữa chừng) để báo cáo số liệu freedBytes/deletedCount THẬT.
 */
class CleanupExecutor : public QThread
{
    Q_OBJECT

public:
    explicit CleanupExecutor(QObject* parent = nullptr);

    /// 'paths' và 'sizes' tương ứng theo chỉ số (sizes[i] là kích thước ghi nhận lúc quét của paths[i]).
    /// 'minAgeSeconds' (tùy chọn, cùng chỉ số): tuổi tối thiểu của mục theo hạng mục của nó
    /// (CleanupCategory::minAgeSeconds; 0/thiếu = không giới hạn). Bộ lọc "vừa sửa gần đây" của máy quét chỉ
    /// đúng tại THỜI ĐIỂM QUÉT - người dùng có thể bấm Dọn dẹp nhiều phút sau, khi một chương trình đã ghi
    /// tiếp vào tệp tạm đó. Mục có giới hạn được kiểm lại ngay trước khi xóa; mục vừa được sửa thì GIỮ LẠI.
    void setItems(const QStringList& paths, const QList<qint64>& sizes, const QList<int>& minAgeSeconds = {});
    void setPermanentDelete(bool permanent) { m_permanent = permanent; }

signals:
    /// 'success' = false CHỈ KHI không có mục nào được giải phóng dù có mục để xóa (thất bại thật sự).
    /// Thành công một phần (vd 349/350 mục) vẫn báo success=true, kèm 'error' là một GHI CHÚ (không
    /// phải lỗi nghiêm trọng) mô tả số mục không xóa được - UI tự quyết định có hiển thị ghi chú đó
    /// hay không, không coi đó là một thất bại cần cảnh báo đỏ.
    void executionFinished(bool success, QString error, qint64 freedBytes, int deletedCount);

protected:
    void run() override;

private:
    QStringList m_paths;
    QList<qint64> m_sizes;
    QList<int> m_minAgeSeconds;
    bool m_permanent{false};
};
