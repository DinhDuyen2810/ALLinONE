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
class QRHistoryStore : public QObject
{
    Q_OBJECT

public:
    static QRHistoryStore& instance();

    const QList<QRHistoryEntry>& entries() const { return m_entries; }
    void add(const QString& source, const QString& typeName, const QString& content);
    void removeAt(int index);
    void clear();

    /// Đường dẫn file; có thể đổi (dùng cho kiểm thử) trước khi gọi load().
    void setFilePath(const QString& path) { m_path = path; }
    QString filePath() const { return m_path; }

    /// Nạp lại từ đĩa. Tệp không tồn tại = lịch sử rỗng (true). Tệp CÓ nội dung nhưng không phải JSON
    /// hợp lệ thì được đổi tên thành "<tên>.bak" (hoặc "<tên>.<thời điểm>.bak" nếu .bak đã có) trước khi
    /// trả về false - lần add() kế tiếp sẽ ghi tệp mới mà không xóa mất bản hỏng (còn cứu tay được).
    bool load();
    bool save() const;

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

    static constexpr int kMaxEntries = 300;
    QString m_path;
    QList<QRHistoryEntry> m_entries; // mới nhất ở đầu
    bool m_lastSaveOk{true};
};
