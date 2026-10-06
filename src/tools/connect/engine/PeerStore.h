#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "../model/PairedPeer.h"

/// Lưu trữ danh sách máy đã ghép đôi, ở profiles/connect_peers.json (giống ActionSerializer/QRHistoryStore).
class PeerStore : public QObject
{
    Q_OBJECT

public:
    static PeerStore& instance();

    QList<PairedPeer> peers() const { return m_peers; }
    PairedPeer* find(const QString& id);

    void addOrUpdate(const PairedPeer& peer);
    void remove(const QString& id);
    void clear();

    /// Đường dẫn file; đổi được trước khi load() (dùng cho kiểm thử).
    void setFilePath(const QString& path) { m_path = path; }
    bool load();
    bool save() const;

signals:
    void changed();

private:
    PeerStore();

    QString m_path{"profiles/connect_peers.json"};
    QList<PairedPeer> m_peers;
};
