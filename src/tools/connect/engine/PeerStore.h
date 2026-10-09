#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "../model/PairedPeer.h"

/// Lưu trữ danh sách máy đã ghép đôi, ở <dữ liệu người dùng>/profiles/connect_peers.json (xem
/// core/AppPaths.h; giống ActionSerializer/QRHistoryStore).
///
/// Khóa dài hạn của từng máy KHÔNG nằm thô trong tệp: nó được bọc bằng DPAPI theo tài khoản Windows hiện
/// tại (CryptoSession::protectForCurrentUser) rồi mới ghi ra ("longTermKeyProtected"). Tệp của bản cũ
/// (trường "longTermKey" dạng Base64 thô) vẫn đọc được và được ghi lại ngay sang dạng mới.
class PeerStore : public QObject
{
    Q_OBJECT

public:
    static PeerStore& instance();

    /// Kho riêng tại một đường dẫn cụ thể - dùng cho kiểm thử (hai ConnectSessionController trong cùng một
    /// tiến trình test cần hai kho tách biệt). Ứng dụng thật chỉ dùng instance().
    explicit PeerStore(const QString& path, QObject* parent = nullptr);

    QList<PairedPeer> peers() const { return m_peers; }
    PairedPeer* find(const QString& id);

    void addOrUpdate(const PairedPeer& peer);
    void remove(const QString& id);
    void clear();

    /// Ghi nhận địa chỉ/cổng lắng nghe/tên máy vừa được XÁC NHẬN qua một lần bắt tay phiên thành công. Chỉ
    /// ghi tệp khi có gì đó thật sự đổi (được gọi ở mỗi lần kết nối lại).
    void updateEndpoint(const QString& id, const QString& address, quint16 listenPort, const QString& machineName);

    /// Đường dẫn file; đổi được trước khi load() (dùng cho kiểm thử).
    void setFilePath(const QString& path) { m_path = path; }
    bool load();
    bool save() const;

signals:
    void changed();

private:
    PeerStore();

    QString m_path;
    QList<PairedPeer> m_peers;
};
