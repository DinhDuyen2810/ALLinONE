#include "ConnectSessionController.h"

#include "CryptoSession.h"
#include "EdgeDetector.h"
#include "InputHook.h"
#include "NetworkSession.h"
#include "PairingCode.h"
#include "PeerDiscovery.h"
#include "PeerStore.h"

#include <QClipboard>
#include <QDateTime>
#include <QGuiApplication>
#include <QScreen>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <algorithm>

namespace
{
constexpr quint16 kPreferredPort = 47632;
constexpr int kEdgePollIntervalMs = 16; // ~60Hz, đủ mượt để chạm biên không bị giật/trễ
constexpr int kPairingReplyTimeoutMs = 10000;
}

ConnectSessionController::ConnectSessionController(QObject* parent)
    : QObject(parent)
    , m_identity(LocalIdentityStore::instance().identity())
{
}

ConnectSessionController::~ConnectSessionController()
{
    stop();
}

bool ConnectSessionController::start(QString* error)
{
    if (m_running)
        return true;

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &ConnectSessionController::onNewIncomingConnection);
    if (!m_server->listen(QHostAddress::AnyIPv4, kPreferredPort))
    {
        // Cổng ưu tiên bận - để hệ điều hành tự chọn cổng trống; PeerDiscovery luôn quảng bá đúng
        // cổng thật nên máy khác vẫn tìm thấy đúng, không cần cố định cổng.
        if (!m_server->listen(QHostAddress::AnyIPv4, 0))
        {
            if (error) *error = "Không mở được cổng TCP để nhận kết nối: " + m_server->errorString();
            m_server->deleteLater();
            m_server = nullptr;
            return false;
        }
    }

    m_discovery = new PeerDiscovery(this);
    connect(m_discovery, &PeerDiscovery::peerAnnounced, this, &ConnectSessionController::onPeerAnnounced);
    QString discoErr;
    if (!m_discovery->start(m_identity.id, m_identity.machineName, m_server->serverPort(), &discoErr))
        emit logMessage("⚠ Không khám phá được máy khác trong LAN: " + discoErr);

    m_hook = new InputHook(this);
    connect(m_hook, &InputHook::mouseMoveRelative, this, [this](int dx, int dy) {
        ProtocolMessage m;
        m.type = MessageType::MouseMove;
        m.intA = dx;
        m.intB = dy;
        if (NetworkSession* s = sessionFor(m_activePeerId))
            s->sendMessage(m);
    });
    connect(m_hook, &InputHook::mouseButtonChanged, this, [this](int button, bool pressed) {
        ProtocolMessage m;
        m.type = MessageType::MouseButton;
        m.intA = button;
        m.flagA = pressed;
        if (NetworkSession* s = sessionFor(m_activePeerId))
            s->sendMessage(m);
    });
    connect(m_hook, &InputHook::mouseWheelMoved, this, [this](int dy, int dx) {
        ProtocolMessage m;
        m.type = MessageType::MouseWheel;
        m.intA = dx;
        m.intB = dy;
        if (NetworkSession* s = sessionFor(m_activePeerId))
            s->sendMessage(m);
    });
    connect(m_hook, &InputHook::keyChanged, this, [this](int vk, int scan, bool pressed, bool extended) {
        ProtocolMessage m;
        m.type = MessageType::KeyEvent;
        m.intA = vk;
        m.intB = scan;
        m.flagA = pressed;
        m.flagB = extended;
        if (NetworkSession* s = sessionFor(m_activePeerId))
            s->sendMessage(m);
    });
    connect(m_hook, &InputHook::returnHotkeyPressed, this, [this] {
        if (m_role == ControlRole::Controlling)
            deactivateControlling(true);
    });

    QString hookErr;
    if (!m_hook->install(&hookErr))
    {
        emit logMessage("⚠ Không cài được hook bàn phím/chuột toàn cục: " + hookErr +
                        " (chia sẻ điều khiển từ máy này sẽ không hoạt động, nhưng vẫn có thể NHẬN điều khiển từ máy khác)");
    }

    m_edgeTimer = new QTimer(this);
    m_edgeTimer->setInterval(kEdgePollIntervalMs);
    connect(m_edgeTimer, &QTimer::timeout, this, &ConnectSessionController::onEdgeTimerTick);
    m_edgeTimer->start();

    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ConnectSessionController::onClipboardChanged);

    m_running = true;
    emit logMessage(QString("✓ Đã khởi động Connect Together. Tên máy: %1  •  Cổng TCP: %2")
                        .arg(m_identity.machineName)
                        .arg(m_server->serverPort()));
    return true;
}

void ConnectSessionController::stop()
{
    if (!m_running)
        return;

    if (m_role != ControlRole::Idle)
        deactivateControlling(true);

    QObject::disconnect(QGuiApplication::clipboard(), nullptr, this, nullptr);

    if (m_edgeTimer)
    {
        m_edgeTimer->stop();
        m_edgeTimer->deleteLater();
        m_edgeTimer = nullptr;
    }
    if (m_pairingTimer)
    {
        m_pairingTimer->stop();
        m_pairingTimer->deleteLater();
        m_pairingTimer = nullptr;
    }
    m_pairingCode.clear();

    for (NetworkSession* s : std::as_const(m_sessions))
        s->deleteLater();
    m_sessions.clear();
    for (NetworkSession* s : std::as_const(m_pendingSessions))
        s->deleteLater();
    m_pendingSessions.clear();

    if (m_hook)
    {
        m_hook->uninstall();
        m_hook->deleteLater();
        m_hook = nullptr;
    }
    if (m_discovery)
    {
        m_discovery->stop();
        m_discovery->deleteLater();
        m_discovery = nullptr;
    }
    if (m_server)
    {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }

    m_running = false;
}

quint16 ConnectSessionController::listenPort() const
{
    return m_server ? m_server->serverPort() : 0;
}

// ------------------------------------------------------------------ Ghép đôi

QString ConnectSessionController::beginPairingSession()
{
    m_pairingCode = PairingCode::generate();
    m_pairingSecondsLeft = PairingCode::kExpirySeconds;

    if (!m_pairingTimer)
    {
        m_pairingTimer = new QTimer(this);
        m_pairingTimer->setInterval(1000);
        connect(m_pairingTimer, &QTimer::timeout, this, &ConnectSessionController::onPairingTimerTick);
    }
    m_pairingTimer->start();

    emit pairingCodeGenerated(PairingCode::formatForDisplay(m_pairingCode), m_pairingSecondsLeft);
    emit logMessage(QString("🔑 Đã tạo mã ghép đôi, chờ máy khác nhập mã trong %1 phút.")
                        .arg(PairingCode::kExpirySeconds / 60));
    return m_pairingCode;
}

void ConnectSessionController::cancelPairingSession()
{
    m_pairingCode.clear();
    if (m_pairingTimer)
        m_pairingTimer->stop();
    emit pairingSessionClosed();
}

void ConnectSessionController::onPairingTimerTick()
{
    if (--m_pairingSecondsLeft <= 0)
    {
        cancelPairingSession();
        emit logMessage("⏱ Mã ghép đôi đã hết hạn.");
    }
}

void ConnectSessionController::connectWithCode(const QHostAddress& address, quint16 port, const QString& code)
{
    const QString normalized = PairingCode::normalize(code);
    if (normalized.isEmpty())
    {
        emit pairingFailed("Mã ghép đôi không hợp lệ (phải gồm đúng 9 chữ số).");
        return;
    }

    auto* session = new NetworkSession(this);
    session->setCryptoKey(CryptoSession::deriveKeyFromPairingCode(normalized));
    m_pendingSessions.push_back(session);

    connect(session, &NetworkSession::connected, this, [this, session] {
        ProtocolMessage req;
        req.type = MessageType::PairRequest;
        req.textA = m_identity.machineName;
        req.textB = m_identity.id;
        session->sendMessage(req);
    });
    connect(session, &NetworkSession::messageReceived, this,
           [this, session](ProtocolMessage msg) { handleMessage(session, QString(), msg); });
    connect(session, &NetworkSession::errorOccurred, this,
           [this](const QString& err) { emit pairingFailed("Lỗi kết nối: " + err); });
    connect(session, &NetworkSession::disconnected, this, [this, session] {
        m_pendingSessions.removeAll(session);
        session->deleteLater();
    });

    session->connectToHost(address, port);

    QTimer::singleShot(kPairingReplyTimeoutMs, session, [this, session] {
        if (m_pendingSessions.contains(session))
        {
            emit pairingFailed("Không nhận được phản hồi ghép đôi (hết thời gian chờ).");
            session->disconnectSession();
        }
    });
}

void ConnectSessionController::sendPairAccept(NetworkSession* session, const QString& remoteMachineName,
                                              const QString& remoteId)
{
    if (m_pairingCode.isEmpty())
    {
        ProtocolMessage reject;
        reject.type = MessageType::PairReject;
        reject.textA = "Phiên ghép đôi đã đóng hoặc hết hạn.";
        session->sendMessage(reject);
        session->disconnectSession();
        return;
    }

    const QByteArray newLongTermKey = CryptoSession::generateRandomKey();

    ProtocolMessage accept;
    accept.type = MessageType::PairAccept;
    accept.textA = m_identity.machineName;
    accept.textB = m_identity.id;
    accept.longTermKey = newLongTermKey;
    session->sendMessage(accept);

    finalizePairing(remoteId, remoteMachineName, session->peerAddress(), session->peerPort(), newLongTermKey);

    m_pendingSessions.removeAll(session);
    session->disconnectSession();
    session->deleteLater();

    cancelPairingSession(); // mã ghép đôi chỉ dùng một lần
}

void ConnectSessionController::finalizePairing(const QString& peerId, const QString& machineName,
                                               const QHostAddress& addr, quint16 port, const QByteArray& longTermKey)
{
    PairedPeer p;
    p.id = peerId;
    p.machineName = machineName;
    p.lastAddress = addr.toString();
    p.lastPort = port;
    p.longTermKey = longTermKey;
    p.autoConnect = true;
    p.pairedAt = QDateTime::currentDateTime();

    if (const PairedPeer* existing = PeerStore::instance().find(peerId))
        p.side = existing->side; // giữ lại vị trí đã xếp nếu ghép đôi lại

    PeerStore::instance().addOrUpdate(p);
    emit pairedPeersChanged();
    emit pairingSucceeded(peerId, machineName);
    emit logMessage("✓ Đã ghép đôi với " + machineName);
}

// ------------------------------------------------------------------ Kết nối đến/đi

void ConnectSessionController::onNewIncomingConnection()
{
    while (m_server->hasPendingConnections())
    {
        QTcpSocket* sock = m_server->nextPendingConnection();
        auto* session = new NetworkSession(this);
        session->adoptSocket(sock);

        // Ưu tiên: địa chỉ khớp một peer đã ghép đôi từng thấy qua discovery -> có khả năng cao đây là
        // kết nối lại bình thường, thử khóa dài hạn của peer đó trước.
        QString guessedPeerId;
        for (auto it = m_discovered.constBegin(); it != m_discovered.constEnd(); ++it)
        {
            if (it->address == sock->peerAddress())
            {
                guessedPeerId = it.key();
                break;
            }
        }

        bool keyAssigned = false;
        if (!guessedPeerId.isEmpty())
        {
            if (const PairedPeer* p = PeerStore::instance().find(guessedPeerId))
            {
                session->setCryptoKey(p->longTermKey);
                keyAssigned = true;
            }
        }
        if (!keyAssigned && !m_pairingCode.isEmpty())
        {
            session->setCryptoKey(CryptoSession::deriveKeyFromPairingCode(m_pairingCode));
            keyAssigned = true;
            guessedPeerId.clear(); // đây là một yêu cầu ghép đôi, chưa biết là ai
        }

        if (!keyAssigned)
        {
            // Không rõ danh tính và không có phiên chờ ghép đôi nào đang mở - từ chối an toàn.
            session->disconnectSession();
            session->deleteLater();
            continue;
        }

        m_pendingSessions.push_back(session);
        wireSession(session, guessedPeerId);
    }
}

void ConnectSessionController::wireSession(NetworkSession* session, const QString& tentativePeerId)
{
    connect(session, &NetworkSession::messageReceived, this, [this, session, tentativePeerId](ProtocolMessage msg) {
        handleMessage(session, tentativePeerId, msg);
    });
    connect(session, &NetworkSession::errorOccurred, this, [this, session](const QString&) {
        if (m_pendingSessions.contains(session))
        {
            m_pendingSessions.removeAll(session);
            session->disconnectSession();
            session->deleteLater();
        }
    });
    connect(session, &NetworkSession::disconnected, this, [this, session] {
        m_pendingSessions.removeAll(session);
        const QString peerId = m_sessions.key(session);
        if (!peerId.isEmpty())
        {
            m_sessions.remove(peerId);
            emit peerConnectionChanged(peerId, false);
            if (m_activePeerId == peerId && m_role != ControlRole::Idle)
                setRole(ControlRole::Idle, QString());
        }
        session->deleteLater();
    });
}

NetworkSession* ConnectSessionController::openSessionToPeer(const PairedPeer& peer)
{
    if (NetworkSession* existing = sessionFor(peer.id))
        return existing;

    auto* session = new NetworkSession(this);
    session->setCryptoKey(peer.longTermKey);
    m_pendingSessions.push_back(session);
    wireSession(session, peer.id);

    const QString myName = m_identity.machineName;
    connect(session, &NetworkSession::connected, this, [session, myName] {
        ProtocolMessage hello;
        hello.type = MessageType::SessionHello;
        hello.textA = myName;
        session->sendMessage(hello);
    });

    session->connectToHost(QHostAddress(peer.lastAddress), peer.lastPort);
    return session;
}

NetworkSession* ConnectSessionController::sessionFor(const QString& peerId) const
{
    if (peerId.isEmpty())
        return nullptr;
    const auto it = m_sessions.find(peerId);
    return it == m_sessions.end() ? nullptr : it.value();
}

// ------------------------------------------------------------------ Xử lý thông điệp

void ConnectSessionController::handleMessage(NetworkSession* session, const QString& sessionKey, const ProtocolMessage& msg)
{
    switch (msg.type)
    {
        case MessageType::PairRequest:
            sendPairAccept(session, msg.textA, msg.textB);
            break;

        case MessageType::PairAccept:
            finalizePairing(msg.textB, msg.textA, session->peerAddress(), session->peerPort(), msg.longTermKey);
            m_pendingSessions.removeAll(session);
            session->disconnectSession();
            session->deleteLater();
            break;

        case MessageType::PairReject:
            emit pairingFailed(msg.textA.isEmpty() ? "Máy kia từ chối ghép đôi." : msg.textA);
            m_pendingSessions.removeAll(session);
            session->deleteLater();
            break;

        case MessageType::SessionHello:
            if (!sessionKey.isEmpty())
            {
                m_pendingSessions.removeAll(session);
                m_sessions[sessionKey] = session;
                emit peerConnectionChanged(sessionKey, true);
                emit logMessage("✓ Đã kết nối lại với " + msg.textA);
            }
            break;

        case MessageType::ControlHandoff:
        {
            const auto side = static_cast<ScreenSide>(msg.intA);
            const double frac = msg.intB / 1000.0;
            const QString peerId = m_sessions.key(session);

            if (side == ScreenSide::None)
            {
                if (m_role == ControlRole::BeingControlled)
                    setRole(ControlRole::Idle, QString());
            }
            else if (!peerId.isEmpty())
            {
                const QRect bounds = localVirtualBounds();
                const QPoint entry = EdgeDetector::entryPoint(bounds, side, frac);
                m_injector.moveAbsolute(entry.x(), entry.y());
                setRole(ControlRole::BeingControlled, peerId);
                emit logMessage("🖱 Một máy khác đang điều khiển máy này (Ctrl+Alt+Home trên máy đó để trả lại).");
            }
            break;
        }

        case MessageType::MouseMove:
        case MessageType::MouseButton:
        case MessageType::MouseWheel:
        case MessageType::KeyEvent:
            if (m_role == ControlRole::BeingControlled)
                applyRemoteEvent(msg);
            break;

        case MessageType::ClipboardText:
            if (msg.textA != m_lastClipboardText)
            {
                m_applyingRemoteClipboard = true;
                m_lastClipboardText = msg.textA;
                QGuiApplication::clipboard()->setText(msg.textA);
                m_applyingRemoteClipboard = false;
            }
            break;

        case MessageType::Heartbeat:
        case MessageType::Goodbye:
        default:
            break;
    }
}

void ConnectSessionController::applyRemoteEvent(const ProtocolMessage& msg)
{
    switch (msg.type)
    {
        case MessageType::MouseMove:
            m_injector.moveRelative(msg.intA, msg.intB);
            break;
        case MessageType::MouseButton:
            m_injector.mouseButton(msg.intA, msg.flagA);
            break;
        case MessageType::MouseWheel:
            m_injector.mouseWheel(msg.intB, msg.intA);
            break;
        case MessageType::KeyEvent:
            m_injector.keyEvent(msg.intA, msg.intB, msg.flagA, msg.flagB);
            break;
        default:
            break;
    }
}

// ------------------------------------------------------------------ Biên màn hình & vai trò

QRect ConnectSessionController::localVirtualBounds() const
{
    QRect bounds;
    for (QScreen* screen : QGuiApplication::screens())
        bounds = bounds.united(screen->geometry());

    // QScreen::geometry() là tọa độ logic; quy đổi theo devicePixelRatio để khớp pixel vật lý mà
    // GetCursorPos/SetCursorPos dùng (cùng cách RuntimeOverlay của Auto Click đã xử lý trước đó).
    QScreen* primary = QGuiApplication::primaryScreen();
    const qreal dpr = primary ? primary->devicePixelRatio() : 1.0;
    if (dpr != 1.0)
        bounds = QRect(QPoint(static_cast<int>(bounds.left() * dpr), static_cast<int>(bounds.top() * dpr)),
                       QSize(static_cast<int>(bounds.width() * dpr), static_cast<int>(bounds.height() * dpr)));
    return bounds;
}

void ConnectSessionController::onEdgeTimerTick()
{
    if (m_role != ControlRole::Idle || !m_hook || !m_hook->isInstalled())
        return;

    QSet<ScreenSide> sidesWithNeighbor;
    QHash<int, QString> sideToPeerId;
    for (const PairedPeer& p : PeerStore::instance().peers())
    {
        if (p.side != ScreenSide::None && sessionFor(p.id))
        {
            sidesWithNeighbor.insert(p.side);
            sideToPeerId[static_cast<int>(p.side)] = p.id;
        }
    }
    if (sidesWithNeighbor.isEmpty())
        return;

    const QRect bounds = localVirtualBounds();
    const QPoint cursor = InputInjector::currentCursorPos();
    const ScreenSide crossed = EdgeDetector::detectCrossing(cursor, bounds, sidesWithNeighbor);
    if (crossed == ScreenSide::None)
        return;

    const QString peerId = sideToPeerId.value(static_cast<int>(crossed));
    const double frac = EdgeDetector::normalizedPositionAlongEdge(cursor, bounds, crossed);
    activateControlling(peerId, oppositeSide(crossed), frac);
}

void ConnectSessionController::activateControlling(const QString& peerId, ScreenSide enterSideOnPeer, double fraction)
{
    NetworkSession* session = sessionFor(peerId);
    if (!session)
        return;

    ProtocolMessage handoff;
    handoff.type = MessageType::ControlHandoff;
    handoff.intA = static_cast<int>(enterSideOnPeer);
    handoff.intB = static_cast<int>(fraction * 1000);
    session->sendMessage(handoff);

    m_hook->setActive(true);
    setRole(ControlRole::Controlling, peerId);
    emit logMessage("🖱 Đang điều khiển " + activePeerName() + " (nhấn Ctrl+Alt+Home để quay lại)");
}

void ConnectSessionController::deactivateControlling(bool notifyPeer)
{
    if (m_hook)
        m_hook->setActive(false);

    if (notifyPeer)
    {
        if (NetworkSession* session = sessionFor(m_activePeerId))
        {
            ProtocolMessage back;
            back.type = MessageType::ControlHandoff;
            back.intA = static_cast<int>(ScreenSide::None);
            session->sendMessage(back);
        }
    }
    setRole(ControlRole::Idle, QString());
    emit logMessage("✓ Đã quay lại điều khiển máy này.");
}

void ConnectSessionController::setRole(ControlRole role, const QString& peerId)
{
    m_role = role;
    m_activePeerId = peerId;
    emit roleChanged();
}

QString ConnectSessionController::activePeerName() const
{
    if (m_activePeerId.isEmpty())
        return QString();
    for (const PairedPeer& p : PeerStore::instance().peers())
        if (p.id == m_activePeerId)
            return p.machineName;
    return m_activePeerId;
}

// ------------------------------------------------------------------ Clipboard

void ConnectSessionController::onClipboardChanged()
{
    if (m_applyingRemoteClipboard)
        return;

    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty() || text == m_lastClipboardText)
        return;
    m_lastClipboardText = text;

    ProtocolMessage msg;
    msg.type = MessageType::ClipboardText;
    msg.textA = text;
    for (NetworkSession* s : std::as_const(m_sessions))
        s->sendMessage(msg);
}

// ------------------------------------------------------------------ Quản lý peer (API công khai)

void ConnectSessionController::onPeerAnnounced(const QString& id, const QString& name, const QHostAddress& addr,
                                               quint16 port)
{
    DiscoveredPeer d;
    d.id = id;
    d.machineName = name;
    d.address = addr;
    d.port = port;
    d.alreadyPaired = PeerStore::instance().find(id) != nullptr;
    m_discovered[id] = d;
    emit discoveredPeersChanged();

    if (PairedPeer* paired = PeerStore::instance().find(id))
    {
        paired->lastAddress = addr.toString();
        paired->lastPort = port;
        if (paired->autoConnect && !sessionFor(id))
            openSessionToPeer(*paired);
    }
}

QList<PairedPeer> ConnectSessionController::pairedPeers() const
{
    return PeerStore::instance().peers();
}

void ConnectSessionController::setPeerSide(const QString& peerId, ScreenSide side)
{
    if (const PairedPeer* p = PeerStore::instance().find(peerId))
    {
        PairedPeer updated = *p;
        updated.side = side;
        PeerStore::instance().addOrUpdate(updated);
        emit pairedPeersChanged();
    }
}

void ConnectSessionController::setPeerAutoConnect(const QString& peerId, bool autoConnect)
{
    if (const PairedPeer* p = PeerStore::instance().find(peerId))
    {
        PairedPeer updated = *p;
        updated.autoConnect = autoConnect;
        PeerStore::instance().addOrUpdate(updated);
        emit pairedPeersChanged();
    }
}

void ConnectSessionController::forgetPeer(const QString& peerId)
{
    if (NetworkSession* s = sessionFor(peerId))
    {
        m_sessions.remove(peerId);
        s->disconnectSession();
        s->deleteLater();
    }
    PeerStore::instance().remove(peerId);
    emit pairedPeersChanged();
}

QList<DiscoveredPeer> ConnectSessionController::discoveredPeers() const
{
    return m_discovered.values();
}
