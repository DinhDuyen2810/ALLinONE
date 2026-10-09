#include "PeerStore.h"

#include "CryptoSession.h"
#include "ProtocolMessage.h"
#include "core/AppPaths.h"

#include <algorithm>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

PeerStore& PeerStore::instance()
{
    static PeerStore s;
    return s;
}

PeerStore::PeerStore()
    : m_path(AppPaths::profileFile("connect_peers.json"))
{
    load();
}

PeerStore::PeerStore(const QString& path, QObject* parent)
    : QObject(parent)
    , m_path(path)
{
    load();
}

PairedPeer* PeerStore::find(const QString& id)
{
    for (PairedPeer& p : m_peers)
        if (p.id == id)
            return &p;
    return nullptr;
}

void PeerStore::addOrUpdate(const PairedPeer& peer)
{
    if (PairedPeer* existing = find(peer.id))
        *existing = peer;
    else
        m_peers.push_back(peer);
    save();
    emit changed();
}

void PeerStore::remove(const QString& id)
{
    const int before = m_peers.size();
    m_peers.erase(std::remove_if(m_peers.begin(), m_peers.end(), [&](const PairedPeer& p) { return p.id == id; }),
                 m_peers.end());
    if (m_peers.size() != before)
    {
        save();
        emit changed();
    }
}

void PeerStore::clear()
{
    if (m_peers.isEmpty())
        return;
    m_peers.clear();
    save();
    emit changed();
}

void PeerStore::updateEndpoint(const QString& id, const QString& address, quint16 listenPort, const QString& machineName)
{
    PairedPeer* p = find(id);
    if (!p)
        return;

    bool dirty = false;
    if (!address.isEmpty() && p->lastAddress != address)
    {
        p->lastAddress = address;
        dirty = true;
    }
    if (listenPort != 0 && p->lastPort != listenPort)
    {
        p->lastPort = listenPort;
        dirty = true;
    }
    if (!machineName.isEmpty() && p->machineName != machineName)
    {
        p->machineName = machineName;
        dirty = true;
    }
    if (dirty)
    {
        save();
        emit changed();
    }
}

bool PeerStore::load()
{
    m_peers.clear();
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return false;

    bool hasLegacyRawKey = false;
    for (const QJsonValue& v : doc.object().value("peers").toArray())
    {
        const QJsonObject o = v.toObject();
        PairedPeer p;
        p.id = o.value("id").toString();
        p.machineName = ConnectProtocol::sanitizeMachineName(o.value("machineName").toString());
        p.lastAddress = o.value("lastAddress").toString();
        const int port = o.value("lastPort").toInt();
        p.lastPort = (port >= 1 && port <= 65535) ? static_cast<quint16>(port) : 0;

        if (o.contains("longTermKeyProtected"))
        {
            p.longTermKey = CryptoSession::unprotectForCurrentUser(
                QByteArray::fromBase64(o.value("longTermKeyProtected").toString().toLatin1()));
            if (p.longTermKey.size() != CryptoSession::kKeyBytes)
            {
                // Tệp được chép từ máy/tài khoản Windows khác (DPAPI không mở được), hoặc bị hỏng - khóa
                // không dùng được, phải ghép đôi lại với máy này.
                qWarning("PeerStore: không mở được khóa đã bảo vệ của máy \"%s\" - cần ghép đôi lại.",
                         qUtf8Printable(p.machineName));
                continue;
            }
        }
        else
        {
            // Tệp của bản cũ: khóa dài hạn nằm thô dạng Base64 - vẫn đọc, và ghi lại sang dạng DPAPI ngay
            // bên dưới để khóa thô không nằm lại trên đĩa tới tận lần sửa danh sách kế tiếp.
            p.longTermKey = QByteArray::fromBase64(o.value("longTermKey").toString().toLatin1());
            hasLegacyRawKey = true;
        }

        const int side = o.value("side").toInt(0);
        p.side = (side >= static_cast<int>(ScreenSide::None) && side <= static_cast<int>(ScreenSide::Bottom))
                     ? static_cast<ScreenSide>(side)
                     : ScreenSide::None;
        p.autoConnect = o.value("autoConnect").toBool(true);
        p.pairedAt = QDateTime::fromString(o.value("pairedAt").toString(), Qt::ISODate);
        if (p.isValid() && ConnectProtocol::isValidPeerId(p.id) && !find(p.id))
            m_peers.push_back(p);
    }

    if (hasLegacyRawKey)
    {
        if (save())
        {
            // Đã ghi lại bằng DPAPI - xóa luôn bản cũ ở vị trí trước đây (cạnh exe), nếu không khóa thô
            // vẫn nằm đó và việc bảo vệ bản mới vô nghĩa. Chỉ áp dụng cho đường dẫn mặc định của ứng dụng.
            if (m_path == AppPaths::profileFile("connect_peers.json"))
                AppPaths::removeLegacyProfileCopies("connect_peers.json");
        }
        else
        {
            qWarning("PeerStore: không ghi lại được %s sang dạng khóa đã bảo vệ.", qUtf8Printable(m_path));
        }
    }
    return true;
}

bool PeerStore::save() const
{
    const QFileInfo info(m_path);
    QDir().mkpath(info.absolutePath());

    QJsonArray arr;
    for (const PairedPeer& p : m_peers)
    {
        // DPAPI theo tài khoản Windows hiện tại: người/tiến trình khác đọc được tệp này (sao lưu, đồng bộ
        // đám mây, tài khoản khác trên cùng máy) vẫn không lấy được khóa. Bọc thất bại (rất hiếm - hồ sơ
        // người dùng hỏng) thì BỎ QUA máy đó thay vì ghi khóa thô xuống đĩa.
        const QByteArray protectedKey = CryptoSession::protectForCurrentUser(p.longTermKey);
        if (protectedKey.isEmpty())
        {
            qWarning("PeerStore: không bảo vệ được khóa của máy \"%s\" - không lưu máy này.", qUtf8Printable(p.machineName));
            continue;
        }

        QJsonObject o;
        o["id"] = p.id;
        o["machineName"] = p.machineName;
        o["lastAddress"] = p.lastAddress;
        o["lastPort"] = p.lastPort;
        o["longTermKeyProtected"] = QString::fromLatin1(protectedKey.toBase64());
        o["side"] = static_cast<int>(p.side);
        o["autoConnect"] = p.autoConnect;
        o["pairedAt"] = p.pairedAt.toString(Qt::ISODate);
        arr.append(o);
    }

    QJsonObject root;
    root["version"] = "2.0";
    root["peers"] = arr;

    // QSaveFile: ghi ra tệp tạm rồi ĐỔI TÊN ĐÈ nguyên tử lên đích khi commit() thành công - nếu tiến
    // trình bị crash/kill giữa chừng lúc đang ghi, danh sách thiết bị đã ghép đôi (kèm khóa dài hạn) thật
    // không bao giờ bị mất sạch (khác QFile::open(Truncate) cũ, xóa nội dung đích NGAY khi mở).
    QSaveFile f(m_path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}
