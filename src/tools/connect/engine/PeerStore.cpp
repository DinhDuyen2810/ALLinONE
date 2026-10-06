#include "PeerStore.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

PeerStore& PeerStore::instance()
{
    static PeerStore s;
    return s;
}

PeerStore::PeerStore()
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

bool PeerStore::load()
{
    m_peers.clear();
    QFile f(m_path);
    if (!f.open(QIODevice::ReadOnly))
        return false;

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return false;

    for (const QJsonValue& v : doc.object().value("peers").toArray())
    {
        const QJsonObject o = v.toObject();
        PairedPeer p;
        p.id = o.value("id").toString();
        p.machineName = o.value("machineName").toString();
        p.lastAddress = o.value("lastAddress").toString();
        p.lastPort = static_cast<quint16>(o.value("lastPort").toInt());
        p.longTermKey = QByteArray::fromBase64(o.value("longTermKey").toString().toLatin1());
        p.side = static_cast<ScreenSide>(o.value("side").toInt(0));
        p.autoConnect = o.value("autoConnect").toBool(true);
        p.pairedAt = QDateTime::fromString(o.value("pairedAt").toString(), Qt::ISODate);
        if (p.isValid())
            m_peers.push_back(p);
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
        QJsonObject o;
        o["id"] = p.id;
        o["machineName"] = p.machineName;
        o["lastAddress"] = p.lastAddress;
        o["lastPort"] = p.lastPort;
        o["longTermKey"] = QString::fromLatin1(p.longTermKey.toBase64());
        o["side"] = static_cast<int>(p.side);
        o["autoConnect"] = p.autoConnect;
        o["pairedAt"] = p.pairedAt.toString(Qt::ISODate);
        arr.append(o);
    }

    QJsonObject root;
    root["version"] = "1.0";
    root["peers"] = arr;

    QFile f(m_path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}
