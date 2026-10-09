#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QRect>
#include <QSet>
#include <QString>
#include <atomic>
#include <functional>
#include <memory>

#include "InputInjector.h"
#include "LocalIdentityStore.h"
#include "ProtocolMessage.h"
#include "../model/PairedPeer.h"

class InputHook;
class NetworkSession;
class PeerDiscovery;
class PeerStore;
class QTcpServer;
class QTcpSocket;
class QTimer;

enum class ControlRole
{
    Idle,            // không điều khiển ai, không bị ai điều khiển - chuột/phím hoạt động bình thường
    Controlling,      // đang điều khiển một máy khác (đã đưa chuột qua biên) - hook đang active
    BeingControlled   // một máy khác đang điều khiển máy này - nhận MouseMove/KeyEvent và tiêm vào
};

/// Một máy tìm thấy qua PeerDiscovery gần đây (chưa chắc đã ghép đôi). Thông tin tự khai, CHƯA xác thực.
struct DiscoveredPeer
{
    QString id;
    QString machineName;
    QHostAddress address;
    quint16 port{0};
    bool alreadyPaired{false};
    int protocolVersion{0};
    bool compatible{true}; // false = máy đó dùng phiên bản giao thức khác, không ghép đôi/kết nối được
    qint64 lastSeenMs{0};
};

/**
 * @brief Điều phối toàn bộ Connect Together: khám phá LAN, ghép đôi, phiên mạng mã hóa, phát hiện
 * chạm biên màn hình, bật/tắt hook input, tiêm input, đồng bộ clipboard văn bản.
 *
 * Đây là nơi DUY NHẤT quyết định khi nào InputHook chuyển sang active=true (chia sẻ điều khiển) -
 * luôn luôn là kết quả trực tiếp của việc EdgeDetector phát hiện chuột chạm biên có láng giềng đã
 * ghép đôi, tức là một hành động vật lý, hữu hình của người dùng. Không có đường nào khác kích hoạt.
 * BẤT BIẾN: hook chỉ active khi role == Controlling VÀ còn phiên đã xác thực tới đúng máy đang điều
 * khiển. Mọi đường mất phiên đều tắt hook; bộ dò biên (16 ms) kiểm lại bất biến này như lưới an toàn; và
 * Ctrl+Alt+Home LUÔN tắt hook bất kể đang ở vai trò nào.
 *
 * GIAO THỨC (phiên bản 2 - ConnectProtocol::kVersion). "I" = bên mở kết nối TCP, "R" = bên nhận:
 *
 *   Ghép đôi (K_code = PBKDF2 từ mã 9 số, tính MỘT LẦN mỗi mã, ở LUỒNG NỀN - 100k vòng tốn 245-500 ms mà
 *   luồng của controller cũng là luồng phục vụ hook chuột/bàn phím cấp thấp; xem deriveKeyInBackground):
 *     I -> R  preamble{v, Pairing, idI}                    (không mã hóa)
 *     I -> R  PairRequest{tên, id, v, cổng nghe}           [K_code, nonce ngẫu nhiên, AAD chiều I]
 *     R -> I  PairAccept{tên, id, khóa dài hạn, cổng nghe} [K_code, nonce ngẫu nhiên, AAD chiều R]
 *     -> đóng kết nối. Giải mã thất bại ở R = một lần thử mã sai; quá PairingCode::kMaxAttempts lần thì
 *        hủy mã.
 *     Hai máy cùng nhập mã cho nhau (hai yêu cầu đi chéo): máy có id LỚN hơn hoãn PairAccept của mình tới
 *     khi lần ghép đôi đi của chính nó kết thúc - để cả hai kết thúc với CÙNG một khóa dài hạn.
 *
 *   Phiên (K = khóa dài hạn của cặp máy):
 *     I -> R  preamble{v, Session, idI}                    (không mã hóa - để R biết dùng khóa của ai)
 *     I -> R  SessionHello{idI, nonceI, cổng nghe}         [K, nonce ngẫu nhiên, AAD chiều I]
 *     R -> I  SessionHelloAck{idR, nonceI || nonceR}       [K, nonce ngẫu nhiên, AAD chiều R]
 *        hai bên suy K_i2r, K_r2i = HMAC-SHA256(K, nhãn chiều || nonceI || nonceR || idI || 0 || idR)
 *     I -> R  SessionConfirm{idI}                          [K_i2r, bộ đếm 0]
 *     R -> I  SessionConfirm{idR}                          [K_r2i, bộ đếm 0]
 *     Mỗi bên CHỈ đăng ký phiên (m_sessions) sau khi giải mã được SessionConfirm của bên kia - tức bên
 *     kia chứng minh được nó tính ra khóa phiên từ nonce MỚI của ta. Phát lại SessionHello/HelloAck cũ
 *     hay dội ngược gói của chính ta đều không tới được bước này. Sau đó mọi thông điệp dùng khóa phiên
 *     của chiều tương ứng + bộ đếm tăng dần.
 *     CẢ HAI đầu của cùng một kết nối TCP đều đăng ký phiên (bản 1 chỉ đầu nhận đăng ký, nên lệnh trao
 *     quyền điều khiển gửi qua đó luôn bị đầu kia bỏ qua).
 *     Hai máy mở kết nối chéo cùng lúc: giữ kết nối do máy có id NHỎ hơn mở, đóng kết nối còn lại.
 */
class ConnectSessionController : public QObject
{
    Q_OBJECT

public:
    explicit ConnectSessionController(QObject* parent = nullptr);
    ~ConnectSessionController() override;

    /// CHỈ dành cho bộ kiểm thử - gọi TRƯỚC start(). Cho phép chạy hai controller trong cùng một tiến
    /// trình (danh tính + kho peer riêng) và bảo đảm test KHÔNG đụng chuột/bàn phím/clipboard thật của
    /// người đang ngồi máy: không cài hook toàn cục, tiêm input qua một InputInjector giả.
    struct TestConfig
    {
        LocalIdentity identity;           // id rỗng = giữ danh tính lấy từ LocalIdentityStore
        PeerStore* store{nullptr};        // null = PeerStore::instance()
        InputInjector* injector{nullptr}; // null = InputInjector thật (SendInput/SetCursorPos)
        bool installGlobalHook{true};
        bool enableDiscovery{true};
        bool enableClipboardSync{true};
        bool usePreferredPort{true};
        int handshakeTimeoutMs{0};        // 0 = mặc định
        int housekeepingIntervalMs{0};    // 0 = mặc định
        int keyDerivationDelayMs{0};      // > 0: luồng nền ngủ thêm ngần này trước khi suy khóa (dựng kịch bản đua)
    };
    void configureForTesting(const TestConfig& config);

    bool start(QString* error = nullptr);
    void stop();
    bool isRunning() const { return m_running; }

    LocalIdentity localIdentity() const { return m_identity; }
    quint16 listenPort() const;

    // ---- Ghép đôi ----
    /// Sinh mã mới, mở phiên chờ ghép đôi (tự hủy sau PairingCode::kExpirySeconds). Trả về mã để hiển thị.
    QString beginPairingSession();
    void cancelPairingSession();
    bool isPairingSessionOpen() const { return !m_pairingCode.isEmpty(); }

    /// Chủ động ghép đôi vào một máy khác đã biết địa chỉ (nhập tay hoặc chọn từ danh sách khám phá).
    void connectWithCode(const QHostAddress& address, quint16 port, const QString& code);

    // ---- Quản lý peer ----
    QList<PairedPeer> pairedPeers() const;
    void setPeerSide(const QString& peerId, ScreenSide side);
    void setPeerAutoConnect(const QString& peerId, bool autoConnect);
    void forgetPeer(const QString& peerId);
    bool isPeerConnected(const QString& peerId) const;

    /// Thử kết nối lại NGAY tới mọi máy đã ghép đôi (bật tự kết nối) đang chưa có phiên, theo địa chỉ +
    /// cổng lắng nghe đã xác nhận ở lần bắt tay thành công gần nhất - không cần chờ gói quảng bá UDP.
    void reconnectNow();

    QList<DiscoveredPeer> discoveredPeers() const;

    ControlRole currentRole() const { return m_role; }
    QString activePeerName() const;

    // ---- CHỈ dành cho bộ kiểm thử ----
    bool isHookActive() const;
    InputHook* hookForTesting() const { return m_hook; }
    int pendingLinkCountForTesting() const;
    /// K_code của mã đang mở đã suy xong chưa (việc suy khóa chạy ở luồng nền, kết quả về qua vòng lặp sự kiện).
    bool isPairingKeyReadyForTesting() const { return !m_pairingKey.isEmpty(); }
    /// Tương đương việc bộ dò biên vừa thấy chuột chạm biên 'localSide' có máy peerId - đi đúng đường
    /// activateControlling() thật, chỉ bỏ qua khâu đọc vị trí chuột.
    bool requestControlForTesting(const QString& peerId, ScreenSide localSide);

signals:
    void pairingCodeGenerated(QString displayCode, int expirySeconds);
    void pairingSessionClosed();
    void pairingSucceeded(QString peerId, QString machineName);
    void pairingFailed(QString reason);
    void discoveredPeersChanged();
    void pairedPeersChanged();
    void roleChanged();
    void peerConnectionChanged(QString peerId, bool connected);
    void logMessage(QString text);

private:
    /// Cờ hủy của MỘT lần suy khóa ở luồng nền (mỗi lần một cờ mới - không dùng lại).
    using KeyCancelFlag = std::shared_ptr<std::atomic_bool>;
    struct KeyDeriveGuard;

    enum class LinkKind
    {
        Incoming,         // vừa accept(), chưa đọc preamble nên chưa biết là ghép đôi hay phiên
        PairInitiator,    // ta mở, để ghép đôi bằng mã người dùng vừa nhập
        PairResponder,    // máy khác mở, khai là ghép đôi (chỉ nhận khi đang mở mã)
        SessionInitiator, // ta mở tới một máy đã ghép đôi
        SessionResponder  // một máy khai id đã ghép đôi mở tới ta
    };
    enum class LinkStage
    {
        DerivingKey,      // PairInitiator: K_code đang được suy ở luồng nền, CHƯA mở kết nối TCP
        Connecting,       // chờ TCP nối xong
        AwaitPreamble,
        AwaitPairingKey,  // PairResponder: đã nhận preamble nhưng K_code của mã đang mở chưa suy xong (tạm ngưng đọc)
        AwaitPairRequest,
        AwaitPairAccept,
        AwaitHello,
        AwaitHelloAck,
        AwaitConfirm,
        Established,      // đã xác thực hai chiều, đang nằm trong m_sessions
        Closing           // đã gửi xong câu cuối, chờ bên kia nhận rồi đóng
    };
    /// Trạng thái của MỘT kết nối TCP (mọi NetworkSession đang sống đều có đúng một mục trong m_links).
    struct Link
    {
        LinkKind kind{LinkKind::Incoming};
        LinkStage stage{LinkStage::Connecting};
        QString peerId;          // phiên: id máy kia (do preamble khai, chỉ đáng tin sau Established)
        QByteArray baseKey;      // khóa bắt tay: K_code (ghép đôi) hoặc khóa dài hạn (phiên)
        quint64 keyJob{0};       // DerivingKey: số hiệu lần suy khóa đang chờ (kết quả mang số khác thì bỏ)
        KeyCancelFlag keyCancel; // DerivingKey: cờ hủy của lần suy khóa đó
        QByteArray preamble;     // AwaitPairingKey: preamble đã nhận, để đặt khóa bắt tay khi K_code suy xong
        QByteArray nonceI;
        QByteArray nonceR;
        QHostAddress remoteAddress;
        quint16 remoteListenPort{0}; // cổng LẮNG NGHE của máy kia (cổng ta nối tới / cổng nó tự khai)
        QString remoteName;
        qint64 createdMs{0};
        qint64 lastRxMs{0};
        qint64 probeSentMs{0};   // != 0: đang chờ trả lời một Heartbeat dò "còn sống không"
        bool pairRequestHeld{false};      // PairResponder: đã nhận PairRequest hợp lệ nhưng đang hoãn trả lời
        ProtocolMessage heldPairRequest;  // (hai máy cùng nhập mã cho nhau - xem handlePairRequest)
    };
    struct ReconnectState
    {
        qint64 lastAttemptMs{0};
        int failures{0};
        bool attempted{false};
    };

    void onNewIncomingConnection();
    void onPeerAnnounced(const QString& id, const QString& name, const QHostAddress& addr, quint16 port, int version);
    void onPairingTimerTick();
    void onEdgeTimerTick();
    void onHousekeepingTick();
    void onClipboardChanged();
    void onReturnHotkey();

    // ---- Vòng đời kết nối ----
    void wireSession(NetworkSession* session);
    Link* linkFor(NetworkSession* session);
    void dropLink(NetworkSession* session);
    void closeLinkGracefully(NetworkSession* session);
    void failPairing(NetworkSession* session, const QString& reason);
    void onSessionConnected(NetworkSession* session);
    void onSessionEnded(NetworkSession* session, const QString& socketError);
    void onDecryptFailed(NetworkSession* session);
    void onPreamble(NetworkSession* session, const QByteArray& preamble);
    void onMessage(NetworkSession* session, const ProtocolMessage& msg);
    void protocolViolation(NetworkSession* session, const QString& what);

    // ---- Ghép đôi ----
    void handlePairRequest(NetworkSession* session, const ProtocolMessage& msg);
    void handlePairAccept(NetworkSession* session, const ProtocolMessage& msg);
    bool outgoingPairingInFlight() const;
    void resumeHeldPairRequests();
    void registerPairingFailure();
    KeyCancelFlag deriveKeyInBackground(const QString& normalizedCode, std::function<void(const QByteArray&)> onDone);
    void abandonPairingKeyJob();
    void onPairingKeyDerived(quint64 job, const QByteArray& key);
    void onOutgoingKeyDerived(NetworkSession* session, quint64 job, const QByteArray& key);
    void finalizePairing(const QString& peerId, const QString& machineName, const QHostAddress& addr, quint16 port,
                         const QByteArray& longTermKey);

    // ---- Bắt tay phiên ----
    void handleSessionHello(NetworkSession* session, const ProtocolMessage& msg);
    void handleSessionHelloAck(NetworkSession* session, const ProtocolMessage& msg);
    void handleSessionConfirm(NetworkSession* session, const ProtocolMessage& msg);
    bool isPreferredDirection(const Link& link) const;
    bool resolveDuplicateLinks(NetworkSession* session);
    void handleEstablishedMessage(NetworkSession* session, const QString& peerId, const ProtocolMessage& msg);
    void handlePeerSessionLost(const QString& peerId);

    NetworkSession* sessionFor(const QString& peerId) const;
    NetworkSession* pendingOutgoingTo(const QString& peerId) const;
    NetworkSession* openSessionToPeer(const PairedPeer& peer, const QHostAddress& address, quint16 port);
    void attemptReconnect(const QString& peerId, const QHostAddress& hintAddress, quint16 hintPort, bool viaDiscovery);
    void dropLinksForPeer(const QString& peerId);

    // ---- Vai trò / input ----
    bool activateControlling(const QString& peerId, ScreenSide crossedSide, const QPoint& cursor, double fraction);
    void deactivateControlling(bool notifyPeer);
    void endBeingControlled(bool notifyPeer);
    void enforceHookInvariant();
    void forwardInput(const ProtocolMessage& msg);
    void applyRemoteEvent(const ProtocolMessage& msg);
    void releaseInjectedInput();
    QRect localVirtualBounds() const;
    void setRole(ControlRole role, const QString& peerId);
    QString peerDisplayName(const QString& peerId) const;
    void emitPairingFailedLater(const QString& reason);
    int handshakeTimeoutMs() const;

    PeerStore& store() const;
    void reportStoreSaveResult(bool saved, const QString& what);
    qint64 nowMs() const { return m_clock.elapsed(); }

    bool m_running{false};
    LocalIdentity m_identity;
    PeerStore* m_store{nullptr};        // null = PeerStore::instance() (chỉ khác null trong kiểm thử)
    InputInjector m_realInjector;
    InputInjector* m_injector{nullptr}; // trỏ tới m_realInjector, hoặc bản giả của bộ kiểm thử
    bool m_installHook{true};
    bool m_enableDiscovery{true};
    bool m_enableClipboard{true};
    bool m_usePreferredPort{true};
    int m_handshakeTimeoutMs{0};
    int m_housekeepingIntervalMs{0};
    int m_keyDerivationDelayMs{0};
    QElapsedTimer m_clock;

    QTcpServer* m_server{nullptr};
    PeerDiscovery* m_discovery{nullptr};
    InputHook* m_hook{nullptr};
    QTimer* m_edgeTimer{nullptr};
    QTimer* m_pairingTimer{nullptr};
    QTimer* m_houseTimer{nullptr};
    qint64 m_lastHeartbeatMs{0};
    qint64 m_lastReconnectSweepMs{0};

    QString m_pairingCode;     // mã 9 số đang mở chờ (rỗng = không mở)
    QByteArray m_pairingKey;   // K_code của mã đang mở - tính MỘT LẦN lúc sinh mã, không tính lại mỗi kết nối
                               // (rỗng trong lúc luồng nền còn đang suy - xem m_pairingKeyJob)
    quint64 m_pairingKeyJob{0};          // != 0: K_code của mã đang mở còn đang được suy ở luồng nền
    KeyCancelFlag m_pairingKeyCancel;    // cờ hủy của lần suy đó
    quint64 m_keyJobCounter{0};          // cấp số hiệu cho mọi lần suy khóa (ghép đôi đến lẫn đi)
    std::shared_ptr<KeyDeriveGuard> m_keyGuard; // cầu nối an toàn giữa luồng nền và controller (xem .cpp)
    int m_pairingSecondsLeft{0};
    int m_pairingFailures{0};  // số lần giải mã thất bại bằng K_code kể từ khi sinh mã

    // Mọi kết nối TCP đang sống (đang bắt tay, đã xác thực, hoặc đang đóng).
    QHash<NetworkSession*, Link> m_links;
    // Phiên đã xác thực HAI CHIỀU bằng khóa phiên, khóa theo peerId. Chỉ handleSessionConfirm() thêm vào.
    QHash<QString, NetworkSession*> m_sessions;
    QHash<QString, ReconnectState> m_reconnect;

    QHash<QString, DiscoveredPeer> m_discovered; // id -> thông tin khám phá gần nhất (có trần + hết hạn)
    QSet<QString> m_warnedIncompatible;          // đã ghi nhật ký "khác phiên bản" cho id này rồi

    ControlRole m_role{ControlRole::Idle};
    QString m_activePeerId;
    ScreenSide m_activeSide{ScreenSide::None}; // biên của MÁY NÀY đã chạm (khi Controlling) - để đặt lại con trỏ lúc quay về
    QPoint m_activeCrossPoint;                 // vị trí con trỏ lúc chạm biên
    bool m_edgeArmed{false};                   // con trỏ đã rời biên kể từ lần trao/nhận quyền gần nhất chưa
    qint64 m_edgeCooldownUntilMs{0};

    // Phím/nút đang "giữ" mà ta đã chuyển tiếp sang máy đang điều khiển (để gửi lệnh nhả khi trả quyền),
    // và phím/nút đang "giữ" mà ta đã tiêm vào máy này theo lệnh máy kia (để tự nhả khi nó thôi điều khiển
    // hoặc rớt kết nối giữa chừng).
    struct KeyInfo { int scanCode{0}; bool extended{false}; };
    QHash<int, KeyInfo> m_forwardedKeysDown;
    QSet<int> m_forwardedButtonsDown;
    QHash<int, KeyInfo> m_injectedKeysDown;
    QSet<int> m_injectedButtonsDown;

    QString m_lastClipboardText;
    bool m_applyingRemoteClipboard{false};
};
