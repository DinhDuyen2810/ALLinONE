#include "DownloadHistoryStore.h"

#include "core/AppPaths.h"
#include "core/Logger.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

DownloadHistoryStore::DownloadHistoryStore(QObject* parent)
    : QObject(parent)
    , m_path(AppPaths::profileFile("downloader_history.json"))
{
    load();
}

DownloadHistoryStore::DownloadHistoryStore(const QString& path, QObject* parent)
    : QObject(parent)
    , m_path(path)
{
    load();
}

void DownloadHistoryStore::load()
{
    m_entries.clear();
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return; // chưa có tệp = lịch sử rỗng
    const QByteArray raw = f.readAll();
    f.close();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray())
    {
        if (!raw.trimmed().isEmpty())
        {
            // Tệp có nội dung nhưng không phải JSON hợp lệ: giữ lại bản hỏng (đổi tên .bak) để còn cứu tay được.
            QFile::remove(m_path + ".bak");
            QFile::rename(m_path, m_path + ".bak");
            Logger::instance().warning("Downloader", "Lịch sử tải bị hỏng - đã đổi tên thành " + m_path + ".bak");
        }
        return;
    }
    for (const QJsonValue& v : doc.array())
    {
        if (!v.isObject())
            continue;
        const QJsonObject o = v.toObject();
        DownloadHistoryEntry e;
        e.time = QDateTime::fromString(o.value("time").toString(), Qt::ISODate);
        e.title = o.value("title").toString();
        e.fileName = o.value("fileName").toString();
        e.path = o.value("path").toString();
        e.folder = o.value("folder").toString();
        e.sourceUrl = o.value("sourceUrl").toString();
        e.mediaUrl = o.value("mediaUrl").toString();
        e.status = o.value("status").toString();
        e.error = o.value("error").toString();
        e.size = static_cast<qint64>(o.value("size").toDouble(-1));
        e.kind = o.value("kind").toString();
        m_entries << e;
        if (m_entries.size() >= kMaxEntries)
            break;
    }
}

bool DownloadHistoryStore::save()
{
    QJsonArray arr;
    for (const DownloadHistoryEntry& e : m_entries)
    {
        QJsonObject o;
        o["time"] = e.time.toString(Qt::ISODate);
        o["title"] = e.title;
        o["fileName"] = e.fileName;
        o["path"] = e.path;
        o["folder"] = e.folder;
        o["sourceUrl"] = e.sourceUrl;
        o["mediaUrl"] = e.mediaUrl;
        o["status"] = e.status;
        o["error"] = e.error;
        o["size"] = static_cast<double>(e.size);
        o["kind"] = e.kind;
        arr << o;
    }
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
    {
        Logger::instance().warning("Downloader", "Không lưu được lịch sử tải: " + m_path);
        return false;
    }
    f.write(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    return f.commit();
}

void DownloadHistoryStore::add(const DownloadHistoryEntry& entry)
{
    DownloadHistoryEntry e = entry;
    if (!e.time.isValid())
        e.time = QDateTime::currentDateTime();
    m_entries.prepend(e);
    while (m_entries.size() > kMaxEntries)
        m_entries.removeLast();
    save();
    emit changed();
}

void DownloadHistoryStore::removeAt(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    m_entries.removeAt(index);
    save();
    emit changed();
}

void DownloadHistoryStore::clear()
{
    if (m_entries.isEmpty())
        return;
    m_entries.clear();
    save();
    emit changed();
}
