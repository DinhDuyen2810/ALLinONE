#pragma once

#include <QHostAddress>
#include <QObject>
#include <QString>

class QTimer;
class QUdpSocket;

/**
 * @brief Quảng bá "tôi đang ở đây" trong mạng LAN và lắng nghe quảng bá từ máy khác qua UDP broadcast.
 * Gói tin quảng bá CHỈ chứa thông tin công khai (id ngẫu nhiên, tên máy, cổng TCP) - không có bí mật,
 * không đủ để ghép đôi (vẫn cần mã bảo mật nhập tay). Chỉ hoạt động trong mạng LAN cục bộ (broadcast
 * không đi qua router/Internet).
 */
class PeerDiscovery : public QObject
{
    Q_OBJECT

public:
    explicit PeerDiscovery(QObject* parent = nullptr);
    ~PeerDiscovery() override;

    static constexpr quint16 kDiscoveryPort = 47631;

    bool start(const QString& myId, const QString& myMachineName, quint16 myTcpPort, QString* error = nullptr);
    void stop();
    bool isRunning() const { return m_running; }

signals:
    /// hasPairingSessionOpen: máy đó có đang mở một phiên chờ ghép đôi hay không (hiển thị gợi ý trên UI).
    void peerAnnounced(QString id, QString machineName, QHostAddress address, quint16 tcpPort);

private slots:
    void onReadyRead();
    void onAnnounceTick();

private:
    QUdpSocket* m_socket{nullptr};
    QTimer* m_announceTimer{nullptr};
    QString m_myId;
    QString m_myName;
    quint16 m_myPort{0};
    bool m_running{false};
};
