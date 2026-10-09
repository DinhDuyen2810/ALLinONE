#pragma once

#include <QHostAddress>
#include <QObject>
#include <memory>

#include "ProtocolMessage.h"

class CryptoSession;
class QTcpSocket;

/**
 * @brief Một kết nối TCP đã (hoặc sắp) mã hóa tới một máy khác. Đóng khung: 4 byte độ dài (big-endian)
 * + nội dung. Không tự mở cổng nghe - ConnectSessionController dùng QTcpServer và gọi adoptSocket() cho
 * mỗi kết nối đến.
 *
 * Ba giai đoạn nối tiếp trên cùng một kết nối (xem ConnectProtocol trong ProtocolMessage.h và sơ đồ bắt
 * tay trong ConnectSessionController.h):
 *  1. Preamble KHÔNG mã hóa: đúng MỘT khung đầu tiên, bên mở kết nối gửi (sendPreamble), bên nhận kết
 *     nối nhận qua preambleReceived() rồi mới chọn khóa.
 *  2. Bắt tay (setHandshakeKey): mỗi gói mang nonce ngẫu nhiên, dữ liệu kèm theo (AAD) chứa CHIỀU gửi
 *     ("I" = bên mở kết nối, "R" = bên nhận) nên gói của chính mình bị dội ngược lại không giải mã được.
 *  3. Phiên (setSessionKeys): mỗi chiều một khóa riêng + bộ đếm tuần tự làm nonce (không nằm trong gói -
 *     TCP đã bảo đảm thứ tự). Gói bị phát lại, bỏ sót hoặc đảo thứ tự làm lệch bộ đếm -> giải mã thất bại.
 *
 * Mọi lỗi giao thức (khung sai kích thước, giải mã thất bại, thông điệp sai định dạng) đều ĐÓNG kết nối
 * ngay - không "bỏ qua gói này rồi đọc tiếp" như bản 1 (ở chế độ bộ đếm, bỏ qua một gói là lệch vĩnh viễn).
 */
class NetworkSession : public QObject
{
    Q_OBJECT

public:
    enum class Side
    {
        Unknown,
        Initiator, // ta mở kết nối (connectToHost)
        Responder  // ta nhận kết nối (adoptSocket)
    };

    static constexpr int kMaxPreambleBytes = 256;
    /// Trần khung khi CHƯA có khóa phiên (chưa xác thực): thông điệp bắt tay/ghép đôi rất nhỏ - không để
    /// một kết nối lạ bắt ta đệm hàng MB chỉ bằng cách khai độ dài lớn trong 4 byte đầu.
    static constexpr int kUnauthenticatedMaxFrameBytes = 4 * 1024;
    /// Trần khung sau khi đã có khóa phiên: đủ cho văn bản clipboard (ConnectProtocol::kMaxClipboardBytes)
    /// cộng phần đóng gói.
    static constexpr int kAuthenticatedMaxFrameBytes = 2 * 1024 * 1024;
    /// Bên kia ngừng đọc (treo/mất mạng chưa phát hiện) mà ta vẫn gửi sự kiện chuột liên tục thì hàng đợi
    /// ghi phình vô hạn - vượt ngưỡng này coi như phiên đã chết, đóng luôn.
    static constexpr qint64 kMaxPendingWriteBytes = 8 * 1024 * 1024;

    explicit NetworkSession(QObject* parent = nullptr);
    ~NetworkSession() override;

    void connectToHost(const QHostAddress& address, quint16 port);
    /// Nhận socket đã accept() từ QTcpServer. expectPreamble=false chỉ dành cho kiểm thử đóng khung/mã
    /// hóa thuần (bỏ qua giai đoạn 1).
    void adoptSocket(QTcpSocket* socket, bool expectPreamble = true);

    Side side() const { return m_side; }

    /// Gửi khung preamble không mã hóa. Chỉ gọi một lần, ngay sau connected(), trước mọi sendMessage().
    bool sendPreamble(const QByteArray& preamble, QString* error = nullptr);

    /// Giai đoạn bắt tay. 'context' (thường là chính preamble) được trộn vào AAD để các gói bắt tay gắn
    /// chặt với đúng kết nối này (đúng phiên bản/mục đích/id đã khai).
    void setHandshakeKey(const QByteArray& key256bit, const QByteArray& context = QByteArray());

    /// Giai đoạn phiên: khóa gửi và khóa nhận KHÁC nhau (xem CryptoSession::deriveSessionKeys), bộ đếm
    /// hai chiều bắt đầu lại từ 0. Từ đây trần khung nhận nâng lên kAuthenticatedMaxFrameBytes.
    void setSessionKeys(const QByteArray& sendKey, const QByteArray& receiveKey);
    bool hasSessionKeys() const { return m_txSession && m_rxSession; }

    /// false (kèm *error) nếu chưa kết nối, chưa có khóa, hoặc thông điệp vượt trần khung của giai đoạn
    /// hiện tại - trường hợp cuối KHÔNG đóng kết nối và không tiêu tốn bộ đếm (nơi gọi tự bỏ qua).
    bool sendMessage(const ProtocolMessage& msg, QString* error = nullptr);

    void disconnectSession(); // đóng êm: gửi nốt dữ liệu đang chờ rồi mới đóng
    void abortSession();      // đóng ngay, bỏ dữ liệu đang chờ

    bool isConnected() const;
    QHostAddress peerAddress() const;
    quint16 peerPort() const;

signals:
    void connected();
    void disconnected();
    void preambleReceived(QByteArray preamble);
    void messageReceived(ProtocolMessage msg);
    /// Một gói không giải mã/xác thực được (sai khóa, bị sửa, phát lại, phản xạ). Phát ra TRƯỚC
    /// errorOccurred() tương ứng; kết nối bị đóng ngay sau đó.
    void decryptFailed();
    void errorOccurred(QString message);

private:
    void wireSocket();
    void processBuffer();
    void failAndClose(const QString& message);
    QByteArray handshakeAad(Side sender) const;

    QTcpSocket* m_socket{nullptr};
    Side m_side{Side::Unknown};
    bool m_expectPreamble{false};
    bool m_closed{false};

    std::unique_ptr<CryptoSession> m_handshake; // giai đoạn 2 (dùng chung cho gửi và nhận, phân biệt bằng AAD)
    QByteArray m_handshakeContext;
    std::unique_ptr<CryptoSession> m_txSession; // giai đoạn 3
    std::unique_ptr<CryptoSession> m_rxSession;
    quint64 m_txCounter{0};
    quint64 m_rxCounter{0};

    QByteArray m_recvBuffer;
};
