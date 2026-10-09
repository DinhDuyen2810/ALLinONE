#include "QRHistoryStore.h"

#include "core/AppPaths.h"
#include "core/Logger.h"

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
    : m_path(AppPaths::profileFile("qr_history.json"))
{
    load();
}

void QRHistoryStore::persist()
{
    // Trước đây kết quả save() bị bỏ qua ở cả add/removeAt/clear: ghi thất bại (thư mục không ghi được,
    // đĩa đầy, tệp bị chương trình khác khóa) thì lịch sử lặng lẽ không được lưu, người dùng chỉ phát
    // hiện ở lần mở ứng dụng sau.
    m_lastSaveOk = save();
    if (!m_lastSaveOk)
    {
        Logger::instance().warning("QR", "Không ghi được tệp lịch sử QR: " + m_path);
        emit saveFailed(m_path);
    }
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
    persist();
    emit changed();
}

void QRHistoryStore::removeAt(int index)
{
    if (index < 0 || index >= m_entries.size())
        return;
    m_entries.removeAt(index);
    persist();
    emit changed();
}

void QRHistoryStore::clear()
{
    m_entries.clear();
    persist();
    emit changed();
}

bool QRHistoryStore::load()
{
    m_entries.clear();
    QFile f(m_path);
    if (!f.exists())
        return true; // chưa có lịch sử - không phải lỗi
    if (!f.open(QIODevice::ReadOnly))
    {
        Logger::instance().warning("QR", "Không mở được tệp lịch sử QR: " + m_path + " (" + f.errorString() + ")");
        return false;
    }

    const QByteArray raw = f.readAll();
    f.close();
    if (raw.trimmed().isEmpty())
        return true; // tệp rỗng (vd vừa được tạo) - coi như chưa có lịch sử

    const QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (!doc.isObject())
    {
        // Tệp hỏng (ghi dở từ bản cũ, bị sửa tay sai cú pháp...). Trước đây chỉ trả false với danh sách
        // rỗng, và lần add() kế tiếp GHI ĐÈ luôn lên tệp này - toàn bộ lịch sử cũ mất không dấu vết. Đổi
        // tên sang .bak để tệp mới không đè lên nó, người dùng còn tự cứu/sửa tay được.
        QString backup = m_path + ".bak";
        if (QFile::exists(backup))
            backup = m_path + "." + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".bak";
        if (QFile::rename(m_path, backup))
            Logger::instance().warning("QR", "Tệp lịch sử QR bị hỏng, đã đổi tên thành: " + backup);
        else
            Logger::instance().warning("QR", "Tệp lịch sử QR bị hỏng và không đổi tên được: " + m_path);
        return false;
    }

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
