#include "NetworkSession.h"

#include "CryptoSession.h"

#include <QTcpSocket>
#include <QtEndian>

namespace
{
constexpr int kLengthPrefixBytes = 4;
constexpr qint64 kMaxFrameSize = 16 * 1024 * 1024; // 16MB - đủ cho clipboard/ảnh, chặn gói tin dị dạng
}

NetworkSession::NetworkSession(QObject* parent)
    : QObject(parent)
{
}

NetworkSession::~NetworkSession() = default;

void NetworkSession::setCryptoKey(const QByteArray& key256bit)
{
    m_crypto = key256bit.size() == 32 ? std::make_unique<CryptoSession>(key256bit) : nullptr;
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
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_socket = new QTcpSocket(this);
    wireSocket();
    m_socket->connectToHost(address, port);
}

void NetworkSession::adoptSocket(QTcpSocket* socket)
{
    if (m_socket)
    {
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_socket = socket;
    m_socket->setParent(this);
    wireSocket();
}

bool NetworkSession::sendMessage(const ProtocolMessage& msg, QString* error)
{
    if (!m_socket || m_socket->state() != QAbstractSocket::ConnectedState)
    {
        if (error) *error = "Chưa kết nối.";
        return false;
    }
    if (!m_crypto || !m_crypto->isValid())
    {
        if (error) *error = "Chưa thiết lập khóa mã hóa cho phiên này.";
        return false;
    }

    QByteArray packet;
    if (!m_crypto->encrypt(msg.toBytes(), &packet, error))
        return false;

    QByteArray frame;
    frame.resize(kLengthPrefixBytes);
    qToBigEndian<quint32>(static_cast<quint32>(packet.size()), reinterpret_cast<uchar*>(frame.data()));
    frame += packet;

    return m_socket->write(frame) == frame.size();
}

void NetworkSession::processBuffer()
{
    if (!m_socket)
        return;
    m_recvBuffer += m_socket->readAll();

    while (true)
    {
        if (m_recvBuffer.size() < kLengthPrefixBytes)
            return;

        const quint32 frameLen = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(m_recvBuffer.constData()));
        if (frameLen == 0 || frameLen > kMaxFrameSize)
        {
            emit errorOccurred("Gói tin không hợp lệ (kích thước bất thường) - đóng kết nối.");
            disconnectSession();
            return;
        }
        if (m_recvBuffer.size() < kLengthPrefixBytes + static_cast<int>(frameLen))
            return; // chưa nhận đủ, chờ thêm dữ liệu

        const QByteArray packet = m_recvBuffer.mid(kLengthPrefixBytes, static_cast<int>(frameLen));
        m_recvBuffer.remove(0, kLengthPrefixBytes + static_cast<int>(frameLen));

        if (!m_crypto || !m_crypto->isValid())
        {
            emit errorOccurred("Nhận được dữ liệu trước khi thiết lập khóa mã hóa.");
            continue;
        }

        QByteArray plain;
        QString err;
        if (!m_crypto->decrypt(packet, &plain, &err))
        {
            // Sai khóa (ghép đôi nhầm/dữ liệu giả mạo) - không crash, chỉ báo lỗi và bỏ qua gói này.
            emit errorOccurred("Không giải mã được dữ liệu nhận: " + err);
            continue;
        }

        ProtocolMessage msg;
        if (ProtocolMessage::fromBytes(plain, &msg))
            emit messageReceived(msg);
    }
}

void NetworkSession::disconnectSession()
{
    if (m_socket)
        m_socket->disconnectFromHost();
}

bool NetworkSession::isConnected() const
{
    return m_socket && m_socket->state() == QAbstractSocket::ConnectedState;
}

QHostAddress NetworkSession::peerAddress() const
{
    return m_socket ? m_socket->peerAddress() : QHostAddress();
}

quint16 NetworkSession::peerPort() const
{
    return m_socket ? m_socket->peerPort() : 0;
}
