#pragma once

#include <QString>
#include <QStringList>
#include <QThread>
#include <atomic>

/// Một nhóm tệp có NỘI DUNG giống hệt nhau (cùng kích thước + cùng hash SHA-256).
struct DuplicateGroup
{
    QStringList paths;       // >= 2 đường dẫn, nội dung giống hệt nhau
    qint64 sizeEachBytes{0}; // kích thước của MỖI tệp (tất cả bằng nhau trong 1 nhóm)
    QString hashHex;
    /// Thời điểm sửa cuối (FILETIME thô) của từng tệp LÚC QUÉT, song song với 'paths' - để ngay trước
    /// khi xóa kiểm lại tệp có còn đúng là tệp đã được so nội dung không (xem fileUnchanged()).
    QList<qint64> lastWriteTimes;

    /// Giữ lại 1 bản, phần còn lại là dung lượng có thể giải phóng.
    qint64 wastedBytes() const { return sizeEachBytes * static_cast<qint64>(paths.size() - 1); }
};

/**
 * @brief Tìm tệp trùng lặp nội dung trong một thư mục gốc, chạy trên QThread riêng (giống
 * LargeFileScanner). Thuật toán 2 bước để tránh hash mọi tệp (chậm): trước tiên nhóm theo KÍCH
 * THƯỚC (rẻ, không đọc nội dung), chỉ hash các tệp có ít nhất 1 tệp khác cùng kích thước; trong
 * mỗi nhóm kích thước, nhóm tiếp theo bằng hash SHA-256 nội dung để xác nhận THẬT SỰ giống nhau
 * (không chỉ trùng kích thước ngẫu nhiên). Bỏ qua lối tắt/symlink và KHÔNG đi vào junction/symlink
 * thư mục (FsSafety::walkFiles) để không đi lạc sang một thư mục/tệp đích hoàn toàn khác với thư mục
 * gốc. Trước khi băm còn khử trùng theo DANH TÍNH TỆP VẬT LÝ (số sê-ri ổ + chỉ số tệp NTFS): hai đường
 * dẫn cùng trỏ vào MỘT tệp trên đĩa (hardlink, hoặc thấy qua một liên kết thư mục) không phải là hai bản
 * sao - xóa "bản thừa" hoặc không giải phóng gì, hoặc xóa mất tệp duy nhất.
 */
class DuplicateFinder : public QThread
{
    Q_OBJECT

public:
    explicit DuplicateFinder(QObject* parent = nullptr);
    ~DuplicateFinder() override;

    void setRootPath(const QString& path) { m_rootPath = path; }
    void setMinSizeBytes(qint64 bytes) { m_minSizeBytes = bytes; }
    /// Giới hạn số NHÓM trả về (giống LargeFileScanner::setMaxResults) - thư mục như cache trình
    /// duyệt có thể tạo ra hàng nghìn tệp trùng kích thước (các khối cache cùng cỡ cố định), nếu báo
    /// về UI từng nhóm một không giới hạn sẽ làm giao diện phải dựng hàng nghìn mục cây liên tiếp,
    /// có nguy cơ khiến Windows đánh dấu cửa sổ "Không phản hồi". Kết quả được sắp xếp theo dung
    /// lượng lãng phí giảm dần trước khi cắt bớt, nên các nhóm đáng chú ý nhất luôn được giữ lại.
    void setMaxGroups(int max) { m_maxGroups = max; }
    /// Bắt đầu quét - LUÔN dùng hàm này thay cho start() trực tiếp (đặt lại cờ dừng trước khi luồng chạy,
    /// xem CleanupScanner::startScan).
    void startScan(QThread::Priority priority = QThread::InheritPriority);
    void requestStop();

    /// Tệp có còn tồn tại với ĐÚNG kích thước + thời điểm sửa cuối đã ghi nhận lúc quét không. Kết quả
    /// "trùng lặp" chỉ đúng tại thời điểm băm; người dùng có thể để bảng kết quả hàng giờ rồi mới bấm
    /// xóa - một tệp đã bị sửa sau đó KHÔNG còn là bản sao của các tệp kia nữa, xóa nó là mất dữ liệu.
    static bool fileUnchanged(const QString& path, qint64 sizeBytes, qint64 lastWriteTime);

signals:
    void progressTick(qint64 filesScanned, qint64 filesHashed, QString currentPath);
    /// Phát MỘT LẦN DUY NHẤT khi quét xong (không phát từng nhóm một - xem setMaxGroups()), giống
    /// LargeFileScanner::scanFinished(). 'groups' đã được sắp xếp theo wastedBytes() giảm dần và cắt
    /// theo setMaxGroups(); 'totalGroupsFound' là tổng số nhóm THẬT tìm thấy (có thể > groups.size()
    /// nếu bị cắt bớt) để UI báo rõ cho người dùng biết có bị giới hạn hay không.
    void scanFinished(QList<DuplicateGroup> groups, qint64 wastedBytes, int totalGroupsFound);
    void scanStopped();

protected:
    void run() override;

private:
    QString m_rootPath;
    qint64 m_minSizeBytes{4096}; // bỏ qua tệp quá nhỏ theo mặc định (nhiều trùng lặp vô nghĩa, vd tệp rỗng)
    int m_maxGroups{500};
    std::atomic_bool m_stopRequested{false};
};
