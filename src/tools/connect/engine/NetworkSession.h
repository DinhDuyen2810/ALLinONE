#pragma once

#include <QHostAddress>
#include <QObject>
#include <memory>

#include "ProtocolMessage.h"

class CryptoSession;
class QTcpSocket;

/**
 * @brief Một kết nối TCP đã (hoặc sắp) mã hóa tới một máy khác. Đóng khung: 4 byte độ dài (big-endian)
 * + gói tin mã hóa (CryptoSession). Không tự mở cổng nghe - ConnectSessionController dùng QTcpServer
 * và gọi adoptSocket() cho mỗi kết nối đến.
 */
class NetworkSession : public QObject
{
    Q_OBJECT

public:
    explicit NetworkSession(QObject* parent = nullptr);
    ~NetworkSession() override;

    /// Phải gọi trước khi gửi/nhận bất kỳ thông điệp nào. key rỗng -> tạm thời không mã hóa
    /// (chỉ dùng cho bước PairRequest/PairAccept đầu tiên, nơi khóa suy từ mã ghép đôi được set riêng).
    void setCryptoKey(const QByteArray& key256bit);

    void connectToHost(const QHostAddress& address, quint16 port);
    void adoptSocket(QTcpSocket* socket); // nhận socket đã accept() từ QTcpServer

    bool sendMessage(const ProtocolMessage& msg, QString* error = nullptr);
    void disconnectSession();

    bool isConnected() const;
    QHostAddress peerAddress() const;
    quint16 peerPort() const;

signals:
    void connected();
    void disconnected();
    void messageReceived(ProtocolMessage msg);
    void errorOccurred(QString message);

private:
    void wireSocket();
    void processBuffer();

    QTcpSocket* m_socket{nullptr};
    std::unique_ptr<CryptoSession> m_crypto;
    QByteArray m_recvBuffer;
};
