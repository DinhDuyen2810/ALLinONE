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

/// Lịch sử tạo/quét, lưu ở profiles/qr_history.json (tối đa 300 mục).
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
    bool load();
    bool save() const;

signals:
    void changed();

private:
    QRHistoryStore();

    static constexpr int kMaxEntries = 300;
    QString m_path{"profiles/qr_history.json"};
    QList<QRHistoryEntry> m_entries; // mới nhất ở đầu
};
