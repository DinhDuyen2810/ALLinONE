#include "QRHistoryStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

QRHistoryStore& QRHistoryStore::instance()
{
    static QRHistoryStore s;
    return s;
}

QRHistoryStore::QRHistoryStore()
{
    load();
}

void QRHistoryStore::add(const QString& source, const QString& typeName, const QString& content)
{
    if (content.isEmpty())
        return;

    // Không ghi trùng liền kề (cùng nguồn + nội dung)
    if (!m_entries.isEmpty() && m_entries.first().source == source && m_entries.first().content == content)
    {
        m_entries.first().time = QDateTime::currentDateTime();
    }
    else
    {
        m_entries.prepend({QDateTime::currentDateTime(), source, typeName, content});
        while (m_entries.size() > kMaxEntries)
            m_entries.removeLast();
    }
    save();
    emit changed();
}

void QRHistoryStore::removeAt(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    m_entries.removeAt(index);
    save();
    emit changed();
}

void QRHistoryStore::clear()
{
    m_entries.clear();
    save();
    emit changed();
}

bool QRHistoryStore::load()
{
    m_entries.clear();
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false;

    for (const QJsonValue& v : doc.object().value("entries").toArray())
    {
        const QJsonObject o = v.toObject();
        QRHistoryEntry e;
        e.time = QDateTime::fromString(o.value("time").toString(), Qt::ISODate);
        e.source = o.value("source").toString();
        e.typeName = o.value("type").toString();
        e.content = o.value("content").toString();
        if (!e.content.isEmpty())
            m_entries.push_back(e);
    }
    return true;
}

bool QRHistoryStore::save() const
{
    const QFileInfo info(m_path);
    QDir().mkpath(info.absolutePath());

    QJsonArray arr;
    for (const QRHistoryEntry& e : m_entries)
    {
        QJsonObject o;
        o["time"] = e.time.toString(Qt::ISODate);
        o["source"] = e.source;
        o["type"] = e.typeName;
        o["content"] = e.content;
        arr.append(o);
    }
    QJsonObject root;
    root["version"] = "1.0";
    root["entries"] = arr;

    // QSaveFile: ghi ra tệp tạm rồi ĐỔI TÊN ĐÈ nguyên tử lên đích khi commit() thành công - nếu tiến
    // trình bị crash/kill giữa chừng lúc đang ghi (QFile::open(Truncate) cũ sẽ xóa sạch nội dung đích
    // NGAY khi mở, trước khi ghi lại), tệp đích THẬT (profiles/qr_history.json) không bao giờ bị để lại
    // ở trạng thái rỗng/dở dang - vẫn giữ nguyên bản cũ cho tới khi bản mới ghi xong hoàn toàn.
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}
