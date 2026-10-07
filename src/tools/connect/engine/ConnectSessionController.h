#pragma once

#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QRect>
#include <QString>

#include "InputInjector.h"
#include "LocalIdentityStore.h"
#include "ProtocolMessage.h"
#include "../model/PairedPeer.h"

class InputHook;
class NetworkSession;
class PeerDiscovery;
class QTcpServer;
class QTcpSocket;
class QTimer;

enum class ControlRole
{
    Idle,            // không điều khiển ai, không bị ai điều khiển - chuột/phím hoạt động bình thường
    Controlling,      // đang điều khiển một máy khác (đã đưa chuột qua biên) - hook đang active
    BeingControlled   // một máy khác đang điều khiển máy này - nhận MouseMove/KeyEvent và tiêm vào
};

/// Một máy tìm thấy qua PeerDiscovery gần đây (chưa chắc đã ghép đôi).
struct DiscoveredPeer
{
    QString id;
    QString machineName;
    QHostAddress address;
    quint16 port{0};
    bool alreadyPaired{false};
};

/**
 * @brief Điều phối toàn bộ Connect Together: khám phá LAN, ghép đôi, phiên mạng mã hóa, phát hiện
 * chạm biên màn hình, bật/tắt hook input, tiêm input, đồng bộ clipboard văn bản.
 *
 * Đây là nơi DUY NHẤT quyết định khi nào InputHook chuyển sang active=true (chia sẻ điều khiển) -
 * luôn luôn là kết quả trực tiếp của việc EdgeDetector phát hiện chuột chạm biên có láng giềng đã
 * ghép đôi, tức là một hành động vật lý, hữu hình của người dùng. Không có đường nào khác kích hoạt.
 */
class ConnectSessionController : public QObject
{
    Q_OBJECT

public:
    explicit ConnectSessionController(QObject* parent = nullptr);
    ~ConnectSessionController() override;

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

    QList<DiscoveredPeer> discoveredPeers() const;

    ControlRole currentRole() const { return m_role; }
    QString activePeerName() const;

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
    void onNewIncomingConnection();
    void onPeerAnnounced(const QString& id, const QString& name, const QHostAddress& addr, quint16 port);
    void onPairingTimerTick();
    void onEdgeTimerTick();
    void onClipboardChanged();

    void wireSession(NetworkSession* session, const QString& tentativePeerId);
    void handleMessage(NetworkSession* session, const QString& sessionKey, const ProtocolMessage& msg);
    void sendPairAccept(NetworkSession* session, const QString& remoteMachineName, const QString& remoteId);
    void finalizePairing(const QString& peerId, const QString& machineName, const QHostAddress& addr, quint16 port,
                         const QByteArray& longTermKey);

    void activateControlling(const QString& peerId, ScreenSide enterSideOnPeer, double fraction);
    void deactivateControlling(bool notifyPeer);
    void applyRemoteEvent(const ProtocolMessage& msg);

    NetworkSession* sessionFor(const QString& peerId) const;
    NetworkSession* openSessionToPeer(const PairedPeer& peer);
    QRect localVirtualBounds() const;
    void setRole(ControlRole role, const QString& peerId);

    bool m_running{false};
    LocalIdentity m_identity;

    QTcpServer* m_server{nullptr};
    PeerDiscovery* m_discovery{nullptr};
    InputHook* m_hook{nullptr};
    InputInjector m_injector;
    QTimer* m_edgeTimer{nullptr};
    QTimer* m_pairingTimer{nullptr};

    QString m_pairingCode;     // mã 9 số đang mở chờ (rỗng = không mở)
    int m_pairingSecondsLeft{0};

    // Phiên đã xác thực bằng khóa dài hạn, khóa theo peerId
    QHash<QString, NetworkSession*> m_sessions;
    // Phiên TCP mới đến, chưa xác định là ai (đang thử khóa mã ghép đôi hoặc khóa peer đoán theo địa chỉ)
    QList<NetworkSession*> m_pendingSessions;

    QHash<QString, DiscoveredPeer> m_discovered; // id -> thông tin khám phá gần nhất

    ControlRole m_role{ControlRole::Idle};
    QString m_activePeerId;
    ScreenSide m_activeSide{ScreenSide::None}; // hướng đã chạm biên (khi Controlling) để gửi lại đúng hướng

    QString m_lastClipboardText;
    bool m_applyingRemoteClipboard{false};
};
