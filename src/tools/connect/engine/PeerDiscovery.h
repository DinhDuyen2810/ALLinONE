#pragma once

#include <QHostAddress>
#include <QObject>
#include <QString>

class QTimer;
class QUdpSocket;

/**
 * @brief Quảng bá "tôi đang ở đây" trong mạng LAN và lắng nghe quảng bá từ máy khác qua UDP broadcast.
 * Gói tin quảng bá CHỈ chứa thông tin công khai (id ngẫu nhiên, tên máy, cổng TCP, phiên bản giao thức) -
 * không có bí mật, không đủ để ghép đôi (vẫn cần mã bảo mật nhập tay). Chỉ hoạt động trong mạng LAN cục
 * bộ (broadcast không đi qua router/Internet).
 *
 * Gói quảng bá KHÔNG được xác thực - bất kỳ máy nào trong LAN cũng tự khai được id/tên/địa chỉ tùy ý.
 * Nơi dùng (ConnectSessionController) vì thế chỉ coi nó là GỢI Ý "thử kết nối tới đây xem": không ghi
 * vào PeerStore, không dùng để chọn khóa; danh tính thật chỉ được xác nhận qua bắt tay phiên.
 */
class PeerDiscovery : public QObject
{
    Q_OBJECT

public:
    explicit PeerDiscovery(QObject* parent = nullptr);
    ~PeerDiscovery() override;

    static constexpr quint16 kDiscoveryPort = 47631;
    static constexpr int kMaxDatagramBytes = 1024; // gói hợp lệ chỉ ~150 byte - lớn hơn là rác, bỏ qua

    bool start(const QString& myId, const QString& myMachineName, quint16 myTcpPort, QString* error = nullptr);
    void stop();
    bool isRunning() const { return m_running; }

signals:
    /// protocolVersion: trường "v" máy kia tự khai (ConnectProtocol::kVersion của bản đó) - nơi nhận tự so
    /// để báo "khác phiên bản" thay vì cố kết nối rồi thất bại khó hiểu. id/tên đã được kiểm độ dài/ký tự.
    void peerAnnounced(QString id, QString machineName, QHostAddress address, quint16 tcpPort, int protocolVersion);

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
