#include "PeerDiscovery.h"

#include "ProtocolMessage.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkDatagram>
#include <QTimer>
#include <QUdpSocket>

namespace
{
const QString kAppTag = "OneForAll.ConnectTogether";
constexpr int kAnnounceIntervalMs = 2000;
}

PeerDiscovery::PeerDiscovery(QObject* parent)
    : QObject(parent)
{
}

PeerDiscovery::~PeerDiscovery()
{
    stop();
}

bool PeerDiscovery::start(const QString& myId, const QString& myMachineName, quint16 myTcpPort, QString* error)
{
    if (m_running)
        return true;

    m_socket = new QUdpSocket(this);
    if (!m_socket->bind(QHostAddress::AnyIPv4, kDiscoveryPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint))
    {
        if (error)
            *error = QString("Không mở được cổng khám phá mạng LAN (UDP %1): %2")
                         .arg(kDiscoveryPort).arg(m_socket->errorString());
        m_socket->deleteLater();
        m_socket = nullptr;
        return false;
    }
    connect(m_socket, &QUdpSocket::readyRead, this, &PeerDiscovery::onReadyRead);

    m_myId = myId;
    m_myName = myMachineName;
    m_myPort = myTcpPort;

    m_announceTimer = new QTimer(this);
    m_announceTimer->setInterval(kAnnounceIntervalMs);
    connect(m_announceTimer, &QTimer::timeout, this, &PeerDiscovery::onAnnounceTick);
    m_announceTimer->start();
    onAnnounceTick(); // gửi ngay một lần, không đợi tick đầu tiên

    m_running = true;
    return true;
}

void PeerDiscovery::stop()
{
    if (m_announceTimer)
    {
        m_announceTimer->stop();
        m_announceTimer->deleteLater();
        m_announceTimer = nullptr;
    }
    if (m_socket)
    {
        m_socket->close();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
    m_running = false;
}

void PeerDiscovery::onAnnounceTick()
{
    if (!m_socket)
        return;

    QJsonObject o;
    o["app"] = kAppTag;
    // Phiên bản GIAO THỨC (không phải phiên bản ứng dụng): hai máy khác "v" không bắt tay được với nhau -
    // máy nhận thấy "v" khác thì báo rõ "cần cập nhật" thay vì cố kết nối.
    o["v"] = ConnectProtocol::kVersion;
    o["id"] = m_myId;
    o["name"] = m_myName;
    o["port"] = m_myPort;
    const QByteArray data = QJsonDocument(o).toJson(QJsonDocument::Compact);
    m_socket->writeDatagram(data, QHostAddress::Broadcast, kDiscoveryPort);
}

void PeerDiscovery::onReadyRead()
{
    while (m_socket && m_socket->hasPendingDatagrams())
    {
        const QNetworkDatagram dg = m_socket->receiveDatagram(kMaxDatagramBytes + 1);
        if (dg.data().size() > kMaxDatagramBytes)
            continue; // quá lớn so với một gói quảng bá thật - không phân tích JSON rác

        const QJsonDocument doc = QJsonDocument::fromJson(dg.data());
        if (!doc.isObject())
            continue;

        const QJsonObject o = doc.object();
        if (o.value("app").toString() != kAppTag)
            continue; // không phải gói của ứng dụng này - bỏ qua

        const QString id = o.value("id").toString();
        if (id == m_myId || !ConnectProtocol::isValidPeerId(id))
            continue; // bỏ qua gói của chính mình + id sai định dạng (rỗng/quá dài/ký tự lạ)

        const int port = o.value("port").toInt();
        if (port < 1 || port > 65535)
            continue;

        const QHostAddress sender = dg.senderAddress();
        if (sender.isNull())
            continue;

        const QString name = ConnectProtocol::sanitizeMachineName(o.value("name").toString());
        const int version = o.value("v").toInt(0);
        emit peerAnnounced(id, name, sender, static_cast<quint16>(port), version);
    }
}
