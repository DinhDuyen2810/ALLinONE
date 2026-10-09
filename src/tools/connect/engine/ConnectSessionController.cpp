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
#include <QMutex>
#include <QMutexLocker>
#include <QScreen>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QThread>
#include <QThreadPool>
#include <QTimer>
#include <algorithm>
#include <limits>

namespace
{
constexpr quint16 kPreferredPort = 47632;
constexpr int kEdgePollIntervalMs = 16; // ~60Hz, đủ mượt để chạm biên không bị giật/trễ

// Mọi kết nối chưa bắt tay xong (đi hoặc đến, ghép đôi hoặc phiên) bị đóng sau ngần này - không kết nối
// nào được phép nằm "chờ" vô thời hạn (bản 1 rò một phiên mỗi 2 giây ở vài tình huống).
constexpr int kDefaultHandshakeTimeoutMs = 10000;
constexpr int kClosingGraceMs = 3000;            // chờ bên kia nhận nốt câu cuối (PairAccept/PairReject) rồi đóng hẳn
constexpr int kDefaultHousekeepingIntervalMs = 1000;

constexpr int kHeartbeatIntervalMs = 3000;       // nhịp gửi Heartbeat trên phiên đã xác thực
constexpr int kPeerSilentTimeoutMs = 10000;      // không nhận được GÌ từ máy kia lâu hơn mức này -> coi phiên đã chết
constexpr int kProbeTimeoutMs = 2500;            // thời gian chờ trả lời một Heartbeat dò "còn sống không"

constexpr int kMaxPendingIncoming = 16;          // trần số kết nối ĐẾN chưa bắt tay xong cùng lúc
constexpr int kMaxDiscovered = 64;               // trần số máy trong danh sách khám phá
constexpr int kDiscoveredExpiryMs = 10000;       // máy không quảng bá lại trong 10 giây thì rời danh sách

constexpr int kReconnectBaseMs = 5000;           // giãn cách thử kết nối lại: 5, 10, 20, 40, 60 giây...
constexpr int kReconnectMaxMs = 60000;
constexpr int kDiscoveryAttemptGapMs = 2000;     // vừa thấy quảng bá thì được thử lại sớm hơn, nhưng không dày hơn mức này

constexpr int kReturnInsetPx = 32;               // lùi con trỏ vào trong biên khi lấy lại quyền điều khiển
constexpr int kEdgeCooldownMs = 400;             // sau khi đổi vai trò, tạm không dò biên trong khoảng này

bool isValidPort(int port)
{
    return port >= 1 && port <= 65535;
}

/// Lý do từ chối do MÁY KIA gửi sang (PairReject) được đưa lên hộp thoại/nhật ký của máy này: cắt ngắn,
/// bỏ ký tự điều khiển và '<' '>' (để không bị hiểu thành rich text).
QString sanitizeRemoteText(const QString& text, int maxChars)
{
    QString out;
    for (const QChar c : text)
    {
        if (out.size() >= maxChars)
            break;
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format || c == '<' || c == '>')
            continue;
        out += c;
    }
    return out.trimmed();
}
} // namespace

/// Luồng nền suy khóa KHÔNG được chạm vào controller (có thể đã bị hủy trong 245-500 ms đó). Nó chỉ giữ
/// đối tượng này (shared_ptr - sống lâu hơn cả hai bên): suy xong thì khóa mutex, và CHỈ khi 'owner' còn
/// khác null mới gửi kết quả về luồng của controller qua một sự kiện xếp hàng. Hàm hủy của controller đặt
/// owner = null dưới cùng mutex đó, nên không có khe hở "vừa kiểm xong thì đối tượng bị hủy"; sự kiện đã
/// xếp hàng cho một QObject bị hủy thì Qt tự bỏ.
struct ConnectSessionController::KeyDeriveGuard
{
    QMutex mutex;
    ConnectSessionController* owner{nullptr};
};

ConnectSessionController::ConnectSessionController(QObject* parent)
    : QObject(parent)
    , m_identity(LocalIdentityStore::instance().identity())
    , m_injector(&m_realInjector)
    , m_keyGuard(std::make_shared<KeyDeriveGuard>())
{
    m_keyGuard->owner = this;
    m_clock.start();
}

ConnectSessionController::~ConnectSessionController()
{
    // Đang bị hủy: không phát thêm tín hiệu nào (nơi nhận - các tab của ConnectWindow - sẽ gọi ngược lại
    // vào một đối tượng đang hủy dở). Việc dừng êm khi người dùng đóng cửa sổ đã do closeEvent() lo.
    blockSignals(true);
    {
        // Từ đây luồng nền suy khóa (nếu còn đang chạy) không gửi kết quả về đối tượng này nữa.
        QMutexLocker locker(&m_keyGuard->mutex);
        m_keyGuard->owner = nullptr;
    }
    abandonPairingKeyJob();
    for (auto it = m_links.constBegin(); it != m_links.constEnd(); ++it)
        if (it->keyCancel)
            it->keyCancel->store(true);
    stop();
}

void ConnectSessionController::configureForTesting(const TestConfig& config)
{
    if (m_running)
        return;
    if (!config.identity.id.isEmpty())
        m_identity = config.identity;
    m_store = config.store;
    m_injector = config.injector ? config.injector : &m_realInjector;
    m_installHook = config.installGlobalHook;
    m_enableDiscovery = config.enableDiscovery;
    m_enableClipboard = config.enableClipboardSync;
    m_usePreferredPort = config.usePreferredPort;
    m_handshakeTimeoutMs = config.handshakeTimeoutMs;
    m_housekeepingIntervalMs = config.housekeepingIntervalMs;
    m_keyDerivationDelayMs = config.keyDerivationDelayMs;
}

PeerStore& ConnectSessionController::store() const
{
    return m_store ? *m_store : PeerStore::instance();
}

void ConnectSessionController::reportStoreSaveResult(bool saved, const QString& what)
{
    if (saved)
        return;
    // Thay đổi đã có hiệu lực trong phiên làm việc này nhưng KHÔNG nằm trên đĩa: lần mở ứng dụng kế tiếp sẽ
    // thấy lại trạng thái cũ (máy vừa ghép đôi biến mất, máy vừa "quên" - cùng khóa của nó - quay lại). Phải
    // cho người dùng biết, không được im lặng.
    emit logMessage(QString("⚠ Không lưu được danh sách máy đã ghép đôi khi %1: %2. Thay đổi này chỉ có hiệu lực "
                            "tới khi đóng ứng dụng - lần mở sau sẽ trở lại như cũ.")
                        .arg(what, store().lastSaveError()));
}

int ConnectSessionController::handshakeTimeoutMs() const
{
    return m_handshakeTimeoutMs > 0 ? m_handshakeTimeoutMs : kDefaultHandshakeTimeoutMs;
}

bool ConnectSessionController::start(QString* error)
{
    if (m_running)
        return true;

    m_server = new QTcpServer(this);
    connect(m_server, &QTcpServer::newConnection, this, &ConnectSessionController::onNewIncomingConnection);
    if (!m_usePreferredPort || !m_server->listen(QHostAddress::AnyIPv4, kPreferredPort))
    {
        // Cổng ưu tiên bận - để hệ điều hành tự chọn cổng trống; cổng thật được quảng bá qua PeerDiscovery
        // và gửi kèm trong PairRequest/SessionHello nên máy khác vẫn kết nối lại đúng, không cần cố định.
        if (!m_server->listen(QHostAddress::AnyIPv4, 0))
        {
            if (error) *error = "Không mở được cổng TCP để nhận kết nối: " + m_server->errorString();
            m_server->deleteLater();
            m_server = nullptr;
            return false;
        }
    }

    if (m_enableDiscovery)
    {
        m_discovery = new PeerDiscovery(this);
        connect(m_discovery, &PeerDiscovery::peerAnnounced, this, &ConnectSessionController::onPeerAnnounced);
        QString discoErr;
        if (!m_discovery->start(m_identity.id, m_identity.machineName, m_server->serverPort(), &discoErr))
            emit logMessage("⚠ Không khám phá được máy khác trong LAN: " + discoErr);
    }

    m_hook = new InputHook(this);
    connect(m_hook, &InputHook::mouseMoveRelative, this, [this](int dx, int dy) {
        ProtocolMessage m;
        m.type = MessageType::MouseMove;
        m.intA = dx;
        m.intB = dy;
        forwardInput(m);
    });
    connect(m_hook, &InputHook::mouseButtonChanged, this, [this](int button, bool pressed) {
        ProtocolMessage m;
        m.type = MessageType::MouseButton;
        m.intA = button;
        m.flagA = pressed;
        forwardInput(m);
    });
    connect(m_hook, &InputHook::mouseWheelMoved, this, [this](int dy, int dx) {
        ProtocolMessage m;
        m.type = MessageType::MouseWheel;
        m.intA = dx;
        m.intB = dy;
        forwardInput(m);
    });
    connect(m_hook, &InputHook::keyChanged, this, [this](int vk, int scan, bool pressed, bool extended) {
        ProtocolMessage m;
        m.type = MessageType::KeyEvent;
        m.intA = vk;
        m.intB = scan;
        m.flagA = pressed;
        m.flagB = extended;
        forwardInput(m);
    });
    connect(m_hook, &InputHook::returnHotkeyPressed, this, &ConnectSessionController::onReturnHotkey);

    if (m_installHook)
    {
        QString hookErr;
        if (!m_hook->install(&hookErr))
        {
            emit logMessage("⚠ Không cài được hook bàn phím/chuột toàn cục: " + hookErr +
                            " (chia sẻ điều khiển từ máy này sẽ không hoạt động, nhưng vẫn có thể NHẬN điều khiển từ máy khác)");
        }
    }

    m_edgeTimer = new QTimer(this);
    m_edgeTimer->setInterval(kEdgePollIntervalMs);
    connect(m_edgeTimer, &QTimer::timeout, this, &ConnectSessionController::onEdgeTimerTick);
    m_edgeTimer->start();

    m_houseTimer = new QTimer(this);
    m_houseTimer->setInterval(m_housekeepingIntervalMs > 0 ? m_housekeepingIntervalMs : kDefaultHousekeepingIntervalMs);
    connect(m_houseTimer, &QTimer::timeout, this, &ConnectSessionController::onHousekeepingTick);
    m_houseTimer->start();
    m_lastHeartbeatMs = nowMs();
    m_lastReconnectSweepMs = nowMs();

    if (m_enableClipboard)
        connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &ConnectSessionController::onClipboardChanged);

    m_edgeArmed = false;
    m_edgeCooldownUntilMs = 0;
    m_running = true;
    emit logMessage(QString("✓ Đã khởi động Connect Together. Tên máy: %1  •  Cổng TCP: %2")
                        .arg(m_identity.machineName)
                        .arg(m_server->serverPort()));

    // Không chờ gói quảng bá UDP: thử ngay địa chỉ đã xác nhận ở lần kết nối trước của từng máy đã ghép đôi.
    QTimer::singleShot(0, this, &ConnectSessionController::reconnectNow);
    return true;
}

void ConnectSessionController::stop()
{
    if (!m_running)
        return;

    // Trả/nhả quyền điều khiển cho tử tế TRƯỚC khi đóng phiên (gửi lệnh nhả phím + ControlHandoff(None)).
    if (m_role == ControlRole::Controlling)
        deactivateControlling(true);
    else if (m_role == ControlRole::BeingControlled)
        endBeingControlled(true);
    if (m_hook)
        m_hook->setActive(false);

    m_running = false; // từ đây mọi kết nối mới đến/đi đều bị từ chối

    if (m_enableClipboard)
        QObject::disconnect(QGuiApplication::clipboard(), nullptr, this, nullptr);

    if (m_edgeTimer)
    {
        m_edgeTimer->stop();
        m_edgeTimer->deleteLater();
        m_edgeTimer = nullptr;
    }
    if (m_houseTimer)
    {
        m_houseTimer->stop();
        m_houseTimer->deleteLater();
        m_houseTimer = nullptr;
    }
    if (m_pairingTimer)
    {
        m_pairingTimer->stop();
        m_pairingTimer->deleteLater();
        m_pairingTimer = nullptr;
    }
    const bool hadPairingOpen = !m_pairingCode.isEmpty();
    m_pairingCode.clear();
    m_pairingKey.clear();
    m_pairingFailures = 0;
    abandonPairingKeyJob(); // khóa của mã vừa đóng còn đang suy ở luồng nền: kết quả về sau sẽ bị bỏ

    if (m_server)
    {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }

    // Đóng mọi kết nối. Ngắt tín hiệu trước để việc đóng không gọi ngược lại vào controller đang dọn dẹp.
    const QHash<NetworkSession*, Link> links = m_links;
    m_links.clear();
    m_sessions.clear();
    QStringList wasConnected;
    for (auto it = links.constBegin(); it != links.constEnd(); ++it)
    {
        NetworkSession* session = it.key();
        QObject::disconnect(session, nullptr, this, nullptr);
        if (it->keyCancel)
            it->keyCancel->store(true); // lần ghép đôi đi còn đang suy khóa - kết quả về sau không còn mục nào để nhận
        if (it->stage == LinkStage::Established)
        {
            // Báo cho máy kia biết ta chủ động dừng (nó dọn phiên ngay, khỏi chờ hết hạn Heartbeat).
            ProtocolMessage bye;
            bye.type = MessageType::Goodbye;
            session->sendMessage(bye);
            wasConnected << it->peerId;
        }
        session->disconnectSession();
        session->deleteLater();
    }
    m_reconnect.clear();

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
    const bool hadDiscovered = !m_discovered.isEmpty();
    m_discovered.clear();
    m_warnedIncompatible.clear();
    m_forwardedKeysDown.clear();
    m_forwardedButtonsDown.clear();
    m_injectedKeysDown.clear();
    m_injectedButtonsDown.clear();

    for (const QString& id : std::as_const(wasConnected))
        emit peerConnectionChanged(id, false);
    if (hadDiscovered)
        emit discoveredPeersChanged();
    if (hadPairingOpen)
        emit pairingSessionClosed();
}

quint16 ConnectSessionController::listenPort() const
{
    return m_server ? m_server->serverPort() : 0;
}

QString ConnectSessionController::peerDisplayName(const QString& peerId) const
{
    if (const PairedPeer* p = store().find(peerId))
        return p->machineName.isEmpty() ? peerId : p->machineName;
    return peerId;
}

void ConnectSessionController::emitPairingFailedLater(const QString& reason)
{
    // Phát ở vòng lặp sự kiện kế tiếp: ConnectWindow bật một hộp thoại (vòng lặp sự kiện lồng) khi nhận
    // tín hiệu này - không để điều đó xảy ra ngay giữa lúc đang xử lý dữ liệu của một socket.
    QMetaObject::invokeMethod(this, [this, reason] { emit pairingFailed(reason); }, Qt::QueuedConnection);
}

// ------------------------------------------------------------------ Ghép đôi

QString ConnectSessionController::beginPairingSession()
{
    // Kết nối ghép đôi còn dở của mã TRƯỚC (nếu có) không được phép dùng tiếp với mã mới.
    cancelPairingSession();

    m_pairingCode = PairingCode::generate();
    // PBKDF2 100k vòng tốn 245-500 ms - tính MỘT LẦN cho mỗi mã, không tính lại cho từng kết nối đến (bản 1
    // tính lại mỗi kết nối ngay trên luồng giao diện: chỉ cần mở dồn dập kết nối là treo cả ứng dụng lẫn
    // hook chuột/bàn phím toàn cục), và tính ở LUỒNG NỀN: luồng này còn phải phục vụ hook cấp thấp - đứng
    // nửa giây là chuột/bàn phím của cả máy khựng theo. Mã được hiển thị ngay; kết nối ghép đôi tới trước
    // khi khóa suy xong được giữ lại chờ (onPreamble -> AwaitPairingKey) chứ không bị từ chối.
    const quint64 job = ++m_keyJobCounter;
    m_pairingKeyJob = job;
    m_pairingKeyCancel =
        deriveKeyInBackground(m_pairingCode, [this, job](const QByteArray& key) { onPairingKeyDerived(job, key); });
    m_pairingSecondsLeft = PairingCode::kExpirySeconds;
    m_pairingFailures = 0;

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
    const bool wasOpen = !m_pairingCode.isEmpty();
    m_pairingCode.clear();
    m_pairingKey.clear();
    m_pairingFailures = 0;
    abandonPairingKeyJob(); // hủy/hết hạn/đổi mã ngay trong lúc đang suy khóa: kết quả về sau sẽ bị bỏ
    if (m_pairingTimer)
        m_pairingTimer->stop();

    // Kết nối ghép đôi đang chờ PairRequest (hoặc đang chờ khóa của mã vừa đóng suy xong) vẫn gắn với mã
    // vừa đóng - đóng luôn (kết nối đang ở bước Closing là kết nối vừa ghép đôi XONG, để yên cho nó gửi nốt
    // PairAccept).
    QList<NetworkSession*> stale;
    for (auto it = m_links.constBegin(); it != m_links.constEnd(); ++it)
        if (it->kind == LinkKind::PairResponder && it->stage != LinkStage::Closing)
            stale << it.key();
    for (NetworkSession* s : std::as_const(stale))
        dropLink(s);

    if (wasOpen)
        emit pairingSessionClosed();
}

ConnectSessionController::KeyCancelFlag ConnectSessionController::deriveKeyInBackground(
    const QString& normalizedCode, std::function<void(const QByteArray&)> onDone)
{
    // Mỗi lần suy khóa một cờ hủy MỚI. PBKDF2 đang chạy thì không ngắt giữa chừng được, nhưng việc còn nằm
    // trong hàng đợi của bể luồng (bấm tạo mã/ghép đôi dồn dập) thấy cờ là bỏ luôn, không đốt CPU vô ích.
    auto cancelled = std::make_shared<std::atomic_bool>(false);
    const std::shared_ptr<KeyDeriveGuard> guard = m_keyGuard;
    const int delayMs = m_keyDerivationDelayMs;
    QThreadPool::globalInstance()->start([guard, cancelled, normalizedCode, delayMs, onDone = std::move(onDone)]() {
        if (delayMs > 0)
            QThread::msleep(static_cast<unsigned long>(delayMs));
        if (cancelled->load())
            return;
        const QByteArray key = CryptoSession::deriveKeyFromPairingCode(normalizedCode);
        if (cancelled->load())
            return;
        QMutexLocker locker(&guard->mutex);
        if (guard->owner)
            QMetaObject::invokeMethod(guard->owner, [onDone, key] { onDone(key); }, Qt::QueuedConnection);
    });
    return cancelled;
}

void ConnectSessionController::abandonPairingKeyJob()
{
    m_pairingKeyJob = 0;
    if (m_pairingKeyCancel)
    {
        m_pairingKeyCancel->store(true);
        m_pairingKeyCancel.reset();
    }
}

void ConnectSessionController::onPairingKeyDerived(quint64 job, const QByteArray& key)
{
    // Kết quả của một mã đã bị hủy/hết hạn/thay bằng mã khác, hoặc controller đã stop() - bỏ. Không bao giờ
    // để khóa của mã CŨ trở thành khóa của mã đang mở.
    if (job == 0 || job != m_pairingKeyJob)
        return;
    m_pairingKeyJob = 0;
    m_pairingKeyCancel.reset();

    if (key.size() != CryptoSession::kKeyBytes)
    {
        cancelPairingSession(); // đóng luôn mã (kèm các kết nối đang chờ khóa) - mã không có khóa thì vô dụng
        emit logMessage("⚠ Không suy được khóa từ mã ghép đôi (lỗi hệ thống) - đã đóng mã, hãy tạo mã mới.");
        return;
    }
    m_pairingKey = key;

    // Các kết nối ghép đôi đã tới trong lúc chờ khóa: đặt khóa bắt tay rồi cho đọc tiếp (PairRequest của
    // chúng thường đã nằm sẵn trong bộ đệm).
    QList<NetworkSession*> parked;
    for (auto it = m_links.constBegin(); it != m_links.constEnd(); ++it)
        if (it->kind == LinkKind::PairResponder && it->stage == LinkStage::AwaitPairingKey)
            parked << it.key();
    for (NetworkSession* s : std::as_const(parked))
    {
        Link* link = linkFor(s);
        if (!link)
            continue;
        link->stage = LinkStage::AwaitPairRequest;
        link->baseKey = m_pairingKey;
        link->createdMs = link->lastRxMs = nowMs(); // hạn bắt tay tính từ lúc có khóa, không tính thời gian suy khóa
        const QByteArray preamble = link->preamble;
        link->preamble.clear();
        s->setHandshakeKey(m_pairingKey, preamble);
        s->setReceivePaused(false);
    }
}

void ConnectSessionController::onOutgoingKeyDerived(NetworkSession* session, quint64 job, const QByteArray& key)
{
    // 'session' chỉ dùng làm khóa tra sổ, không được chạm vào trước khi tra: lần ghép đôi đó có thể đã bị
    // thay/hủy (bấm "Ghép đôi" lần nữa, stop()) trong lúc suy khóa và đối tượng đã bị hủy. Số hiệu 'job'
    // chặn cả trường hợp một NetworkSession mới được cấp phát trùng đúng địa chỉ cũ.
    Link* link = linkFor(session);
    if (!link || job == 0 || link->keyJob != job || link->stage != LinkStage::DerivingKey)
        return;
    link->keyCancel.reset();

    if (!m_running || key.size() != CryptoSession::kKeyBytes)
    {
        failPairing(session, "Không suy được khóa từ mã ghép đôi (lỗi hệ thống).");
        return;
    }
    link->baseKey = key;
    link->stage = LinkStage::Connecting;
    link->createdMs = link->lastRxMs = nowMs(); // hạn bắt tay tính từ lúc bắt đầu nối, như trước
    const QHostAddress address = link->remoteAddress;
    const quint16 port = link->remoteListenPort;
    session->connectToHost(address, port);
}

void ConnectSessionController::onPairingTimerTick()
{
    if (--m_pairingSecondsLeft <= 0)
    {
        cancelPairingSession();
        emit logMessage("⏱ Mã ghép đôi đã hết hạn.");
    }
}

void ConnectSessionController::registerPairingFailure()
{
    if (m_pairingKey.isEmpty())
        return;

    ++m_pairingFailures;
    if (m_pairingFailures >= PairingCode::kMaxAttempts)
    {
        // Mỗi kết nối chỉ thử được một mã; không chặn thì trong 5 phút mở mã, một máy trong LAN thử được
        // không giới hạn số mã. Hủy mã sau vài lần sai - người dùng tạo mã mới nếu đó là nhập nhầm thật.
        cancelPairingSession();
        emit logMessage(QString("⚠ Đã hủy mã ghép đôi: có %1 lần thử ghép đôi bằng mã sai.").arg(PairingCode::kMaxAttempts));
        emitPairingFailedLater("Mã ghép đôi đã bị hủy vì có quá nhiều lần nhập sai từ máy khác. Hãy tạo mã mới nếu "
                               "bạn vẫn muốn ghép đôi.");
    }
    else
    {
        emit logMessage(QString("⚠ Một máy vừa thử ghép đôi bằng mã sai (lần %1/%2).")
                            .arg(m_pairingFailures)
                            .arg(PairingCode::kMaxAttempts));
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
    if (!m_running)
    {
        emit pairingFailed("Connect Together chưa khởi động - hãy mở lại cửa sổ rồi thử lại.");
        return;
    }
    if (address.isNull() || port == 0)
    {
        emit pairingFailed("Địa chỉ hoặc cổng của máy kia không hợp lệ.");
        return;
    }

    // Máy đó đã tự quảng bá là dùng phiên bản giao thức khác: báo rõ ngay, khỏi kết nối rồi nhận về một
    // lỗi "máy kia đóng kết nối" khó hiểu (và khỏi tiêu một lần thử mã của máy kia).
    for (auto it = m_discovered.constBegin(); it != m_discovered.constEnd(); ++it)
    {
        if (it->address == address && it->port == port && !it->compatible)
        {
            emit pairingFailed(QString("Máy \"%1\" đang dùng phiên bản Connect Together khác (giao thức v%2, máy này v%3). "
                                       "Hãy cập nhật One for ALL trên cả hai máy lên cùng phiên bản rồi ghép đôi lại.")
                                   .arg(it->machineName)
                                   .arg(it->protocolVersion)
                                   .arg(ConnectProtocol::kVersion));
            return;
        }
    }

    // Mỗi lúc chỉ một lần ghép đôi đi: bấm "Ghép đôi" lần nữa thay cho lần đang dở.
    QList<NetworkSession*> previous;
    for (auto it = m_links.constBegin(); it != m_links.constEnd(); ++it)
        if (it->kind == LinkKind::PairInitiator && it->stage != LinkStage::Closing)
            previous << it.key();
    for (NetworkSession* s : std::as_const(previous))
        dropLink(s);

    // K_code suy ở luồng nền (245-500 ms - xem beginPairingSession). Mục trong sổ được tạo NGAY để mọi quy
    // tắc sẵn có vẫn đúng trong lúc chờ: "đang có một lần ghép đôi đi" (outgoingPairingInFlight - hoãn
    // PairRequest đi chéo), bấm lần nữa thay cho lần đang dở, stop() dọn sạch. Kết nối TCP chỉ mở khi có
    // khóa (onOutgoingKeyDerived), nên máy kia vẫn thấy đúng trình tự cũ: nối xong là có preamble + PairRequest.
    auto* session = new NetworkSession(this);
    Link link;
    link.kind = LinkKind::PairInitiator;
    link.stage = LinkStage::DerivingKey;
    link.remoteAddress = address;
    link.remoteListenPort = port;
    link.createdMs = link.lastRxMs = nowMs();
    const quint64 job = ++m_keyJobCounter;
    link.keyJob = job;
    link.keyCancel = deriveKeyInBackground(
        normalized, [this, session, job](const QByteArray& key) { onOutgoingKeyDerived(session, job, key); });
    m_links.insert(session, link);
    wireSession(session);
}

void ConnectSessionController::failPairing(NetworkSession* session, const QString& reason)
{
    if (!linkFor(session))
        return;
    dropLink(session);
    emit logMessage("⚠ Ghép đôi thất bại: " + reason);
    emitPairingFailedLater(reason);
}

void ConnectSessionController::handlePairRequest(NetworkSession* session, const ProtocolMessage& msg)
{
    Link* link = linkFor(session);
    if (!link)
        return;

    // Giải mã được PairRequest = máy kia biết đúng mã. Vẫn phải kiểm: mã đó có CÒN là mã đang mở không
    // (kết nối mở từ lúc mã trước còn hiệu lực không được dùng lại cho mã sau), và nội dung tự khai hợp lệ.
    QString rejectReason;
    if (m_pairingKey.isEmpty() || link->baseKey != m_pairingKey)
        rejectReason = "Phiên ghép đôi đã đóng hoặc hết hạn.";
    else if (msg.intA != ConnectProtocol::kVersion)
        rejectReason = "Hai máy dùng phiên bản Connect Together khác nhau - hãy cập nhật cả hai máy.";
    else if (!ConnectProtocol::isValidPeerId(msg.textB) || msg.textB == m_identity.id)
        rejectReason = "Định danh của máy gửi yêu cầu không hợp lệ.";

    // Hai máy cùng nhập mã cho nhau (mỗi máy vừa tạo mã vừa bấm "Ghép đôi" vào máy kia, hai yêu cầu đi chéo
    // nhau): nếu cả hai cùng trả lời ngay thì mỗi máy sinh MỘT khóa dài hạn riêng, và mỗi máy kết thúc với
    // khóa do máy KIA sinh ra (PairAccept tới sau khi đã lưu khóa của mình) - cả hai báo "đã ghép đôi" nhưng
    // giữ hai khóa khác nhau, không bao giờ bắt tay phiên được. Phá thế đối xứng: máy có id LỚN hơn hoãn
    // việc trả lời cho tới khi lần ghép đôi đi của chính nó kết thúc (resumeHeldPairRequests()); khi đó thứ
    // tự ở cả hai máy đều là "khóa của máy id nhỏ trước, khóa của máy id lớn sau" -> cùng giữ một khóa.
    if (rejectReason.isEmpty() && m_identity.id > msg.textB && outgoingPairingInFlight())
    {
        link->pairRequestHeld = true;
        link->heldPairRequest = msg;
        link->createdMs = nowMs(); // tính lại hạn chờ từ lúc bắt đầu hoãn
        return;
    }
    link->pairRequestHeld = false;

    const QByteArray newLongTermKey = rejectReason.isEmpty() ? CryptoSession::generateRandomKey() : QByteArray();
    if (rejectReason.isEmpty() && newLongTermKey.size() != CryptoSession::kKeyBytes)
        rejectReason = "Không sinh được khóa (lỗi hệ thống).";

    if (!rejectReason.isEmpty())
    {
        ProtocolMessage reject;
        reject.type = MessageType::PairReject;
        reject.textA = rejectReason;
        session->sendMessage(reject);
        closeLinkGracefully(session);
        return;
    }

    const QString remoteId = msg.textB;
    const QString remoteName = ConnectProtocol::sanitizeMachineName(msg.textA);
    // Cổng LẮNG NGHE máy kia tự khai - KHÔNG phải session->peerPort() (đó là cổng nguồn tạm thời của kết
    // nối này, nối lại vào đó sẽ không bao giờ được).
    const quint16 remoteListenPort = isValidPort(msg.intB) ? static_cast<quint16>(msg.intB) : 0;
    const QHostAddress remoteAddress = session->peerAddress();

    ProtocolMessage accept;
    accept.type = MessageType::PairAccept;
    accept.textA = m_identity.machineName;
    accept.textB = m_identity.id;
    accept.longTermKey = newLongTermKey;
    accept.intA = ConnectProtocol::kVersion;
    accept.intB = listenPort();
    if (!session->sendMessage(accept))
    {
        dropLink(session);
        return;
    }

    // KHÔNG deleteLater() phiên ở đây: closeLinkGracefully() để socket gửi nốt PairAccept rồi tín hiệu
    // disconnected (hoặc hạn kClosingGraceMs) mới dọn - một điểm dọn dẹp duy nhất cho mọi kết nối (dropLink).
    closeLinkGracefully(session);
    cancelPairingSession(); // mã ghép đôi chỉ dùng một lần
    finalizePairing(remoteId, remoteName, remoteAddress, remoteListenPort, newLongTermKey);
}

bool ConnectSessionController::outgoingPairingInFlight() const
{
    for (auto it = m_links.constBegin(); it != m_links.constEnd(); ++it)
        if (it->kind == LinkKind::PairInitiator && it->stage != LinkStage::Closing)
            return true;
    return false;
}

void ConnectSessionController::resumeHeldPairRequests()
{
    if (outgoingPairingInFlight())
        return; // người dùng vừa bấm "Ghép đôi" lần nữa - chờ tiếp lần đó

    QList<NetworkSession*> held;
    for (auto it = m_links.constBegin(); it != m_links.constEnd(); ++it)
        if (it->pairRequestHeld)
            held << it.key();
    for (NetworkSession* s : std::as_const(held))
    {
        // Trả lời yêu cầu đầu tiên là dùng xong mã (cancelPairingSession() đóng các yêu cầu đang hoãn còn
        // lại) - nên phải tra lại sổ ở mỗi vòng.
        Link* link = linkFor(s);
        if (!link || !link->pairRequestHeld)
            continue;
        const ProtocolMessage request = link->heldPairRequest;
        link->heldPairRequest = ProtocolMessage();
        handlePairRequest(s, request);
    }
}

void ConnectSessionController::handlePairAccept(NetworkSession* session, const ProtocolMessage& msg)
{
    Link* link = linkFor(session);
    if (!link)
        return;

    if (msg.intA != ConnectProtocol::kVersion || msg.longTermKey.size() != CryptoSession::kKeyBytes ||
        !ConnectProtocol::isValidPeerId(msg.textB) || msg.textB == m_identity.id)
    {
        failPairing(session, "Phản hồi ghép đôi của máy kia không hợp lệ.");
        return;
    }

    const QString remoteId = msg.textB;
    const QString remoteName = ConnectProtocol::sanitizeMachineName(msg.textA);
    const QHostAddress remoteAddress = link->remoteAddress; // địa chỉ + cổng ta vừa nối tới = cổng lắng nghe thật
    const quint16 remoteListenPort = link->remoteListenPort;
    const QByteArray key = msg.longTermKey;

    dropLink(session);
    finalizePairing(remoteId, remoteName, remoteAddress, remoteListenPort, key);
}

void ConnectSessionController::finalizePairing(const QString& peerId, const QString& machineName,
                                               const QHostAddress& addr, quint16 port, const QByteArray& longTermKey)
{
    // Ghép đôi lại với một máy đã ghép đôi: mọi kết nối cũ tới máy đó đang dùng khóa cũ - đóng hết.
    dropLinksForPeer(peerId);
    m_reconnect.remove(peerId);

    PairedPeer p;
    p.id = peerId;
    p.machineName = machineName;
    p.lastAddress = addr.toString();
    p.lastPort = port;
    p.longTermKey = longTermKey;
    p.autoConnect = true;
    p.pairedAt = QDateTime::currentDateTime();

    if (const PairedPeer* existing = store().find(peerId))
        p.side = existing->side; // giữ lại vị trí đã xếp nếu ghép đôi lại

    const bool saved = store().addOrUpdate(p);
    emit pairedPeersChanged();
    emit pairingSucceeded(peerId, machineName);
    emit logMessage("✓ Đã ghép đôi với " + machineName);
    reportStoreSaveResult(saved, "ghép đôi với " + machineName);

    // Mở phiên ngay (cả hai máy cùng làm - quy tắc phân xử kết nối chéo lo phần trùng), không chờ quảng bá.
    QTimer::singleShot(300, this, [this, peerId] { attemptReconnect(peerId, QHostAddress(), 0, false); });
}

// ------------------------------------------------------------------ Vòng đời kết nối

ConnectSessionController::Link* ConnectSessionController::linkFor(NetworkSession* session)
{
    // LƯU Ý: con trỏ trả về chỉ dùng được tới lần thêm/xóa m_links kế tiếp (QHash có thể băm lại) - và
    // NetworkSession::sendMessage() có thể dẫn tới dropLink() (ghi socket lỗi). Sau mỗi lời gọi như vậy
    // phải gọi lại linkFor().
    const auto it = m_links.find(session);
    return it == m_links.end() ? nullptr : &it.value();
}

void ConnectSessionController::wireSession(NetworkSession* session)
{
    connect(session, &NetworkSession::connected, this, [this, session] { onSessionConnected(session); });
    connect(session, &NetworkSession::preambleReceived, this,
           [this, session](const QByteArray& preamble) { onPreamble(session, preamble); });
    connect(session, &NetworkSession::messageReceived, this,
           [this, session](const ProtocolMessage& msg) { onMessage(session, msg); });
    connect(session, &NetworkSession::decryptFailed, this, [this, session] { onDecryptFailed(session); });
    connect(session, &NetworkSession::errorOccurred, this,
           [this, session](const QString& err) { onSessionEnded(session, err); });
    connect(session, &NetworkSession::disconnected, this, [this, session] { onSessionEnded(session, QString()); });
}

void ConnectSessionController::dropLink(NetworkSession* session)
{
    // Điểm dọn dẹp DUY NHẤT của một kết nối: gỡ khỏi sổ, ngắt tín hiệu (để không tín hiệu nào của nó gọi
    // lại vào đây lần hai), đóng socket, hẹn hủy đối tượng. Gọi nhiều lần cho cùng một phiên là vô hại.
    const auto it = m_links.find(session);
    if (it == m_links.end())
        return;
    const Link link = it.value();
    m_links.erase(it);
    QObject::disconnect(session, nullptr, this, nullptr);
    if (link.keyCancel)
        link.keyCancel->store(true); // còn đang suy khóa cho kết nối này - không cần nữa

    const bool wasRegistered = link.stage == LinkStage::Established && m_sessions.value(link.peerId) == session;
    if (wasRegistered)
        m_sessions.remove(link.peerId);

    session->abortSession();
    session->deleteLater();

    // Lần ghép đôi đi vừa kết thúc (thành công hay thất bại): tới lượt các yêu cầu ghép đôi ĐẾN đang hoãn.
    // Ở vòng lặp sự kiện kế tiếp - khi thành công, nơi gọi (handlePairAccept) còn phải lưu khóa vừa nhận
    // TRƯỚC khi ta sinh khóa mới cho yêu cầu đang hoãn (xem handlePairRequest).
    if (link.kind == LinkKind::PairInitiator)
        QMetaObject::invokeMethod(this, [this] { resumeHeldPairRequests(); }, Qt::QueuedConnection);

    if (wasRegistered)
    {
        emit peerConnectionChanged(link.peerId, false);
        handlePeerSessionLost(link.peerId);
    }
}

void ConnectSessionController::closeLinkGracefully(NetworkSession* session)
{
    Link* link = linkFor(session);
    if (!link)
        return;
    if (link->stage == LinkStage::Established)
    {
        dropLink(session); // phiên đã đăng ký không đi đường này - đóng thẳng cho gọn sổ
        return;
    }
    link->stage = LinkStage::Closing;
    link->createdMs = nowMs(); // mốc tính hạn kClosingGraceMs
    session->disconnectSession();
}

void ConnectSessionController::dropLinksForPeer(const QString& peerId)
{
    QList<NetworkSession*> targets;
    for (auto it = m_links.constBegin(); it != m_links.constEnd(); ++it)
        if ((it->kind == LinkKind::SessionInitiator || it->kind == LinkKind::SessionResponder) && it->peerId == peerId)
            targets << it.key();
    for (NetworkSession* s : std::as_const(targets))
        dropLink(s);
}

void ConnectSessionController::protocolViolation(NetworkSession* session, const QString& what)
{
    Link* link = linkFor(session);
    if (!link)
        return;
    if (link->kind == LinkKind::PairInitiator && link->stage != LinkStage::Closing)
    {
        failPairing(session, "Máy kia trả lời không đúng giao thức ghép đôi.");
        return;
    }
    if (link->stage == LinkStage::Established)
        emit logMessage("⚠ " + peerDisplayName(link->peerId) + " gửi thông điệp không hợp lệ (" + what + ") - đóng phiên.");
    dropLink(session);
}

void ConnectSessionController::onNewIncomingConnection()
{
    while (m_server && m_server->hasPendingConnections())
    {
        QTcpSocket* sock = m_server->nextPendingConnection();
        if (!sock)
            break;
        if (!m_running)
        {
            sock->abort();
            sock->deleteLater();
            continue;
        }

        // Trần số kết nối đến CHƯA bắt tay xong. Đầy thì đóng kết nối cũ nhất (bắt tay thật chỉ mất vài
        // mili giây, nên cái nằm lâu nhất gần như chắc chắn là kết nối treo/phá) thay vì từ chối cái mới -
        // một máy trong LAN không thể chặn đường vào chỉ bằng cách giữ đủ số kết nối không làm gì.
        int pendingIncoming = 0;
        NetworkSession* oldest = nullptr;
        qint64 oldestMs = std::numeric_limits<qint64>::max();
        for (auto it = m_links.constBegin(); it != m_links.constEnd(); ++it)
        {
            const bool incoming = it->kind == LinkKind::Incoming || it->kind == LinkKind::PairResponder ||
                                  it->kind == LinkKind::SessionResponder;
            if (!incoming || it->stage == LinkStage::Established)
                continue;
            ++pendingIncoming;
            if (it->createdMs < oldestMs)
            {
                oldestMs = it->createdMs;
                oldest = it.key();
            }
        }
        if (pendingIncoming >= kMaxPendingIncoming && oldest)
            dropLink(oldest);

        auto* session = new NetworkSession(this);
        Link link;
        link.kind = LinkKind::Incoming;
        link.stage = LinkStage::AwaitPreamble;
        link.remoteAddress = sock->peerAddress();
        link.createdMs = link.lastRxMs = nowMs();
        m_links.insert(session, link);
        wireSession(session);
        session->adoptSocket(sock);
    }
}

void ConnectSessionController::onSessionConnected(NetworkSession* session)
{
    Link* link = linkFor(session);
    if (!link || link->stage != LinkStage::Connecting)
        return;

    const bool pairing = link->kind == LinkKind::PairInitiator;
    const QByteArray preamble = ConnectProtocol::buildPreamble(
        pairing ? ConnectProtocol::LinkPurpose::Pairing : ConnectProtocol::LinkPurpose::Session, m_identity.id);

    ProtocolMessage first;
    first.textA = m_identity.machineName;
    first.textB = m_identity.id;
    first.intA = ConnectProtocol::kVersion;
    first.intB = listenPort();
    if (pairing)
    {
        first.type = MessageType::PairRequest;
        link->stage = LinkStage::AwaitPairAccept;
    }
    else
    {
        link->nonceI = CryptoSession::randomBytes(ConnectProtocol::kNonceBytes);
        first.type = MessageType::SessionHello;
        first.nonce = link->nonceI;
        link->stage = LinkStage::AwaitHelloAck;
    }
    const QByteArray baseKey = link->baseKey;
    const bool nonceOk = pairing || first.nonce.size() == ConnectProtocol::kNonceBytes;

    session->setHandshakeKey(baseKey, preamble);
    if (!nonceOk || !session->sendPreamble(preamble) || !session->sendMessage(first))
    {
        if (pairing)
            failPairing(session, "Không gửi được yêu cầu ghép đôi tới máy kia.");
        else
            dropLink(session);
    }
}

void ConnectSessionController::onSessionEnded(NetworkSession* session, const QString& socketError)
{
    Link* link = linkFor(session);
    if (!link)
        return;

    if (link->kind == LinkKind::PairInitiator && link->stage == LinkStage::Connecting)
    {
        failPairing(session, socketError.isEmpty() ? QString("Không kết nối được tới máy kia.")
                                                   : "Không kết nối được tới máy kia: " + socketError);
        return;
    }
    if (link->kind == LinkKind::PairInitiator && link->stage == LinkStage::AwaitPairAccept)
    {
        // Máy kia không có cách nào trả lời "sai mã" một cách xác thực được (hai bên chưa có khóa chung) -
        // nó chỉ đóng kết nối. Đây là dấu hiệu thường gặp nhất của việc nhập sai mã.
        failPairing(session, "Máy kia đã đóng kết nối trước khi ghép đôi xong - sai mã ghép đôi, mã đã hết hạn/đã "
                             "được dùng, máy kia chưa bấm tạo mã, hoặc hai máy khác phiên bản ứng dụng.");
        return;
    }
    if (link->kind == LinkKind::SessionInitiator && link->stage == LinkStage::AwaitHelloAck)
    {
        // Nối TCP được mà bị đóng ngay ở bước đầu: máy kia không nhận khóa/không biết id của ta. Chỉ ghi
        // nhật ký ở lần thử đầu của mỗi đợt (các lần thử lại giãn dần sẽ im lặng).
        const ReconnectState st = m_reconnect.value(link->peerId);
        if (st.failures <= 1)
            emit logMessage("⚠ " + peerDisplayName(link->peerId) +
                            " từ chối bắt tay - có thể máy đó đã \"quên\" ghép đôi với máy này, hoặc hai máy khác phiên bản.");
    }
    dropLink(session);
}

void ConnectSessionController::onDecryptFailed(NetworkSession* session)
{
    Link* link = linkFor(session);
    if (!link)
        return;

    if (link->kind == LinkKind::PairResponder && link->stage == LinkStage::AwaitPairRequest)
    {
        dropLink(session);
        registerPairingFailure();
        return;
    }
    if (link->kind == LinkKind::PairInitiator && link->stage != LinkStage::Closing)
    {
        failPairing(session, "Không giải mã được phản hồi của máy kia - sai mã ghép đôi.");
        return;
    }
    if (link->stage == LinkStage::Established)
        emit logMessage("⚠ Dữ liệu từ " + peerDisplayName(link->peerId) +
                        " không xác thực được (bị sửa/phát lại/lệch thứ tự) - đóng phiên.");
    dropLink(session);
}

void ConnectSessionController::onPreamble(NetworkSession* session, const QByteArray& preamble)
{
    Link* link = linkFor(session);
    if (!link || link->stage != LinkStage::AwaitPreamble)
    {
        dropLink(session);
        return;
    }

    int version = 0;
    ConnectProtocol::LinkPurpose purpose = ConnectProtocol::LinkPurpose::Session;
    QString senderId;
    const bool parsed = ConnectProtocol::parsePreamble(preamble, &version, &purpose, &senderId);
    if (!parsed || version != ConnectProtocol::kVersion)
    {
        // Không đúng giao thức (bản 1 gửi thẳng gói mã hóa, không có preamble) hoặc khác phiên bản. Ghi
        // nhật ký một lần cho mỗi địa chỉ để người dùng biết vì sao máy kia "không kết nối được".
        const QString key = "addr:" + link->remoteAddress.toString();
        if (!m_warnedIncompatible.contains(key) && m_warnedIncompatible.size() < 256)
        {
            m_warnedIncompatible.insert(key);
            emit logMessage(QString("⚠ Từ chối kết nối từ %1: không cùng phiên bản giao thức Connect Together "
                                    "(máy này v%2) - hãy cập nhật One for ALL trên máy đó.")
                                .arg(link->remoteAddress.toString())
                                .arg(ConnectProtocol::kVersion));
        }
        dropLink(session);
        return;
    }

    if (purpose == ConnectProtocol::LinkPurpose::Pairing)
    {
        if (m_pairingKey.isEmpty() && m_pairingKeyJob != 0)
        {
            // Có mã đang mở nhưng K_code của nó còn đang được suy ở luồng nền (máy kia nhập mã rất nhanh,
            // hoặc là kịch bản tự động): giữ kết nối lại, tạm ngưng đọc (PairRequest theo ngay sau preamble
            // mà ta chưa có khóa để giải mã); onPairingKeyDerived() đặt khóa rồi cho đọc tiếp. Vẫn tính vào
            // trần kMaxPendingIncoming và bị cancelPairingSession() đóng như mọi kết nối PairResponder khác.
            link->kind = LinkKind::PairResponder;
            link->stage = LinkStage::AwaitPairingKey;
            link->preamble = preamble;
            session->setReceivePaused(true);
            return;
        }
        if (m_pairingKey.isEmpty())
        {
            dropLink(session); // không có phiên chờ ghép đôi nào đang mở - từ chối an toàn
            return;
        }
        link->kind = LinkKind::PairResponder;
        link->stage = LinkStage::AwaitPairRequest;
        link->baseKey = m_pairingKey;
        session->setHandshakeKey(m_pairingKey, preamble);
        return;
    }

    // Phiên: id trong preamble chỉ để CHỌN KHÓA (khóa dài hạn của đúng máy đó). Khai id của máy khác thì
    // không giải mã được gì ở bước sau; mọi quyết định có hậu quả (đăng ký phiên, đóng kết nối trùng...)
    // đều chờ tới khi nhận SessionConfirm hợp lệ.
    const PairedPeer* peer = senderId == m_identity.id ? nullptr : store().find(senderId);
    if (!peer || !peer->isValid())
    {
        dropLink(session); // máy lạ, hoặc máy ta đã "quên" - từ chối an toàn
        return;
    }
    link->kind = LinkKind::SessionResponder;
    link->stage = LinkStage::AwaitHello;
    link->peerId = senderId;
    link->baseKey = peer->longTermKey;
    link->remoteName = peer->machineName;
    session->setHandshakeKey(peer->longTermKey, preamble);
}

void ConnectSessionController::onMessage(NetworkSession* session, const ProtocolMessage& msg)
{
    Link* link = linkFor(session);
    if (!link)
        return;
    link->lastRxMs = nowMs();

    // Mỗi bước chỉ nhận ĐÚNG loại thông điệp của bước đó; mọi thứ khác là vi phạm giao thức -> đóng kết
    // nối. (Bản 1 nhận mọi loại ở mọi lúc: một máy đã ghép đôi có thể gửi PairAccept để ghi đè khóa của
    // máy khác, một kết nối chưa xác thực xong vẫn đẩy được clipboard...)
    switch (link->stage)
    {
        case LinkStage::AwaitPairRequest:
            if (msg.type == MessageType::PairRequest)
                handlePairRequest(session, msg);
            else
                protocolViolation(session, "chờ PairRequest");
            break;

        case LinkStage::AwaitPairAccept:
            if (msg.type == MessageType::PairAccept)
                handlePairAccept(session, msg);
            else if (msg.type == MessageType::PairReject)
            {
                const QString reason = sanitizeRemoteText(msg.textA, 200);
                failPairing(session, reason.isEmpty() ? QString("Máy kia từ chối ghép đôi.") : "Máy kia từ chối ghép đôi: " + reason);
            }
            else
                protocolViolation(session, "chờ PairAccept");
            break;

        case LinkStage::AwaitHello:
            if (msg.type == MessageType::SessionHello)
                handleSessionHello(session, msg);
            else
                protocolViolation(session, "chờ SessionHello");
            break;

        case LinkStage::AwaitHelloAck:
            if (msg.type == MessageType::SessionHelloAck)
                handleSessionHelloAck(session, msg);
            else
                protocolViolation(session, "chờ SessionHelloAck");
            break;

        case LinkStage::AwaitConfirm:
            if (msg.type == MessageType::SessionConfirm)
                handleSessionConfirm(session, msg);
            else
                protocolViolation(session, "chờ SessionConfirm");
            break;

        case LinkStage::Established:
        {
            const QString peerId = link->peerId;
            handleEstablishedMessage(session, peerId, msg);
            break;
        }

        case LinkStage::DerivingKey:
        case LinkStage::Connecting:
        case LinkStage::AwaitPreamble:
        case LinkStage::AwaitPairingKey:
        case LinkStage::Closing:
            break; // không chờ gì ở các bước này - bỏ qua
    }
}

// ------------------------------------------------------------------ Bắt tay phiên

void ConnectSessionController::handleSessionHello(NetworkSession* session, const ProtocolMessage& msg)
{
    Link* link = linkFor(session);
    if (!link)
        return;

    if (msg.intA != ConnectProtocol::kVersion || msg.nonce.size() != ConnectProtocol::kNonceBytes ||
        msg.textB != link->peerId)
    {
        protocolViolation(session, "SessionHello sai nội dung");
        return;
    }

    // nonceR MỚI cho từng kết nối: khóa phiên phụ thuộc nó, nên một SessionHello cũ bị phát lại không thể
    // dẫn tới một SessionConfirm hợp lệ (kẻ phát lại không có khóa dài hạn để tính khóa phiên mới).
    const QByteArray nonceR = CryptoSession::randomBytes(ConnectProtocol::kNonceBytes);
    QByteArray keyI2R, keyR2I;
    if (nonceR.size() != ConnectProtocol::kNonceBytes ||
        !CryptoSession::deriveSessionKeys(link->baseKey, msg.nonce, nonceR, link->peerId, m_identity.id, &keyI2R, &keyR2I))
    {
        dropLink(session);
        return;
    }

    link->nonceI = msg.nonce;
    link->nonceR = nonceR;
    link->remoteListenPort = isValidPort(msg.intB) ? static_cast<quint16>(msg.intB) : 0;
    link->remoteName = ConnectProtocol::sanitizeMachineName(msg.textA);
    link->stage = LinkStage::AwaitConfirm;

    ProtocolMessage ack;
    ack.type = MessageType::SessionHelloAck;
    ack.textA = m_identity.machineName;
    ack.textB = m_identity.id;
    ack.nonce = msg.nonce + nonceR; // lặp lại nonceI để bên kia biết đây là câu trả lời cho ĐÚNG lời chào của nó
    ack.intA = ConnectProtocol::kVersion;
    ack.intB = listenPort();
    if (!session->sendMessage(ack)) // vẫn bằng khóa dài hạn (giai đoạn bắt tay)
    {
        dropLink(session);
        return;
    }
    // Từ đây: ta gửi bằng K_r2i, nhận bằng K_i2r. Câu kế tiếp phải là SessionConfirm của bên kia.
    session->setSessionKeys(keyR2I, keyI2R);
}

void ConnectSessionController::handleSessionHelloAck(NetworkSession* session, const ProtocolMessage& msg)
{
    Link* link = linkFor(session);
    if (!link)
        return;

    if (msg.intA != ConnectProtocol::kVersion || msg.nonce.size() != 2 * ConnectProtocol::kNonceBytes ||
        msg.nonce.left(ConnectProtocol::kNonceBytes) != link->nonceI || msg.textB != link->peerId)
    {
        protocolViolation(session, "SessionHelloAck sai nội dung");
        return;
    }

    const QByteArray nonceR = msg.nonce.mid(ConnectProtocol::kNonceBytes);
    QByteArray keyI2R, keyR2I;
    if (!CryptoSession::deriveSessionKeys(link->baseKey, link->nonceI, nonceR, m_identity.id, link->peerId, &keyI2R, &keyR2I))
    {
        dropLink(session);
        return;
    }

    link->nonceR = nonceR;
    link->remoteName = ConnectProtocol::sanitizeMachineName(msg.textA);
    link->stage = LinkStage::AwaitConfirm;

    // Ta gửi bằng K_i2r, nhận bằng K_r2i. Gửi SessionConfirm trước; CHƯA đăng ký phiên - chờ SessionConfirm
    // của bên kia (bằng chứng nó cũng tính ra đúng khóa phiên từ nonceI mới của ta).
    session->setSessionKeys(keyI2R, keyR2I);
    ProtocolMessage confirm;
    confirm.type = MessageType::SessionConfirm;
    confirm.textA = m_identity.machineName;
    confirm.textB = m_identity.id;
    if (!session->sendMessage(confirm))
        dropLink(session);
}

bool ConnectSessionController::isPreferredDirection(const Link& link) const
{
    // Kết nối "ưu tiên" của một cặp máy = kết nối do máy có id NHỎ hơn mở. Cả hai máy tính ra cùng một
    // kết quả (so sánh chuỗi id ASCII), nên khi hai máy mở chéo cùng lúc, cả hai cùng giữ MỘT kết nối.
    if (link.kind == LinkKind::SessionInitiator)
        return m_identity.id < link.peerId;
    return link.peerId < m_identity.id;
}

bool ConnectSessionController::resolveDuplicateLinks(NetworkSession* session)
{
    const Link* self = linkFor(session);
    if (!self)
        return false;
    const QString peerId = self->peerId;
    const bool newPreferred = isPreferredDirection(*self);

    QList<NetworkSession*> toDrop;
    NetworkSession* toProbe = nullptr;
    bool refuseNew = false;
    for (auto it = m_links.constBegin(); it != m_links.constEnd(); ++it)
    {
        if (it.key() == session || it->peerId != peerId)
            continue;
        if (it->kind != LinkKind::SessionInitiator && it->kind != LinkKind::SessionResponder)
            continue;
        if (it->stage == LinkStage::Closing)
            continue;

        // Kết nối kia là kết nối ưu tiên, kết nối mới thì không, và kết nối kia đã thật sự nối được tới
        // máy đó (qua bước Connecting) -> giữ kết nối kia, bỏ kết nối mới. Mọi trường hợp còn lại: kết nối
        // vừa xác thực xong thắng (vd máy kia khởi động lại và nối lại theo cùng chiều).
        // Chỉ kết nối ĐÁNG TIN mới được quyền cản: phiên đã đăng ký, hoặc kết nối do chính ta mở. Một kết
        // nối ĐẾN còn đang bắt tay chưa chứng minh được gì - id của nó mới chỉ là lời tự khai trong preamble
        // (SessionHello cũ cũng phát lại được); nếu cho nó cản thì bất kỳ máy nào trong LAN, không cần khóa,
        // chỉ việc khai id của máy kia rồi nằm im là khiến ta vứt bỏ kết nối thật đã xác thực hai chiều.
        const bool otherTrusted = it->stage == LinkStage::Established || it->kind == LinkKind::SessionInitiator;
        if (otherTrusted && isPreferredDirection(it.value()) && !newPreferred && it->stage != LinkStage::Connecting)
        {
            refuseNew = true;
            if (it->stage == LinkStage::Established)
                toProbe = it.key();
        }
        else
        {
            toDrop << it.key();
        }
    }

    if (refuseNew)
    {
        // Máy kia vừa xác thực được một kết nối MỚI trong khi ta còn giữ phiên cũ với nó: hoặc là mở chéo
        // cùng lúc (phiên của ta vẫn sống - bình thường), hoặc máy kia đã khởi động lại và phiên của ta là
        // phiên chết chưa kịp phát hiện. Dò ngay: không trả lời trong kProbeTimeoutMs thì bỏ phiên cũ.
        if (toProbe)
        {
            if (Link* other = linkFor(toProbe))
            {
                if (other->probeSentMs == 0)
                    other->probeSentMs = qMax<qint64>(1, nowMs());
            }
            ProtocolMessage probe;
            probe.type = MessageType::Heartbeat;
            probe.flagA = true;
            toProbe->sendMessage(probe);
        }
        return false;
    }

    for (NetworkSession* s : std::as_const(toDrop))
        dropLink(s);
    return true;
}

void ConnectSessionController::handleSessionConfirm(NetworkSession* session, const ProtocolMessage& msg)
{
    Link* link = linkFor(session);
    if (!link)
        return;

    if (!session->hasSessionKeys() || msg.textB != link->peerId)
    {
        protocolViolation(session, "SessionConfirm sai nội dung");
        return;
    }

    // Tới đây máy kia ĐÃ được xác thực: nó vừa gửi một gói giải mã được bằng khóa phiên suy từ khóa dài
    // hạn + nonce mới của chính ta trong kết nối này.
    const QString peerId = link->peerId;
    const LinkKind kind = link->kind;
    if (!m_running || !store().find(peerId))
    {
        dropLink(session); // vừa bị "quên" (hoặc đang dừng) trong lúc bắt tay
        return;
    }

    if (!resolveDuplicateLinks(session))
    {
        dropLink(session);
        return;
    }

    if (kind == LinkKind::SessionResponder)
    {
        // Bên nhận kết nối xác nhận lại bằng khóa phiên của chiều mình - bên mở kết nối chỉ đăng ký phiên
        // khi nhận được câu này.
        ProtocolMessage confirm;
        confirm.type = MessageType::SessionConfirm;
        confirm.textA = m_identity.machineName;
        confirm.textB = m_identity.id;
        if (!session->sendMessage(confirm))
        {
            dropLink(session);
            return;
        }
    }

    link = linkFor(session); // resolveDuplicateLinks()/sendMessage() có thể đã làm đổi m_links
    if (!link)
        return;
    link->stage = LinkStage::Established;
    link->lastRxMs = nowMs();
    link->probeSentMs = 0;
    const QString address = session->peerAddress().toString();
    const quint16 listenPortOfPeer = link->remoteListenPort;
    const QString remoteName = link->remoteName;

    m_sessions.insert(peerId, session);
    m_reconnect.remove(peerId);
    // Chỉ BÂY GIỜ (đã có SessionConfirm hợp lệ, phiên đã đăng ký) mới nâng trần khung lên 2 MiB cho
    // clipboard. SessionConfirm của máy kia luôn đi TRƯỚC mọi thông điệp lớn của nó, nên không có khung hợp
    // lệ nào bị từ chối oan.
    session->markPeerAuthenticated();

    // Chỉ BÂY GIỜ mới ghi địa chỉ/cổng/tên vào PeerStore - sau khi máy kia chứng minh được danh tính. Gói
    // quảng bá UDP (không xác thực) không bao giờ được ghi đè các giá trị này.
    store().updateEndpoint(peerId, address, listenPortOfPeer, remoteName);

    emit peerConnectionChanged(peerId, true);
    emit logMessage("✓ Đã kết nối với " + peerDisplayName(peerId));
}

NetworkSession* ConnectSessionController::sessionFor(const QString& peerId) const
{
    if (peerId.isEmpty())
        return nullptr;
    const auto it = m_sessions.find(peerId);
    return it == m_sessions.end() ? nullptr : it.value();
}

bool ConnectSessionController::isPeerConnected(const QString& peerId) const
{
    return sessionFor(peerId) != nullptr;
}

NetworkSession* ConnectSessionController::pendingOutgoingTo(const QString& peerId) const
{
    for (auto it = m_links.constBegin(); it != m_links.constEnd(); ++it)
        if (it->kind == LinkKind::SessionInitiator && it->peerId == peerId && it->stage != LinkStage::Established &&
            it->stage != LinkStage::Closing)
            return it.key();
    return nullptr;
}

NetworkSession* ConnectSessionController::openSessionToPeer(const PairedPeer& peer, const QHostAddress& address, quint16 port)
{
    if (!m_running || !peer.isValid() || peer.id == m_identity.id || address.isNull() || port == 0)
        return nullptr;
    // Đã có phiên, hoặc đã có MỘT kết nối đi đang bắt tay tới máy này -> không mở thêm (bản 1 mở một kết
    // nối mới mỗi 2 giây khi kết nối đi không bao giờ được đăng ký).
    if (sessionFor(peer.id) || pendingOutgoingTo(peer.id))
        return nullptr;

    auto* session = new NetworkSession(this);
    Link link;
    link.kind = LinkKind::SessionInitiator;
    link.stage = LinkStage::Connecting;
    link.peerId = peer.id;
    link.baseKey = peer.longTermKey;
    link.remoteAddress = address;
    link.remoteListenPort = port;
    link.remoteName = peer.machineName;
    link.createdMs = link.lastRxMs = nowMs();
    m_links.insert(session, link);
    wireSession(session);
    session->connectToHost(address, port);
    return session;
}

void ConnectSessionController::attemptReconnect(const QString& peerId, const QHostAddress& hintAddress, quint16 hintPort,
                                                bool viaDiscovery)
{
    if (!m_running || peerId == m_identity.id)
        return;
    const PairedPeer* peer = store().find(peerId);
    if (!peer || !peer->autoConnect || !peer->isValid())
        return;
    if (sessionFor(peerId) || pendingOutgoingTo(peerId))
        return;

    const qint64 now = nowMs();
    ReconnectState& st = m_reconnect[peerId];
    if (st.attempted)
    {
        // Thử lại giãn dần (máy kia tắt/vắng mặt lâu thì không gõ cửa mỗi 5 giây mãi). Vừa thấy máy đó
        // quảng bá thì được thử sớm hơn, nhưng vẫn có khoảng cách tối thiểu - gói quảng bá không xác thực,
        // không thể để ai đó dùng nó ép ta mở kết nối liên tục.
        const qint64 backoff = qMin<qint64>(kReconnectMaxMs, qint64(kReconnectBaseMs) << qBound(0, st.failures - 1, 4));
        if (now - st.lastAttemptMs < (viaDiscovery ? qint64(kDiscoveryAttemptGapMs) : backoff))
            return;
    }

    // Địa chỉ từ gói quảng bá chỉ là GỢI Ý cho đúng lần thử này (không ghi vào PeerStore); không có gợi ý
    // thì dùng địa chỉ + cổng lắng nghe đã xác nhận ở lần bắt tay thành công gần nhất.
    QHostAddress address = hintAddress;
    quint16 port = hintPort;
    if (address.isNull() || port == 0)
    {
        address = QHostAddress(peer->lastAddress);
        port = peer->lastPort;
    }
    if (address.isNull() || port == 0)
        return;

    st.attempted = true;
    st.lastAttemptMs = now;
    ++st.failures;
    const PairedPeer peerCopy = *peer;
    openSessionToPeer(peerCopy, address, port);
}

void ConnectSessionController::reconnectNow()
{
    if (!m_running)
        return;
    const QList<PairedPeer> peers = store().peers();
    for (const PairedPeer& p : peers)
    {
        m_reconnect.remove(p.id);
        attemptReconnect(p.id, QHostAddress(), 0, false);
    }
}

// ------------------------------------------------------------------ Thông điệp trên phiên đã xác thực

void ConnectSessionController::handleEstablishedMessage(NetworkSession* session, const QString& peerId,
                                                        const ProtocolMessage& msg)
{
    switch (msg.type)
    {
        case MessageType::ControlHandoff:
        {
            if (msg.intA == static_cast<int>(ScreenSide::None))
            {
                // "Thôi điều khiển": chỉ có nghĩa khi đến từ ĐÚNG máy đang trong quan hệ điều khiển với ta -
                // máy đang điều khiển ta trả quyền, hoặc máy ta đang điều khiển từ chối/đuổi ta ra.
                if (m_activePeerId != peerId)
                    break;
                if (m_role == ControlRole::BeingControlled)
                    endBeingControlled(false);
                else if (m_role == ControlRole::Controlling)
                {
                    emit logMessage("ℹ " + peerDisplayName(peerId) + " không nhận/đã ngắt quyền điều khiển.");
                    deactivateControlling(false);
                }
                break;
            }

            if (msg.intA < static_cast<int>(ScreenSide::Left) || msg.intA > static_cast<int>(ScreenSide::Bottom))
                break; // hướng không hợp lệ - bỏ qua

            const bool sameController = m_role == ControlRole::BeingControlled && m_activePeerId == peerId;
            if (m_role != ControlRole::Idle && !sameController)
            {
                // Đang bận (đang điều khiển máy khác, hoặc đang bị máy khác điều khiển): từ chối rõ ràng để
                // máy kia tắt hook của nó, không nằm "điều khiển" một máy không nghe.
                ProtocolMessage refuse;
                refuse.type = MessageType::ControlHandoff;
                refuse.intA = static_cast<int>(ScreenSide::None);
                session->sendMessage(refuse);
                break;
            }

            const auto side = static_cast<ScreenSide>(msg.intA);
            const double frac = qBound(0, msg.intB, 1000) / 1000.0;
            const QPoint entry = EdgeDetector::entryPoint(localVirtualBounds(), side, frac);
            m_injector->moveAbsolute(entry.x(), entry.y());
            if (!sameController)
            {
                m_injectedKeysDown.clear();
                m_injectedButtonsDown.clear();
                setRole(ControlRole::BeingControlled, peerId);
                emit logMessage("🖱 " + peerDisplayName(peerId) +
                                " đang điều khiển máy này (Ctrl+Alt+Home trên máy nào cũng trả lại quyền điều khiển).");
            }
            break;
        }

        case MessageType::MouseMove:
        case MessageType::MouseButton:
        case MessageType::MouseWheel:
        case MessageType::KeyEvent:
            // Chỉ máy ĐANG điều khiển ta mới được tiêm input - không phải bất kỳ phiên nào giải mã được.
            if (m_role == ControlRole::BeingControlled && m_activePeerId == peerId)
                applyRemoteEvent(msg);
            break;

        case MessageType::ClipboardText:
            if (!m_enableClipboard)
                break;
            if (msg.textA.size() * qsizetype(sizeof(QChar)) > ConnectProtocol::kMaxClipboardBytes)
                break; // vượt trần đồng bộ - bỏ qua
            if (msg.textA != m_lastClipboardText)
            {
                m_applyingRemoteClipboard = true;
                m_lastClipboardText = msg.textA;
                QGuiApplication::clipboard()->setText(msg.textA);
                m_applyingRemoteClipboard = false;
            }
            break;

        case MessageType::Heartbeat:
            if (msg.flagA)
            {
                ProtocolMessage reply;
                reply.type = MessageType::Heartbeat;
                session->sendMessage(reply);
            }
            break;

        case MessageType::Goodbye:
            dropLink(session);
            break;

        default:
            // Thông điệp ghép đôi/bắt tay trên một phiên đã xác thực: không có lý do chính đáng nào.
            protocolViolation(session, "loại thông điệp không dành cho phiên đã kết nối");
            break;
    }
}

void ConnectSessionController::handlePeerSessionLost(const QString& peerId)
{
    if (m_activePeerId != peerId)
        return;

    if (m_role == ControlRole::Controlling)
    {
        // BẮT BUỘC tắt hook: bản 1 chỉ đổi role về Idle, hook vẫn nuốt mọi chuột/phím của máy này và
        // Ctrl+Alt+Home cũng vô tác dụng (chỉ xử lý khi role == Controlling) - người dùng bị khóa input.
        emit logMessage("⚠ Mất kết nối với " + peerDisplayName(peerId) + " trong lúc đang điều khiển.");
        deactivateControlling(false);
    }
    else if (m_role == ControlRole::BeingControlled)
    {
        endBeingControlled(false);
    }
}

void ConnectSessionController::applyRemoteEvent(const ProtocolMessage& msg)
{
    // Giá trị đến từ mạng (dù đã xác thực) vẫn được kẹp/kiểm trước khi đưa cho SendInput.
    switch (msg.type)
    {
        case MessageType::MouseMove:
            m_injector->moveRelative(qBound(-32767, msg.intA, 32767), qBound(-32767, msg.intB, 32767));
            break;
        case MessageType::MouseButton:
            if (msg.intA < 0 || msg.intA > 4)
                break;
            if (msg.flagA)
                m_injectedButtonsDown.insert(msg.intA);
            else
                m_injectedButtonsDown.remove(msg.intA);
            m_injector->mouseButton(msg.intA, msg.flagA);
            break;
        case MessageType::MouseWheel:
            m_injector->mouseWheel(qBound(-12000, msg.intB, 12000), qBound(-12000, msg.intA, 12000));
            break;
        case MessageType::KeyEvent:
            if (msg.intA < 1 || msg.intA > 254 || msg.intB < 0 || msg.intB > 0xFFFF)
                break;
            if (msg.flagA)
                m_injectedKeysDown.insert(msg.intA, KeyInfo{msg.intB, msg.flagB});
            else
                m_injectedKeysDown.remove(msg.intA);
            m_injector->keyEvent(msg.intA, msg.intB, msg.flagA, msg.flagB);
            break;
        default:
            break;
    }
}

void ConnectSessionController::releaseInjectedInput()
{
    // Máy kia thôi điều khiển (hoặc rớt kết nối) trong lúc còn phím/nút đang "giữ" do nó ra lệnh: tự nhả,
    // nếu không máy này kẹt Ctrl/Shift/nút chuột trái... tới khi người dùng bấm lại đúng phím đó.
    for (auto it = m_injectedKeysDown.constBegin(); it != m_injectedKeysDown.constEnd(); ++it)
        m_injector->keyEvent(it.key(), it->scanCode, false, it->extended);
    for (int button : std::as_const(m_injectedButtonsDown))
        m_injector->mouseButton(button, false);
    m_injectedKeysDown.clear();
    m_injectedButtonsDown.clear();
}

// ------------------------------------------------------------------ Biên màn hình & vai trò

QRect ConnectSessionController::localVirtualBounds() const
{
    // Cùng hệ tọa độ pixel vật lý với GetCursorPos/SetCursorPos (xem InputInjector::virtualScreenBounds()).
    const QRect bounds = InputInjector::virtualScreenBounds();
    if (!bounds.isEmpty())
        return bounds;

    // Dự phòng (không lấy được số đo hệ thống): gộp hình học các màn hình theo Qt.
    QRect fallback;
    for (QScreen* screen : QGuiApplication::screens())
        fallback = fallback.united(screen->geometry());
    return fallback;
}

void ConnectSessionController::enforceHookInvariant()
{
    if (!m_hook)
        return;

    // BẤT BIẾN: hook active <=> role == Controlling VÀ còn phiên tới đúng máy đang điều khiển. Mọi đường
    // đổi trạng thái đều tự giữ điều này; hàm này là lưới an toàn chạy mỗi 16 ms - nếu vì bất kỳ lý do gì
    // bất biến bị phá, người dùng lấy lại chuột/bàn phím trong vòng một nhịp thay vì bị khóa.
    if (m_role == ControlRole::Controlling && !sessionFor(m_activePeerId))
    {
        emit logMessage("⚠ Không còn phiên tới máy đang điều khiển - trả chuột/bàn phím về máy này.");
        deactivateControlling(false);
    }
    else if (m_role != ControlRole::Controlling && m_hook->isActive())
    {
        m_hook->setActive(false);
        emit logMessage("⚠ Hook đang chặn input ngoài lúc điều khiển - đã tắt.");
    }
}

void ConnectSessionController::onEdgeTimerTick()
{
    enforceHookInvariant();

    if (!m_running || m_role != ControlRole::Idle || !m_hook || !m_hook->isInstalled())
        return;

    QSet<ScreenSide> sidesWithNeighbor;
    QHash<int, QString> sideToPeerId;
    const QList<PairedPeer> peers = store().peers();
    for (const PairedPeer& p : peers)
    {
        if (p.side != ScreenSide::None && sessionFor(p.id))
        {
            sidesWithNeighbor.insert(p.side);
            sideToPeerId[static_cast<int>(p.side)] = p.id;
        }
    }
    if (sidesWithNeighbor.isEmpty())
    {
        m_edgeArmed = false; // khi có láng giềng trở lại, con trỏ phải được thấy RỜI biên trước đã
        return;
    }

    const QRect bounds = localVirtualBounds();
    const QPoint cursor = InputInjector::currentCursorPos();
    const ScreenSide crossed = EdgeDetector::detectCrossing(cursor, bounds, sidesWithNeighbor);
    if (crossed == ScreenSide::None)
    {
        m_edgeArmed = true;
        return;
    }

    // Chỉ trao quyền khi con trỏ ĐI TỚI biên: đã từng rời biên kể từ lần đổi vai trò gần nhất, và đã qua
    // khoảng nghỉ. Không có hai điều kiện này thì ngay sau Ctrl+Alt+Home (con trỏ vẫn nằm trên biên) nhịp
    // dò kế tiếp lại trao quyền đi luôn.
    if (!m_edgeArmed || nowMs() < m_edgeCooldownUntilMs)
        return;
    // Đang giữ nút chuột (kéo-thả, kéo thanh cuộn sát mép màn hình...): không chuyển máy giữa chừng.
    if (InputInjector::anyMouseButtonDown())
        return;

    const QString peerId = sideToPeerId.value(static_cast<int>(crossed));
    const double frac = EdgeDetector::normalizedPositionAlongEdge(cursor, bounds, crossed);
    activateControlling(peerId, crossed, cursor, frac);
}

bool ConnectSessionController::activateControlling(const QString& peerId, ScreenSide crossedSide, const QPoint& cursor,
                                                   double fraction)
{
    NetworkSession* session = sessionFor(peerId);
    if (!session || !m_hook || m_role != ControlRole::Idle || crossedSide == ScreenSide::None)
        return false;

    ProtocolMessage handoff;
    handoff.type = MessageType::ControlHandoff;
    handoff.intA = static_cast<int>(oppositeSide(crossedSide)); // máy kia đi vào từ phía đối diện
    handoff.intB = static_cast<int>(qBound(0.0, fraction, 1.0) * 1000);
    if (!session->sendMessage(handoff))
        return false; // không gửi được lệnh trao quyền thì KHÔNG bật hook
    if (sessionFor(peerId) != session)
        return false; // gửi lỗi đã làm rớt phiên ngay trong lời gọi trên

    m_forwardedKeysDown.clear();
    m_forwardedButtonsDown.clear();
    m_activeSide = crossedSide;
    m_activeCrossPoint = cursor;
    m_hook->setActive(true);
    setRole(ControlRole::Controlling, peerId);
    emit logMessage("🖱 Đang điều khiển " + activePeerName() + " (nhấn Ctrl+Alt+Home để quay lại)");
    return true;
}

void ConnectSessionController::deactivateControlling(bool notifyPeer)
{
    // Tắt hook TRƯỚC TIÊN, vô điều kiện - dù các bước sau có trục trặc gì thì chuột/bàn phím đã về máy này.
    if (m_hook)
        m_hook->setActive(false);
    if (m_role != ControlRole::Controlling)
        return;

    const QString peerId = m_activePeerId;
    const ScreenSide side = m_activeSide;
    const QPoint crossPoint = m_activeCrossPoint;
    const QHash<int, KeyInfo> keysDown = m_forwardedKeysDown;
    const QSet<int> buttonsDown = m_forwardedButtonsDown;

    // Đổi trạng thái xong xuôi rồi mới gửi (gửi lỗi có thể dẫn ngược về đây qua dropLink()).
    m_forwardedKeysDown.clear();
    m_forwardedButtonsDown.clear();
    m_activeSide = ScreenSide::None;
    m_edgeArmed = false;
    m_edgeCooldownUntilMs = nowMs() + kEdgeCooldownMs;
    setRole(ControlRole::Idle, QString());

    if (notifyPeer)
    {
        if (NetworkSession* session = sessionFor(peerId))
        {
            // Nhả mọi phím/nút mà máy kia đang thấy "giữ": người dùng vừa bấm Ctrl+Alt (+Home) - hai lần
            // NHẤN Ctrl, Alt đã được chuyển sang máy kia, còn lần NHẢ sẽ xảy ra sau khi hook đã tắt và
            // không bao giờ tới được đó. Không gửi nhả thì máy kia kẹt Ctrl+Alt.
            for (auto it = keysDown.constBegin(); it != keysDown.constEnd() && sessionFor(peerId) == session; ++it)
            {
                ProtocolMessage up;
                up.type = MessageType::KeyEvent;
                up.intA = it.key();
                up.intB = it->scanCode;
                up.flagA = false;
                up.flagB = it->extended;
                session->sendMessage(up);
            }
            for (int button : buttonsDown)
            {
                if (sessionFor(peerId) != session)
                    break;
                ProtocolMessage up;
                up.type = MessageType::MouseButton;
                up.intA = button;
                up.flagA = false;
                session->sendMessage(up);
            }
            if (sessionFor(peerId) == session)
            {
                ProtocolMessage back;
                back.type = MessageType::ControlHandoff;
                back.intA = static_cast<int>(ScreenSide::None);
                session->sendMessage(back);
            }
        }
    }

    // Suốt lúc điều khiển máy kia, con trỏ của máy này đứng yên đúng chỗ đã chạm biên. Lùi nó vào trong.
    if (side != ScreenSide::None)
    {
        const QPoint back = EdgeDetector::returnPoint(localVirtualBounds(), side, crossPoint, kReturnInsetPx);
        m_injector->moveAbsolute(back.x(), back.y());
    }
    emit logMessage("✓ Đã quay lại điều khiển máy này.");
}

void ConnectSessionController::endBeingControlled(bool notifyPeer)
{
    if (m_role != ControlRole::BeingControlled)
        return;

    const QString peerId = m_activePeerId;
    releaseInjectedInput();
    m_edgeArmed = false;
    m_edgeCooldownUntilMs = nowMs() + kEdgeCooldownMs;
    setRole(ControlRole::Idle, QString());

    if (notifyPeer)
    {
        if (NetworkSession* session = sessionFor(peerId))
        {
            ProtocolMessage refuse;
            refuse.type = MessageType::ControlHandoff;
            refuse.intA = static_cast<int>(ScreenSide::None);
            session->sendMessage(refuse);
        }
        emit logMessage("✓ Đã ngắt quyền điều khiển của " + peerDisplayName(peerId) + " đối với máy này.");
    }
    else
    {
        emit logMessage("✓ " + peerDisplayName(peerId) + " đã thôi điều khiển máy này.");
    }
}

void ConnectSessionController::onReturnHotkey()
{
    // Ctrl+Alt+Home LUÔN tắt hook, bất kể vai trò hiện tại là gì - đây là lối thoát cuối cùng của người
    // dùng, không được phụ thuộc vào việc sổ sách trạng thái có đang đúng hay không.
    if (m_hook)
        m_hook->setActive(false);

    if (m_role == ControlRole::Controlling)
        deactivateControlling(true);
    else if (m_role == ControlRole::BeingControlled)
        endBeingControlled(true); // người ngồi ở máy BỊ điều khiển cũng có quyền ngắt ngay
}

void ConnectSessionController::forwardInput(const ProtocolMessage& msg)
{
    if (m_role != ControlRole::Controlling)
        return;
    NetworkSession* session = sessionFor(m_activePeerId);
    if (!session)
        return; // enforceHookInvariant() ở nhịp dò biên kế tiếp sẽ tắt hook

    if (msg.type == MessageType::KeyEvent)
    {
        if (msg.flagA)
            m_forwardedKeysDown.insert(msg.intA, KeyInfo{msg.intB, msg.flagB});
        else
            m_forwardedKeysDown.remove(msg.intA);
    }
    else if (msg.type == MessageType::MouseButton)
    {
        if (msg.flagA)
            m_forwardedButtonsDown.insert(msg.intA);
        else
            m_forwardedButtonsDown.remove(msg.intA);
    }
    session->sendMessage(msg);
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
    return peerDisplayName(m_activePeerId);
}

bool ConnectSessionController::isHookActive() const
{
    return m_hook && m_hook->isActive();
}

int ConnectSessionController::pendingLinkCountForTesting() const
{
    int count = 0;
    for (auto it = m_links.constBegin(); it != m_links.constEnd(); ++it)
        if (it->stage != LinkStage::Established)
            ++count;
    return count;
}

bool ConnectSessionController::requestControlForTesting(const QString& peerId, ScreenSide localSide)
{
    if (!m_running)
        return false;
    const QRect bounds = localVirtualBounds();
    const QPoint cursor = EdgeDetector::entryPoint(bounds, localSide, 0.5);
    return activateControlling(peerId, localSide, cursor, 0.5);
}

// ------------------------------------------------------------------ Việc định kỳ

void ConnectSessionController::onHousekeepingTick()
{
    if (!m_running)
        return;
    const qint64 now = nowMs();

    // 1. Hạn bắt tay / hạn đóng / phiên chết.
    const QList<NetworkSession*> sessions = m_links.keys();
    for (NetworkSession* s : sessions)
    {
        Link* link = linkFor(s);
        if (!link)
            continue; // đã bị dọn bởi một bước xử lý trước đó trong chính vòng này

        if (link->stage == LinkStage::Established)
        {
            const bool silent = now - link->lastRxMs > kPeerSilentTimeoutMs;
            const bool probeAnswered = link->probeSentMs != 0 && link->lastRxMs >= link->probeSentMs;
            const bool probeExpired = link->probeSentMs != 0 && !probeAnswered && now - link->probeSentMs > kProbeTimeoutMs;
            if (probeAnswered)
                link->probeSentMs = 0;
            if (silent || probeExpired)
            {
                emit logMessage("⚠ " + peerDisplayName(link->peerId) + " không còn phản hồi - đóng phiên.");
                dropLink(s);
            }
            continue;
        }

        if (link->stage == LinkStage::DerivingKey)
            continue; // chưa mở kết nối - hạn bắt tay tính từ lúc có khóa (onOutgoingKeyDerived đặt lại mốc)

        const qint64 limit = link->stage == LinkStage::Closing ? qint64(kClosingGraceMs) : qint64(handshakeTimeoutMs());
        if (now - link->createdMs <= limit)
            continue;
        if (link->kind == LinkKind::PairInitiator && link->stage != LinkStage::Closing)
            failPairing(s, "Không nhận được phản hồi ghép đôi (hết thời gian chờ).");
        else
            dropLink(s);
    }

    // 2. Nhịp Heartbeat: vừa để máy kia biết ta còn sống, vừa để một phiên đã chết (rút dây mạng, máy kia
    //    mất điện - TCP không tự báo) bị phát hiện qua kPeerSilentTimeoutMs.
    if (now - m_lastHeartbeatMs >= kHeartbeatIntervalMs)
    {
        m_lastHeartbeatMs = now;
        const QList<NetworkSession*> established = m_sessions.values();
        for (NetworkSession* s : established)
        {
            const Link* link = linkFor(s);
            if (!link || link->stage != LinkStage::Established)
                continue;
            ProtocolMessage beat;
            beat.type = MessageType::Heartbeat;
            s->sendMessage(beat);
        }
    }

    // 3. Danh sách khám phá: bỏ máy không còn quảng bá.
    bool discoveredChanged = false;
    for (auto it = m_discovered.begin(); it != m_discovered.end();)
    {
        if (now - it->lastSeenMs > kDiscoveredExpiryMs)
        {
            it = m_discovered.erase(it);
            discoveredChanged = true;
        }
        else
        {
            ++it;
        }
    }
    if (discoveredChanged)
        emit discoveredPeersChanged();

    // 4. Kết nối lại tới máy đã ghép đôi đang chưa có phiên (attemptReconnect tự giãn cách từng máy).
    if (now - m_lastReconnectSweepMs >= kReconnectBaseMs)
    {
        m_lastReconnectSweepMs = now;
        const QList<PairedPeer> peers = store().peers();
        for (const PairedPeer& p : peers)
            attemptReconnect(p.id, QHostAddress(), 0, false);
    }
}

// ------------------------------------------------------------------ Clipboard

void ConnectSessionController::onClipboardChanged()
{
    if (m_applyingRemoteClipboard || !m_running)
        return;

    const QString text = QGuiApplication::clipboard()->text();
    if (text.isEmpty() || text == m_lastClipboardText)
        return;
    m_lastClipboardText = text;

    if (m_sessions.isEmpty())
        return;

    // Trần phía GỬI: bên nhận đóng kết nối nếu khung vượt trần của nó, nên văn bản quá lớn thì bỏ qua (ghi
    // nhật ký) thay vì gửi rồi làm rớt mọi phiên.
    if (text.size() * qsizetype(sizeof(QChar)) > ConnectProtocol::kMaxClipboardBytes)
    {
        emit logMessage(QString("ℹ Không đồng bộ clipboard: văn bản quá lớn (%1 KB, tối đa %2 KB).")
                            .arg(text.size() * qsizetype(sizeof(QChar)) / 1024)
                            .arg(ConnectProtocol::kMaxClipboardBytes / 1024));
        return;
    }

    ProtocolMessage msg;
    msg.type = MessageType::ClipboardText;
    msg.textA = text;
    const QList<NetworkSession*> established = m_sessions.values();
    for (NetworkSession* s : established)
    {
        const Link* link = linkFor(s);
        if (link && link->stage == LinkStage::Established)
            s->sendMessage(msg);
    }
}

// ------------------------------------------------------------------ Quản lý peer (API công khai)

void ConnectSessionController::onPeerAnnounced(const QString& id, const QString& name, const QHostAddress& addr,
                                               quint16 port, int version)
{
    if (!m_running || id == m_identity.id)
        return;

    const qint64 now = nowMs();
    const bool paired = store().find(id) != nullptr;
    const bool compatible = version == ConnectProtocol::kVersion;

    auto it = m_discovered.find(id);
    bool changed = false;
    if (it == m_discovered.end())
    {
        if (m_discovered.size() >= kMaxDiscovered)
        {
            // Danh sách đầy (bình thường không bao giờ xảy ra; đây là trần chống một máy trong LAN bơm vô số
            // id giả). Máy lạ thì bỏ qua; máy ĐÃ ghép đôi thì nhường chỗ bằng cách bỏ mục lạ cũ nhất.
            if (!paired)
                return;
            auto victim = m_discovered.end();
            for (auto d = m_discovered.begin(); d != m_discovered.end(); ++d)
            {
                if (store().find(d.key()))
                    continue;
                if (victim == m_discovered.end() || d->lastSeenMs < victim->lastSeenMs)
                    victim = d;
            }
            if (victim == m_discovered.end())
                return;
            m_discovered.erase(victim);
        }
        DiscoveredPeer d;
        d.id = id;
        it = m_discovered.insert(id, d);
        changed = true;
    }
    else
    {
        changed = it->machineName != name || it->address != addr || it->port != port || it->protocolVersion != version;
    }
    it->machineName = name;
    it->address = addr;
    it->port = port;
    it->protocolVersion = version;
    it->compatible = compatible;
    it->lastSeenMs = now;

    // Chỉ báo giao diện khi có gì đổi - mỗi máy quảng bá lại mỗi 2 giây, dựng lại cả bảng mỗi lần là thừa
    // (và làm mất dòng người dùng đang chọn).
    if (changed)
        emit discoveredPeersChanged();

    if (!compatible)
    {
        if (!m_warnedIncompatible.contains(id) && m_warnedIncompatible.size() < 256)
        {
            m_warnedIncompatible.insert(id);
            emit logMessage(QString("⚠ Máy \"%1\" dùng phiên bản giao thức Connect Together khác (v%2, máy này v%3) - "
                                    "không ghép đôi/kết nối được cho tới khi cả hai cùng phiên bản One for ALL.")
                                .arg(name)
                                .arg(version)
                                .arg(ConnectProtocol::kVersion));
        }
        return;
    }

    // Gói quảng bá KHÔNG được xác thực: không ghi địa chỉ/cổng của nó vào PeerStore (bản 1 ghi thẳng, nên
    // ai trong LAN cũng lái được kết nối của ta sang máy của họ). Chỉ dùng làm gợi ý cho một lần thử kết
    // nối - danh tính thật do bắt tay phiên quyết định.
    if (paired)
        attemptReconnect(id, addr, port, true);
}

QList<PairedPeer> ConnectSessionController::pairedPeers() const
{
    return store().peers();
}

void ConnectSessionController::setPeerSide(const QString& peerId, ScreenSide side)
{
    if (const PairedPeer* p = store().find(peerId))
    {
        PairedPeer updated = *p;
        updated.side = side;
        const bool saved = store().addOrUpdate(updated);
        emit pairedPeersChanged();
        reportStoreSaveResult(saved, "đổi vị trí của " + peerDisplayName(peerId));
    }
}

void ConnectSessionController::setPeerAutoConnect(const QString& peerId, bool autoConnect)
{
    if (const PairedPeer* p = store().find(peerId))
    {
        PairedPeer updated = *p;
        updated.autoConnect = autoConnect;
        const bool saved = store().addOrUpdate(updated);
        emit pairedPeersChanged();
        reportStoreSaveResult(saved, "đổi chế độ tự kết nối của " + peerDisplayName(peerId));
        if (autoConnect)
        {
            m_reconnect.remove(peerId);
            attemptReconnect(peerId, QHostAddress(), 0, false);
        }
    }
}

void ConnectSessionController::forgetPeer(const QString& peerId)
{
    // dropLink() tự lo: gỡ phiên, báo giao diện, và nếu đây là máy đang điều khiển/bị điều khiển thì trả
    // vai trò về Idle + tắt hook (bản 1 gỡ khỏi m_sessions trước nên nhánh đặt lại vai trò không chạy).
    dropLinksForPeer(peerId);
    m_reconnect.remove(peerId);
    const QString name = peerDisplayName(peerId); // lấy tên TRƯỚC khi gỡ khỏi kho
    const bool saved = store().remove(peerId);
    emit pairedPeersChanged();
    reportStoreSaveResult(saved, "quên máy " + name);
}

QList<DiscoveredPeer> ConnectSessionController::discoveredPeers() const
{
    QList<DiscoveredPeer> list = m_discovered.values();
    for (DiscoveredPeer& d : list)
        d.alreadyPaired = store().find(d.id) != nullptr;
    std::sort(list.begin(), list.end(), [](const DiscoveredPeer& a, const DiscoveredPeer& b) {
        const int byName = QString::compare(a.machineName, b.machineName, Qt::CaseInsensitive);
        return byName != 0 ? byName < 0 : a.id < b.id;
    });
    return list;
}
