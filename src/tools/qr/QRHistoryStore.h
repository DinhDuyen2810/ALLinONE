#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

struct QRHistoryEntry
{
    QDateTime time;
    QString source;   // "generate" hoặc "scan"
    QString typeName; // Loại nội dung (Văn bản, URL, WiFi...)
    QString content;
};

/// Lịch sử tạo/quét, lưu ở AppPaths::profileFile("qr_history.json") - tức
/// %LOCALAPPDATA%\OneForAll\profiles\qr_history.json (tối đa 300 mục).
///
/// HAI BẢN ỨNG DỤNG chạy cùng lúc dùng chung tệp này. Mỗi bản giữ danh sách trong bộ nhớ rồi ghi lại CẢ
/// tệp, nên trước đây bản ghi sau xóa mất mọi mục bản kia vừa thêm. Nay trước mỗi lần ghi (add/removeAt),
/// nếu tệp đã đổi từ lần đọc/ghi gần nhất của bản này (so thời điểm sửa + kích thước) thì nạp lại và GỘP:
/// danh sách trên đĩa là gốc, cộng những thay đổi của chính bản này chưa lên được đĩa.
class QRHistoryStore : public QObject
{
    Q_OBJECT

public:
    static QRHistoryStore& instance();

    /// Một kho riêng trỏ vào 'path' (nạp ngay). Ứng dụng chỉ dùng instance(); hàm khởi tạo này để kiểm thử
    /// tình huống hai bản ứng dụng cùng ghi một tệp.
    explicit QRHistoryStore(const QString& path, QObject* parent = nullptr);

    const QList<QRHistoryEntry>& entries() const { return m_entries; }
    void add(const QString& source, const QString& typeName, const QString& content);
    void removeAt(int index);
    void clear();

    /// Đường dẫn file; có thể đổi (dùng cho kiểm thử) trước khi gọi load().
    /// Cho tới lần load()/ghi thành công kế tiếp, tệp ở đường dẫn mới KHÔNG được gộp vào (danh sách đang có
    /// trong bộ nhớ là của tệp cũ) - add() ngay sau đó ghi đúng danh sách đang có, như trước.
    void setFilePath(const QString& path)
    {
        m_path = path;
        m_diskKnown = false;
    }
    QString filePath() const { return m_path; }

    /// Nạp lại từ đĩa. Tệp không tồn tại = lịch sử rỗng (true). Tệp CÓ nội dung nhưng không phải JSON
    /// hợp lệ thì được đổi tên thành "<tên>.bak" (hoặc "<tên>.<thời điểm>.bak" nếu .bak đã có) trước khi
    /// trả về false - lần add() kế tiếp sẽ ghi tệp mới mà không xóa mất bản hỏng (còn cứu tay được).
    bool load();
    bool save();

    /// Lần ghi đĩa gần nhất (qua add/removeAt/clear) có thành công không.
    bool lastSaveOk() const { return m_lastSaveOk; }

signals:
    void changed();
    /// Ghi tệp lịch sử thất bại (đĩa đầy, không có quyền ghi...) - thay đổi vừa rồi CHỈ còn trong bộ nhớ,
    /// sẽ mất khi thoát ứng dụng.
    void saveFailed(const QString& path);

private:
    QRHistoryStore();

    /// save() + ghi log/phát saveFailed() nếu thất bại - mọi thao tác sửa lịch sử đi qua đây.
    void persist();

    /// Phần đọc tệp của load() (không đụng tới trạng thái đồng bộ bên dưới).
    bool readFile();
    /// Ghi nhớ thời điểm sửa + kích thước hiện tại của tệp và coi danh sách đang có là "đã khớp với đĩa".
    void rememberDiskState();
    /// Nạp lại + gộp nếu tệp đã bị một bản ứng dụng khác ghi từ lần đọc/ghi gần nhất - gọi TRƯỚC khi sửa
    /// danh sách để ghi.
    void syncWithDisk();

    static constexpr int kMaxEntries = 300;
    QString m_path;
    QList<QRHistoryEntry> m_entries; // mới nhất ở đầu
    bool m_lastSaveOk{true};

    // Trạng thái của tệp tại lần đọc/ghi thành công gần nhất của CHÍNH đối tượng này.
    bool m_diskKnown{false};
    bool m_diskExists{false};
    QDateTime m_diskModified;
    qint64 m_diskSize{-1};
    QList<QRHistoryEntry> m_synced; // danh sách tại thời điểm đó - để biết bản này đã tự thêm/xóa gì từ ấy
};
