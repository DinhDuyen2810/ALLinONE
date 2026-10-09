#include "NetworkSession.h"

#include "CryptoSession.h"

#include <QTcpSocket>
#include <QtEndian>

namespace
{
constexpr int kLengthPrefixBytes = 4;

/// Nonce GCM 12 byte của giai đoạn phiên: 4 byte 0 + bộ đếm 64-bit big-endian. Không bao giờ lặp lại
/// dưới cùng một khóa vì mỗi kết nối, mỗi chiều có một khóa phiên riêng và bộ đếm chỉ tăng.
QByteArray counterNonce(quint64 counter)
{
    QByteArray nonce(CryptoSession::kNonceBytes, '\0');
    qToBigEndian<quint64>(counter, reinterpret_cast<uchar*>(nonce.data()) + 4);
    return nonce;
}
} // namespace

NetworkSession::NetworkSession(QObject* parent)
    : QObject(parent)
{
}

NetworkSession::~NetworkSession() = default;

void NetworkSession::setHandshakeKey(const QByteArray& key256bit, const QByteArray& context)
{
    m_handshake = key256bit.size() == CryptoSession::kKeyBytes ? std::make_unique<CryptoSession>(key256bit) : nullptr;
    m_handshakeContext = context;
}

void NetworkSession::setSessionKeys(const QByteArray& sendKey, const QByteArray& receiveKey)
{
    if (sendKey.size() != CryptoSession::kKeyBytes || receiveKey.size() != CryptoSession::kKeyBytes)
    {
        m_txSession.reset();
        m_rxSession.reset();
        return;
    }
    m_txSession = std::make_unique<CryptoSession>(sendKey);
    m_rxSession = std::make_unique<CryptoSession>(receiveKey);
    m_txCounter = 0;
    m_rxCounter = 0;
    // Khóa bắt tay (khóa dài hạn/khóa mã ghép đôi) không còn cần cho kết nối này nữa - bỏ ngay để mọi
    // gói về sau BẮT BUỘC đi qua khóa phiên + bộ đếm.
    m_handshake.reset();
}

QByteArray NetworkSession::handshakeAad(Side sender) const
{
    return QByteArrayLiteral("OneForAll.ConnectTogether.v2.handshake/") + (sender == Side::Initiator ? "I/" : "R/") +
           m_handshakeContext;
}

void NetworkSession::wireSocket()
{
    connect(m_socket, &QTcpSocket::connected, this, &NetworkSession::connected);
    connect(m_socket, &QTcpSocket::disconnected, this, &NetworkSession::disconnected);
    connect(m_socket, &QTcpSocket::readyRead, this, &NetworkSession::processBuffer);
    connect(m_socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        if (m_socket)
            emit errorOccurred(m_socket->errorString());
    });
}

void NetworkSession::connectToHost(const QHostAddress& address, quint16 port)
{
    if (m_socket)
    {
        m_socket->disconnect(this);
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_side = Side::Initiator;
    m_expectPreamble = false; // preamble chỉ đi một chiều: bên mở kết nối gửi, bên nhận đọc
    m_closed = false;
    m_recvBuffer.clear();
    m_socket = new QTcpSocket(this);
    wireSocket();
    m_socket->connectToHost(address, port);
}

void NetworkSession::adoptSocket(QTcpSocket* socket, bool expectPreamble)
{
    if (m_socket)
    {
        m_socket->disconnect(this);
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_side = Side::Responder;
    m_expectPreamble = expectPreamble;
    m_closed = false;
    m_recvBuffer.clear();
    m_socket = socket;
    m_socket->setParent(this);
    wireSocket();
    // Bên kia có thể đã gửi dữ liệu trước khi ta kịp nối readyRead (socket nằm chờ trong hàng đợi của
    // QTcpServer) - xử lý phần đã đến ở vòng lặp sự kiện kế tiếp, sau khi nơi gọi nối xong tín hiệu.
    if (m_socket->bytesAvailable() > 0)
        QMetaObject::invokeMethod(this, [this] { processBuffer(); }, Qt::QueuedConnection);
}

bool NetworkSession::sendPreamble(const QByteArray& preamble, QString* error)
{
    if (m_closed || !m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
    {
        if (error) *error = "Chưa kết nối.";
        return false;
    }
    if (preamble.isEmpty() || preamble.size() > kMaxPreambleBytes)
    {
        if (error) *error = "Preamble không hợp lệ.";
        return false;
    }

    QByteArray frame(kLengthPrefixBytes, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(preamble.size()), reinterpret_cast<uchar*>(frame.data()));
    frame += preamble;
    return m_socket->write(frame) == frame.size();
}

bool NetworkSession::sendMessage(const ProtocolMessage& msg, QString* error)
{
    if (m_closed || !m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
    {
        if (error) *error = "Chưa kết nối.";
        return false;
    }

    const bool sessionMode = m_txSession && m_txSession->isValid();
    if (!sessionMode && !(m_handshake && m_handshake->isValid()))
    {
        if (error) *error = "Chưa thiết lập khóa mã hóa cho phiên này.";
        return false;
    }

    // Kiểm kích thước TRƯỚC khi mã hóa: bên nhận sẽ đóng kết nối nếu khung vượt trần của nó, nên thà từ
    // chối gửi ở đây (không đụng tới bộ đếm, phiên vẫn dùng tiếp được) còn hơn làm rớt cả phiên.
    const QByteArray plain = msg.toBytes();
    const qint64 packetSize = plain.size() + CryptoSession::kTagBytes + (sessionMode ? 0 : CryptoSession::kNonceBytes);
    const qint64 limit = sessionMode ? kAuthenticatedMaxFrameBytes : kUnauthenticatedMaxFrameBytes;
    if (packetSize > limit)
    {
        if (error) *error = QString("Thông điệp quá lớn (%1 byte, tối đa %2).").arg(packetSize).arg(limit);
        return false;
    }

    if (m_socket->bytesToWrite() > kMaxPendingWriteBytes)
    {
        if (error) *error = "Bên kia không nhận dữ liệu - đóng kết nối.";
        failAndClose("Hàng đợi gửi quá đầy (bên kia không nhận dữ liệu) - đóng kết nối.");
        return false;
    }

    QByteArray packet;
    if (sessionMode)
    {
        if (!m_txSession->encryptWithNonce(counterNonce(m_txCounter), QByteArray(), plain, &packet, error))
            return false;
        ++m_txCounter;
    }
    else if (!m_handshake->encrypt(plain, &packet, error, handshakeAad(m_side)))
    {
        return false;
    }

    QByteArray frame(kLengthPrefixBytes, '\0');
    qToBigEndian<quint32>(static_cast<quint32>(packet.size()), reinterpret_cast<uchar*>(frame.data()));
    frame += packet;

    if (m_socket->write(frame) != frame.size())
    {
        // Ở chế độ phiên, bộ đếm đã tăng mà gói không đi được -> hai đầu lệch nhau vĩnh viễn; đóng luôn.
        if (error) *error = "Ghi dữ liệu ra socket thất bại.";
        failAndClose("Ghi dữ liệu ra socket thất bại - đóng kết nối.");
        return false;
    }
    return true;
}

void NetworkSession::failAndClose(const QString& message)
{
    if (m_closed)
        return;
    m_closed = true;
    emit errorOccurred(message);
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
}

void NetworkSession::processBuffer()
{
    if (!m_socket || m_closed)
        return;
    m_recvBuffer += m_socket->readAll();

    // Kiểm m_closed ở MỖI vòng: nơi nhận tín hiệu (ConnectSessionController) có thể đóng kết nối ngay
    // trong lúc xử lý một thông điệp - các khung còn lại trong bộ đệm khi đó phải bị bỏ, không xử lý tiếp.
    while (!m_closed)
    {
        if (m_recvBuffer.size() < kLengthPrefixBytes)
            return;

        const bool sessionMode = m_rxSession && m_rxSession->isValid();
        const quint32 limit = m_expectPreamble ? kMaxPreambleBytes
                                               : (sessionMode ? kAuthenticatedMaxFrameBytes : kUnauthenticatedMaxFrameBytes);
        const quint32 frameLen = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(m_recvBuffer.constData()));
        if (frameLen == 0 || frameLen > limit)
        {
            failAndClose("Gói tin không hợp lệ (kích thước bất thường) - đóng kết nối.");
            return;
        }
        if (m_recvBuffer.size() < kLengthPrefixBytes + static_cast<int>(frameLen))
            return; // chưa nhận đủ, chờ thêm dữ liệu

        const QByteArray packet = m_recvBuffer.mid(kLengthPrefixBytes, static_cast<int>(frameLen));
        m_recvBuffer.remove(0, kLengthPrefixBytes + static_cast<int>(frameLen));

        if (m_expectPreamble)
        {
            m_expectPreamble = false;
            emit preambleReceived(packet);
            continue;
        }

        QByteArray plain;
        QString err;
        bool ok = false;
        if (sessionMode)
        {
            ok = m_rxSession->decryptWithNonce(counterNonce(m_rxCounter), QByteArray(), packet, &plain, &err);
            if (ok)
                ++m_rxCounter;
        }
        else if (m_handshake && m_handshake->isValid())
        {
            // AAD của chiều NGƯỢC LẠI: gói bên mở kết nối gửi mang nhãn "I", bên nhận kết nối gửi mang nhãn
            // "R" - gói của chính ta bị dội ngược về sẽ mang nhãn của ta và không qua được bước này.
            const Side sender = m_side == Side::Initiator ? Side::Responder : Side::Initiator;
            ok = m_handshake->decrypt(packet, &plain, &err, handshakeAad(sender));
        }
        else
        {
            failAndClose("Nhận được dữ liệu trước khi thiết lập khóa mã hóa - đóng kết nối.");
            return;
        }

        if (!ok)
        {
            // Sai khóa (nhập nhầm mã ghép đôi), dữ liệu giả mạo, gói phát lại/phản xạ - không crash; báo
            // cho nơi dùng (để đếm số lần thử mã sai) rồi đóng kết nối.
            emit decryptFailed();
            failAndClose("Không giải mã được dữ liệu nhận: " + err);
            return;
        }

        ProtocolMessage msg;
        if (!ProtocolMessage::fromBytes(plain, &msg))
        {
            failAndClose("Thông điệp nhận được sai định dạng - đóng kết nối.");
            return;
        }
        emit messageReceived(msg);
    }
}

void NetworkSession::disconnectSession()
{
    m_closed = true; // không xử lý thêm dữ liệu đến; dữ liệu đang chờ GỬI vẫn được đẩy đi trước khi đóng
    if (!m_socket)
        return;
    // flush(): đẩy ngay phần đang nằm trong bộ đệm của Qt xuống hệ điều hành (thông điệp cuối thường chỉ
    // vài chục byte nên đi hết trong một lần). Nơi gọi thường hẹn hủy đối tượng ngay sau đó - nếu chỉ dựa
    // vào disconnectFromHost() (ghi ở vòng lặp sự kiện kế tiếp) thì câu cuối có thể không kịp đi.
    if (m_socket->state() == QAbstractSocket::ConnectedState)
        m_socket->flush();
    m_socket->disconnectFromHost();
}

void NetworkSession::abortSession()
{
    m_closed = true;
    if (m_socket && m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->abort();
}

bool NetworkSession::isConnected() const
{
    return !m_closed && m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

QHostAddress NetworkSession::peerAddress() const
{
    return m_socket ? m_socket->peerAddress() : QHostAddress();
}

quint16 NetworkSession::peerPort() const
{
    return m_socket ? m_socket->peerPort() : 0;
}
