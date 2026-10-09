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
#include <QThread>

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

QRHistoryStore::QRHistoryStore(const QString& path, QObject* parent)
    : QObject(parent)
    , m_path(path)
{
    load();
}

namespace
{
/// Cùng MỘT mục lịch sử. Thời điểm so tới GIÂY - đúng độ chính xác được lưu ra tệp (Qt::ISODate): mục còn
/// trong bộ nhớ có mili-giây, chính mục đó nạp lại từ tệp thì không.
bool sameEntry(const QRHistoryEntry& a, const QRHistoryEntry& b)
{
    if (a.content != b.content || a.source != b.source || a.typeName != b.typeName)
        return false;
    if (a.time.isValid() != b.time.isValid())
        return false;
    return !a.time.isValid() || a.time.toSecsSinceEpoch() == b.time.toSecsSinceEpoch();
}

int indexOfEntry(const QList<QRHistoryEntry>& list, const QRHistoryEntry& entry)
{
    for (int i = 0; i < list.size(); ++i)
        if (sameEntry(list[i], entry))
            return i;
    return -1;
}

bool sameList(const QList<QRHistoryEntry>& a, const QList<QRHistoryEntry>& b)
{
    if (a.size() != b.size())
        return false;
    for (int i = 0; i < a.size(); ++i)
        if (!sameEntry(a[i], b[i]))
            return false;
    return true;
}
} // namespace

void QRHistoryStore::rememberDiskState()
{
    const QFileInfo info(m_path);
    m_diskKnown = true;
    m_diskExists = info.exists();
    m_diskModified = m_diskExists ? info.lastModified() : QDateTime();
    m_diskSize = m_diskExists ? info.size() : -1;
    m_synced = m_entries;
}

void QRHistoryStore::syncWithDisk()
{
    if (!m_diskKnown)
        return; // đường dẫn vừa đổi, chưa load(): danh sách đang có không thuộc tệp này (xem setFilePath)

    const QFileInfo info(m_path);
    // Tệp không còn (người dùng xóa tay...): không có gì để gộp - lần ghi tới dựng lại từ danh sách đang có.
    if (!info.exists())
        return;
    if (m_diskExists && info.size() == m_diskSize && info.lastModified() == m_diskModified)
        return; // chưa ai ghi từ lần đọc/ghi gần nhất của bản này

    const QList<QRHistoryEntry> mine = m_entries;
    const QList<QRHistoryEntry> base = m_synced;
    if (!load())
    {
        // Tệp hỏng (load() đã đổi tên nó sang .bak) hoặc không đọc được: giữ danh sách đang có.
        m_entries = mine;
        return;
    }

    // m_entries lúc này là danh sách TRÊN ĐĨA - đã gồm mọi thứ bản kia thêm/xóa, và cả mọi thay đổi của bản
    // này đã ghi được (bản kia cũng gộp trước khi ghi). Chỉ còn phải áp lại những thay đổi của CHÍNH bản này
    // chưa lên đĩa (lần ghi trước thất bại); bình thường không có gì.
    if (sameList(mine, base))
        return;
    for (const QRHistoryEntry& e : base)
    {
        if (indexOfEntry(mine, e) >= 0)
            continue;
        const int at = indexOfEntry(m_entries, e); // bản này đã xóa mục đó
        if (at >= 0)
            m_entries.removeAt(at);
    }
    for (int i = mine.size() - 1; i >= 0; --i) // từ cũ tới mới, để mục mới nhất vẫn nằm trên cùng
    {
        const QRHistoryEntry& e = mine[i];
        if (indexOfEntry(base, e) >= 0 || indexOfEntry(m_entries, e) >= 0)
            continue;
        int at = 0; // chèn theo thời điểm, không xáo thứ tự các mục đang có trên đĩa
        while (at < m_entries.size() && m_entries[at].time > e.time)
            ++at;
        // Vẫn không ghi trùng liền kề (cùng nguồn + nội dung), như add().
        const bool dupBefore = at > 0 && m_entries[at - 1].source == e.source && m_entries[at - 1].content == e.content;
        const bool dupAfter = at < m_entries.size() && m_entries[at].source == e.source && m_entries[at].content == e.content;
        if (!dupBefore && !dupAfter)
            m_entries.insert(at, e);
    }
    while (m_entries.size() > kMaxEntries)
        m_entries.removeLast();
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

    // Một bản ứng dụng khác có thể vừa ghi tệp: nạp lại + gộp trước, nếu không lần ghi dưới đây xóa mất các
    // mục của bản đó.
    syncWithDisk();

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
    // 'index' là vị trí trong danh sách người dùng ĐANG THẤY. Sau khi gộp với tệp (bản kia có thể đã thêm
    // mục lên đầu, hoặc đã xóa chính mục này) vị trí có thể lệch - tìm lại đúng mục đó, không xóa nhầm mục khác.
    const QRHistoryEntry target = m_entries.at(index);
    syncWithDisk();
    if (index >= m_entries.size() || !sameEntry(m_entries.at(index), target))
        index = indexOfEntry(m_entries, target);
    if (index >= 0)
        m_entries.removeAt(index);
    persist();
    emit changed();
}

void QRHistoryStore::clear()
{
    // Không gộp: "xóa toàn bộ" là xóa cả những mục bản kia vừa thêm.
    m_entries.clear();
    persist();
    emit changed();
}

bool QRHistoryStore::load()
{
    const bool ok = readFile();
    rememberDiskState();
    return ok;
}

bool QRHistoryStore::readFile()
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
        {
            // Tên theo thời điểm chỉ chính xác tới GIÂY: hai lần gặp tệp hỏng trong cùng một giây cho ra cùng
            // một tên, QFile::rename() thất bại (đích đã có) và tệp hỏng nằm lại để lần add() kế tiếp đè mất.
            // Thêm số thứ tự cho tới khi có tên chưa dùng.
            const QString stamped = m_path + "." + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
            backup = stamped + ".bak";
            for (int n = 2; QFile::exists(backup) && n < 1000; ++n)
                backup = QString("%1-%2.bak").arg(stamped).arg(n);
        }
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
        // Giới hạn 300 mục trước đây chỉ được áp ở add(): một tệp nhiều mục hơn (sửa tay, bản khác ghi) nạp
        // vào nguyên cả nghìn mục, bảng lịch sử dựng hết và mỗi lần ghi lại chép đủ chừng đó. Giữ các mục
        // MỚI NHẤT (đứng đầu tệp), như add() vẫn làm.
        if (m_entries.size() >= kMaxEntries)
            break;
    }
    return true;
}

bool QRHistoryStore::save()
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
    //
    // Thử lại vài lần: bước đổi tên đè của commit() thất bại THOÁNG QUA khi một tiến trình khác đang mở
    // tệp đích đúng lúc đó (trình quét virus, trình lập chỉ mục, bộ theo dõi tệp của trình soạn thảo...).
    // Đã gặp thật khi stress test đặt tệp trong một thư mục đang bị theo dõi: khoảng 20/20.000 lần ghi
    // thất bại rồi lần ghi kế tiếp lại thành công - không thử lại thì mỗi lần như vậy là một lần báo "không
    // lưu được lịch sử" oan cho người dùng.
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    bool committed = false;
    for (int attempt = 0; attempt < 5 && !committed; ++attempt)
    {
        if (attempt > 0)
            QThread::msleep(15 * attempt);
        QSaveFile f(m_path);
        if (!f.open(QIODevice::WriteOnly))
            continue;
        if (f.write(payload) != payload.size())
        {
            f.cancelWriting();
            continue;
        }
        committed = f.commit();
    }
    if (!committed)
        return false;
    // Tệp giờ đúng bằng danh sách đang có - mốc để lần ghi sau biết có bản ứng dụng nào khác ghi chen vào không.
    rememberDiskState();
    return true;
}
