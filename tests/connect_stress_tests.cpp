// Kiểm thử SỨC CHỊU ĐỰNG (stress + fuzz) cho Connect Together: lặp ghép đôi/ngắt/kết nối lại hàng trăm vòng,
// bắn hàng nghìn thông điệp hai chiều, bơm hàng chục nghìn khung rác/cắt cụt/độ dài bịa vào cổng lắng nghe
// ở mọi giai đoạn bắt tay, phát lại khung cũ, vượt trần kết nối chờ... rồi kiểm: không crash, không treo,
// không rò phiên/socket/timer, bộ nhớ không phình, hai đầu luôn kết thúc ở trạng thái nhất quán và kết nối
// hợp lệ vẫn hoạt động sau đó. Chỉ dùng TCP loopback - không cần mạng/phần cứng/máy thứ hai.
//
// AN TOÀN CHO MÁY CHẠY TEST (giống connect_tests.cpp): không bao giờ tiêm input thật (InputInjector giả),
// không cài hook toàn cục (installGlobalHook=false -> setActive(true) chỉ là một cờ trong bộ nhớ, không
// chặn gì), không đụng clipboard thật, không quảng bá ra LAN, mọi dữ liệu nằm trong thư mục tạm.
// Build: cmake --build build --target connect_stress_tests && build\connect_stress_tests.exe [--seed N] [--only M]
//   --seed N: chạy lại đúng chuỗi ngẫu nhiên của một lần chạy trước (mỗi lần chạy in seed ở dòng đầu)
//   --only M: chỉ chạy mục M (1..8) - để dò một lỗi cụ thể; bộ đầy đủ là chạy KHÔNG có tham số này
#include <QGuiApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHash>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>

#include "core/AppPaths.h"
#include "tools/connect/engine/ConnectSessionController.h"
#include "tools/connect/engine/CryptoSession.h"
#include "tools/connect/engine/InputHook.h"
#include "tools/connect/engine/InputInjector.h"
#include "tools/connect/engine/LocalIdentityStore.h"
#include "tools/connect/engine/NetworkSession.h"
#include "tools/connect/engine/PairingCode.h"
#include "tools/connect/engine/PeerStore.h"
#include "tools/connect/engine/ProtocolMessage.h"

/// Chạy vòng lặp sự kiện cho tới khi predicate() đúng hoặc hết thời gian chờ (xem connect_tests.cpp).
template <typename Func>
static bool waitUntil(Func predicate, int timeoutMs = 5000)
{
    if (predicate())
        return true;
    QEventLoop loop;
    QTimer giveUp;
    giveUp.setSingleShot(true);
    QObject::connect(&giveUp, &QTimer::timeout, &loop, &QEventLoop::quit);
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        if (predicate())
            loop.quit();
    });
    poll.start(2);
    giveUp.start(timeoutMs);
    loop.exec();
    return predicate();
}

/// Bơm vòng lặp sự kiện đúng 'ms' mili giây (cũng là lúc các đối tượng đã deleteLater() thật sự bị hủy).
static void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

static QRandomGenerator g_rng;
static int g_onlySection = 0; // --only N: chỉ chạy mục N (0 = chạy hết)
static bool wantSection(int n) { return g_onlySection == 0 || g_onlySection == n; }
static QElapsedTimer g_sectionClock;
static QString g_sectionName;
static qint64 g_lapStart = 0;

/// In thời gian + số kiểm tra của mục vừa xong, rồi mở mục mới (để báo cáo biết mục nào tốn bao lâu).
static void section(const char* name)
{
    if (!g_sectionName.isEmpty())
        std::printf("  [%s] %lld ms (passed=%d failed=%d)\n", qUtf8Printable(g_sectionName),
                    static_cast<long long>(g_sectionClock.elapsed()), g_pass, g_fail);
    std::fflush(stdout);
    g_sectionName = QString::fromUtf8(name);
    g_sectionClock.restart();
    g_lapStart = 0;
}

/// In thời gian của một tiểu mục (tính từ lần gọi lap()/section() trước).
static void lap(const char* name)
{
    const qint64 now = g_sectionClock.elapsed();
    std::printf("    . %s: %lld ms\n", name, static_cast<long long>(now - g_lapStart));
    std::fflush(stdout);
    g_lapStart = now;
}

static QByteArray randomBytes(int n)
{
    QByteArray out((n + 3) & ~3, Qt::Uninitialized);
    if (!out.isEmpty())
        g_rng.fillRange(reinterpret_cast<quint32*>(out.data()), out.size() / 4);
    out.truncate(n);
    return out;
}

/// Một số nguyên "khó chịu": các giá trị biên + ngẫu nhiên toàn dải 32-bit.
static int wildInt()
{
    static const int kEdges[] = {INT_MIN, INT_MIN + 1, -65536, -32769, -32768, -32767, -12001, -256, -1, 0, 1,
                                 4, 5, 254, 255, 256, 1000, 1001, 12001, 32767, 32768, 65535, 65536, INT_MAX - 1, INT_MAX};
    if (g_rng.bounded(3) == 0)
        return static_cast<int>(g_rng.generate());
    return kEdges[g_rng.bounded(static_cast<int>(sizeof(kEdges) / sizeof(kEdges[0])))];
}

static qint64 privateBytes()
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    std::memset(&pmc, 0, sizeof(pmc));
    pmc.cb = sizeof(pmc);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
        return -1;
    return static_cast<qint64>(pmc.PrivateUsage);
}

static int handleCount()
{
    DWORD n = 0;
    GetProcessHandleCount(GetCurrentProcess(), &n);
    return static_cast<int>(n);
}

/// InputInjector giả: chỉ GHI NHẬN + theo dõi phím/nút đang "giữ" + đếm giá trị lọt ra ngoài khoảng mà
/// ConnectSessionController cam kết kẹp trước khi đưa cho SendInput. Không bao giờ đụng chuột/phím thật.
struct FakeInjector : InputInjector
{
    QList<QPoint> relativeMoves;
    int absoluteMoves{0};
    int keyEvents{0};
    int buttonEvents{0};
    int wheelEvents{0};
    int outOfRange{0};
    QSet<int> heldKeys;
    QSet<int> heldButtons;
    QList<int> keyLog; // +vk = nhấn, -vk = nhả (đủ để kiểm thứ tự)

    void moveRelative(int dx, int dy) override
    {
        if (dx < -32767 || dx > 32767 || dy < -32767 || dy > 32767)
            ++outOfRange;
        relativeMoves << QPoint(dx, dy);
    }
    void moveAbsolute(int, int) override { ++absoluteMoves; }
    void mouseButton(int button, bool pressed) override
    {
        if (button < 0 || button > 4)
            ++outOfRange;
        ++buttonEvents;
        if (pressed)
            heldButtons.insert(button);
        else
            heldButtons.remove(button);
    }
    void mouseWheel(int dy, int dx) override
    {
        if (dx < -12000 || dx > 12000 || dy < -12000 || dy > 12000)
            ++outOfRange;
        ++wheelEvents;
    }
    void keyEvent(int vk, int scan, bool pressed, bool) override
    {
        if (vk < 1 || vk > 254 || scan < 0 || scan > 0xFFFF)
            ++outOfRange;
        ++keyEvents;
        keyLog << (pressed ? vk : -vk);
        if (pressed)
            heldKeys.insert(vk);
        else
            heldKeys.remove(vk);
    }
    void reset()
    {
        relativeMoves.clear();
        keyLog.clear();
        heldKeys.clear();
        heldButtons.clear();
        absoluteMoves = keyEvents = buttonEvents = wheelEvents = outOfRange = 0;
    }
};

static PairedPeer makePeer(const QString& id, const QString& name, const QByteArray& key, quint16 port, bool autoConnect)
{
    PairedPeer p;
    p.id = id;
    p.machineName = name;
    p.lastAddress = "127.0.0.1";
    p.lastPort = port;
    p.longTermKey = key;
    p.autoConnect = autoConnect;
    p.pairedAt = QDateTime::currentDateTime();
    return p;
}

// ---------------------------------------------------------------------------------------------------
// "Máy giả" nói giao thức v2 trực tiếp trên QTcpSocket THÔ - một bản cài đặt ĐỘC LẬP với NetworkSession
// (tự đóng khung, tự tính AAD/nonce bộ đếm), để vừa kiểm chéo giao thức, vừa chèn được khung sai/phát lại/
// lệch bộ đếm ở đúng từng bước bắt tay - điều NetworkSession không cho phép làm.
// ---------------------------------------------------------------------------------------------------
static QByteArray lengthPrefix(quint32 n)
{
    QByteArray out(4, '\0');
    qToBigEndian<quint32>(n, reinterpret_cast<uchar*>(out.data()));
    return out;
}

static QByteArray frameOf(const QByteArray& payload)
{
    return lengthPrefix(static_cast<quint32>(payload.size())) + payload;
}

static QByteArray counterNonce(quint64 counter)
{
    QByteArray nonce(CryptoSession::kNonceBytes, '\0');
    qToBigEndian<quint64>(counter, reinterpret_cast<uchar*>(nonce.data()) + 4);
    return nonce;
}

static QByteArray handshakeAad(bool fromInitiator, const QByteArray& preamble)
{
    return QByteArrayLiteral("OneForAll.ConnectTogether.v2.handshake/") + (fromInitiator ? "I/" : "R/") + preamble;
}

struct RawPeer
{
    QTcpSocket sock;
    QByteArray rx;
    QByteArray received; // mọi byte đã nhận - để dội ngược lại chính bên gửi
    QByteArray sent;     // mọi byte đã ghi ra - để phát lại nguyên văn từ một kết nối khác
    QByteArray preamble;
    QByteArray nonceI;
    QByteArray lastSessionFrame;
    QByteArray firstEncryptedFrame; // vai bên nhận: nguyên văn khung mã hóa đầu tiên của bên mở (để dội ngược)
    bool closed{false};
    bool asInitiator{true};         // false = socket do EvilServer accept(): ta đóng vai bên NHẬN kết nối
    int announcedPort{45678};       // "cổng lắng nghe" tự khai trong SessionHello/SessionHelloAck/PairAccept
    std::unique_ptr<CryptoSession> hs, txKey, rxKey;
    quint64 txCounter{0}, rxCounter{0};

    RawPeer()
    {
        QObject::connect(&sock, &QTcpSocket::readyRead, &sock, [this] {
            const QByteArray chunk = sock.readAll();
            rx += chunk;
            received += chunk;
        });
        QObject::connect(&sock, &QTcpSocket::disconnected, &sock, [this] { closed = true; });
        QObject::connect(&sock, &QTcpSocket::errorOccurred, &sock, [this](QAbstractSocket::SocketError) {
            if (sock.state() == QAbstractSocket::UnconnectedState)
                closed = true;
        });
    }

    bool isOpen() const { return sock.state() == QAbstractSocket::ConnectedState; }

    bool open(quint16 port, int timeoutMs = 8000)
    {
        sock.connectToHost(QHostAddress::LocalHost, port);
        waitUntil([this] { return isOpen() || closed; }, timeoutMs);
        return isOpen();
    }

    void write(const QByteArray& bytes)
    {
        if (!isOpen())
            return;
        sent += bytes;
        sock.write(bytes);
        sock.flush();
    }

    bool waitClosed(int timeoutMs = 5000)
    {
        return waitUntil([this] { return closed || sock.state() == QAbstractSocket::UnconnectedState; }, timeoutMs);
    }

    void sendPreamble(ConnectProtocol::LinkPurpose purpose, const QString& id)
    {
        preamble = ConnectProtocol::buildPreamble(purpose, id);
        write(frameOf(preamble));
    }

    QByteArray handshakePacketAs(const ProtocolMessage& m, bool senderIsInitiator) const
    {
        QByteArray packet;
        if (hs)
            hs->encrypt(m.toBytes(), &packet, nullptr, handshakeAad(senderIsInitiator, preamble));
        return packet;
    }
    QByteArray handshakePacket(const ProtocolMessage& m) const { return handshakePacketAs(m, asInitiator); }
    void sendHandshake(const ProtocolMessage& m) { write(frameOf(handshakePacket(m))); }

    QByteArray sessionPacket(const ProtocolMessage& m, quint64 counter) const
    {
        QByteArray packet;
        if (txKey)
            txKey->encryptWithNonce(counterNonce(counter), QByteArray(), m.toBytes(), &packet);
        return packet;
    }
    void sendSession(const ProtocolMessage& m)
    {
        lastSessionFrame = frameOf(sessionPacket(m, txCounter));
        ++txCounter;
        write(lastSessionFrame);
    }

    bool hasFrame() const
    {
        if (rx.size() < 4)
            return false;
        const quint32 n = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(rx.constData()));
        return rx.size() >= 4 + static_cast<qsizetype>(n);
    }
    bool takeFrame(QByteArray* payload, int timeoutMs = 5000)
    {
        waitUntil([this] { return hasFrame() || closed; }, timeoutMs);
        if (!hasFrame())
            return false;
        const quint32 n = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(rx.constData()));
        *payload = rx.mid(4, static_cast<qsizetype>(n));
        rx.remove(0, 4 + static_cast<qsizetype>(n));
        return true;
    }
    bool readHandshake(ProtocolMessage* m, int timeoutMs = 5000)
    {
        QByteArray packet, plain;
        return takeFrame(&packet, timeoutMs) && hs &&
               hs->decrypt(packet, &plain, nullptr, handshakeAad(!asInitiator, preamble)) && ProtocolMessage::fromBytes(plain, m);
    }
    bool readSession(ProtocolMessage* m, int timeoutMs = 5000)
    {
        QByteArray packet, plain;
        if (!takeFrame(&packet, timeoutMs) || !rxKey)
            return false;
        if (!rxKey->decryptWithNonce(counterNonce(rxCounter), QByteArray(), packet, &plain))
            return false;
        ++rxCounter;
        return ProtocolMessage::fromBytes(plain, m);
    }

    ProtocolMessage helloMessage(const QString& myId)
    {
        nonceI = CryptoSession::randomBytes(ConnectProtocol::kNonceBytes);
        ProtocolMessage hello;
        hello.type = MessageType::SessionHello;
        hello.textA = "May Gia";
        hello.textB = myId;
        hello.nonce = nonceI;
        hello.intA = ConnectProtocol::kVersion;
        hello.intB = announcedPort;
        return hello;
    }

    /// Bước 1+2 của bắt tay phiên: preamble + SessionHello, đọc SessionHelloAck, suy khóa phiên.
    bool helloExchange(const QString& myId, const QString& peerId, const QByteArray& key)
    {
        hs = std::make_unique<CryptoSession>(key);
        sendPreamble(ConnectProtocol::LinkPurpose::Session, myId);
        sendHandshake(helloMessage(myId));
        ProtocolMessage ack;
        if (!readHandshake(&ack) || ack.type != MessageType::SessionHelloAck ||
            ack.nonce.size() != 2 * ConnectProtocol::kNonceBytes || ack.nonce.left(ConnectProtocol::kNonceBytes) != nonceI ||
            ack.textB != peerId || ack.intA != ConnectProtocol::kVersion)
            return false;
        QByteArray i2r, r2i;
        if (!CryptoSession::deriveSessionKeys(key, nonceI, ack.nonce.mid(ConnectProtocol::kNonceBytes), myId, peerId, &i2r, &r2i))
            return false;
        txKey = std::make_unique<CryptoSession>(i2r);
        rxKey = std::make_unique<CryptoSession>(r2i);
        txCounter = rxCounter = 0;
        return true;
    }

    ProtocolMessage confirmMessage(const QString& myId) const
    {
        ProtocolMessage confirm;
        confirm.type = MessageType::SessionConfirm;
        confirm.textA = "May Gia";
        confirm.textB = myId;
        return confirm;
    }

    /// Bắt tay phiên ĐẦY ĐỦ với tư cách bên mở kết nối. true = controller đã xác nhận lại bằng khóa phiên.
    bool establish(quint16 port, const QString& myId, const QString& peerId, const QByteArray& key)
    {
        if (!open(port) || !helloExchange(myId, peerId, key))
            return false;
        sendSession(confirmMessage(myId));
        ProtocolMessage back;
        return readSession(&back) && back.type == MessageType::SessionConfirm && back.textB == peerId;
    }

    // ---- Vai bên NHẬN kết nối (controller là bên mở) ----

    /// Đọc preamble + khung mã hóa đầu tiên của controller, giải mã bằng 'key'. false nếu controller không
    /// nói đúng giao thức (đó là lỗi của controller, bộ test sẽ báo).
    bool acceptFirstMessage(ConnectProtocol::LinkPurpose expectedPurpose, const QString& initiatorId, const QByteArray& key,
                            ProtocolMessage* first)
    {
        QByteArray pre, packet, plain;
        if (!takeFrame(&pre))
            return false;
        preamble = pre;
        int version = 0;
        ConnectProtocol::LinkPurpose purpose = ConnectProtocol::LinkPurpose::Session;
        QString id;
        if (!ConnectProtocol::parsePreamble(pre, &version, &purpose, &id) || version != ConnectProtocol::kVersion ||
            purpose != expectedPurpose || id != initiatorId)
            return false;
        hs = std::make_unique<CryptoSession>(key);
        if (!takeFrame(&packet))
            return false;
        firstEncryptedFrame = frameOf(packet);
        return hs->decrypt(packet, &plain, nullptr, handshakeAad(true, preamble)) && ProtocolMessage::fromBytes(plain, first) &&
               first->textB == initiatorId && first->intA == ConnectProtocol::kVersion;
    }

    /// Nhận SessionHello của controller, suy khóa phiên và dựng SessionHelloAck ĐÚNG chuẩn (chưa gửi - nơi
    /// gọi tự quyết định gửi đúng hay bóp méo).
    bool acceptHello(const QString& myId, const QString& initiatorId, const QByteArray& key, ProtocolMessage* ack)
    {
        ProtocolMessage hello;
        if (!acceptFirstMessage(ConnectProtocol::LinkPurpose::Session, initiatorId, key, &hello) ||
            hello.type != MessageType::SessionHello || hello.nonce.size() != ConnectProtocol::kNonceBytes)
            return false;
        nonceI = hello.nonce;
        const QByteArray nonceR = CryptoSession::randomBytes(ConnectProtocol::kNonceBytes);
        QByteArray i2r, r2i;
        if (!CryptoSession::deriveSessionKeys(key, nonceI, nonceR, initiatorId, myId, &i2r, &r2i))
            return false;
        txKey = std::make_unique<CryptoSession>(r2i);
        rxKey = std::make_unique<CryptoSession>(i2r);
        txCounter = rxCounter = 0;
        ack->type = MessageType::SessionHelloAck;
        ack->textA = "May Xau";
        ack->textB = myId;
        ack->nonce = nonceI + nonceR;
        ack->intA = ConnectProtocol::kVersion;
        ack->intB = announcedPort;
        return true;
    }

    /// Bắt tay phiên ĐẦY ĐỦ với tư cách bên nhận kết nối.
    bool acceptSession(const QString& myId, const QString& initiatorId, const QByteArray& key)
    {
        ProtocolMessage ack, confirm;
        if (!acceptHello(myId, initiatorId, key, &ack))
            return false;
        sendHandshake(ack);
        if (!readSession(&confirm) || confirm.type != MessageType::SessionConfirm || confirm.textB != initiatorId)
            return false;
        sendSession(confirmMessage(myId));
        return true;
    }

    /// Gửi Heartbeat "trả lời ngay" rồi đọc cho tới khi nhận được một Heartbeat - mọi khung nhận được trên
    /// đường đi đều phải giải mã được theo đúng thứ tự bộ đếm (bằng chứng phiên còn sống + bộ đếm hai chiều
    /// không lệch). KHÔNG dùng làm mốc "A đã xử lý xong mọi thứ ta gửi": Heartbeat nhận được có thể là nhịp
    /// định kỳ 3 giây của A chứ không phải câu trả lời - muốn mốc chắc chắn thì chờ một trạng thái của A.
    bool probe(int timeoutMs = 5000)
    {
        ProtocolMessage ping;
        ping.type = MessageType::Heartbeat;
        ping.flagA = true;
        sendSession(ping);
        QElapsedTimer t;
        t.start();
        while (t.elapsed() < timeoutMs)
        {
            ProtocolMessage m;
            if (!readSession(&m, timeoutMs))
                return false;
            if (m.type == MessageType::Heartbeat && rx.isEmpty())
                return true;
        }
        return false;
    }
};

/// Cổng lắng nghe của một "máy xấu": mỗi kết nối controller mở tới được trao cho một RawPeer ở vai bên nhận.
class EvilServer : public QTcpServer
{
public:
    std::vector<std::unique_ptr<RawPeer>> accepted;

protected:
    void incomingConnection(qintptr handle) override
    {
        auto peer = std::make_unique<RawPeer>();
        peer->asInitiator = false;
        peer->announcedPort = serverPort();
        peer->sock.setSocketDescriptor(handle);
        accepted.push_back(std::move(peer));
    }
};

/// Một ConnectSessionController thật ở chế độ kiểm thử + kho peer riêng + injector giả + bộ đếm tín hiệu.
struct Node
{
    QString id, name, storePath;
    std::unique_ptr<PeerStore> store;
    FakeInjector injector;
    std::unique_ptr<ConnectSessionController> c;
    int handshakeTimeoutMs{2000};
    QHash<QString, int> ups, downs;
    QStringList pairFailed;
    int pairOk{0};
    int pairClosed{0};

    Node(const QString& id_, const QString& name_, const QString& storePath_)
        : id(id_), name(name_), storePath(storePath_)
    {
        store = std::make_unique<PeerStore>(storePath);
        create();
    }

    void create()
    {
        c = std::make_unique<ConnectSessionController>();
        ConnectSessionController::TestConfig cfg;
        cfg.identity.id = id;
        cfg.identity.machineName = name;
        cfg.store = store.get();
        cfg.injector = &injector;
        cfg.installGlobalHook = false;   // hook KHÔNG được cài -> setActive(true) chỉ là một cờ, không chặn gì
        cfg.enableDiscovery = false;     // không phát quảng bá ra LAN thật
        cfg.enableClipboardSync = false; // không đụng clipboard thật
        cfg.usePreferredPort = false;
        cfg.handshakeTimeoutMs = handshakeTimeoutMs;
        cfg.housekeepingIntervalMs = 50;
        c->configureForTesting(cfg);
        QObject::connect(c.get(), &ConnectSessionController::peerConnectionChanged, c.get(),
                         [this](QString peerId, bool on) { ++(on ? ups : downs)[peerId]; });
        QObject::connect(c.get(), &ConnectSessionController::pairingFailed, c.get(),
                         [this](QString reason) { pairFailed << reason; });
        QObject::connect(c.get(), &ConnectSessionController::pairingSucceeded, c.get(), [this](QString, QString) { ++pairOk; });
        QObject::connect(c.get(), &ConnectSessionController::pairingSessionClosed, c.get(), [this] { ++pairClosed; });
    }

    bool start()
    {
        QString err;
        return c->start(&err);
    }
    quint16 port() const { return c->listenPort(); }
    bool connectedTo(const Node& other) const { return c->isPeerConnected(other.id); }
    int pending() const { return c->pendingLinkCountForTesting(); }
    /// Số NetworkSession còn SỐNG (kể cả cái đã hẹn hủy nhưng chưa tới lượt) - để bắt rò phiên.
    int liveSessions() const { return static_cast<int>(c->findChildren<NetworkSession*>(Qt::FindDirectChildrenOnly).size()); }
    int liveTimers() const { return static_cast<int>(c->findChildren<QTimer*>(Qt::FindDirectChildrenOnly).size()); }
    int liveServers() const { return static_cast<int>(c->findChildren<QTcpServer*>(Qt::FindDirectChildrenOnly).size()); }
    int liveHooks() const { return static_cast<int>(c->findChildren<InputHook*>(Qt::FindDirectChildrenOnly).size()); }
    QByteArray keyFor(const Node& other) const
    {
        const PairedPeer* p = store->find(other.id);
        return p ? p->longTermKey : QByteArray();
    }
    /// Ghi lại cổng lắng nghe HIỆN TẠI của máy kia (thay cho gói quảng bá UDP đang tắt trong test).
    void pointAt(const Node& other) { store->updateEndpoint(other.id, "127.0.0.1", other.port(), QString()); }
};

static bool settled(const Node& x, const Node& y)
{
    return x.connectedTo(y) && y.connectedTo(x) && x.pending() == 0 && y.pending() == 0;
}

/// Ghép đôi "tắt" bằng cách ghi thẳng cùng một khóa vào hai kho (không tốn PBKDF2) - cho các mục không
/// thử bản thân việc ghép đôi.
static void forcePair(Node& x, Node& y)
{
    const QByteArray key = CryptoSession::generateRandomKey();
    x.store->addOrUpdate(makePeer(y.id, y.name, key, y.port(), true));
    y.store->addOrUpdate(makePeer(x.id, x.name, key, x.port(), true));
}

/// Bấm Ctrl+Alt+Home "vật lý" trên một máy (gọi thẳng onRaw* như callback hook thật sẽ gọi), rồi nhả.
static void pressReturnHotkey(InputHook* hook)
{
    if (!hook)
        return;
    hook->onRawKey(VK_LCONTROL, 29, true, false);
    hook->onRawKey(VK_LMENU, 56, true, false);
    hook->onRawKey(VK_HOME, 71, true, true);
    hook->onRawKey(VK_HOME, 71, false, true);
    hook->onRawKey(VK_LMENU, 56, false, false);
    hook->onRawKey(VK_LCONTROL, 29, false, false);
}

/// Mở 'count' kết nối thô cùng lúc tới 'port', gọi prep(peer, i) cho từng cái (prep trả true nếu controller
/// PHẢI tự đóng kết nối đó), chờ, rồi trả về số kết nối "phải bị đóng" mà controller KHÔNG đóng.
template <typename Prep>
static int fuzzBatch(quint16 port, int count, Prep prep, int closeTimeoutMs = 5000)
{
    std::vector<std::unique_ptr<RawPeer>> peers;
    std::vector<bool> expectClose(static_cast<size_t>(count), false);
    for (int i = 0; i < count; ++i)
    {
        peers.push_back(std::make_unique<RawPeer>());
        peers.back()->sock.connectToHost(QHostAddress::LocalHost, port);
    }
    waitUntil([&] {
        for (const auto& p : peers)
            if (!p->isOpen() && !p->closed)
                return false;
        return true;
    }, 8000);
    for (int i = 0; i < count; ++i)
        expectClose[static_cast<size_t>(i)] = prep(*peers[static_cast<size_t>(i)], i);
    waitUntil([&] {
        for (int i = 0; i < count; ++i)
            if (expectClose[static_cast<size_t>(i)] && !peers[static_cast<size_t>(i)]->closed)
                return false;
        return true;
    }, closeTimeoutMs);
    int notClosed = 0;
    for (int i = 0; i < count; ++i)
    {
        if (expectClose[static_cast<size_t>(i)] && !peers[static_cast<size_t>(i)]->closed)
            ++notClosed;
        peers[static_cast<size_t>(i)]->sock.abort();
    }
    return notClosed;
}

int main(int argc, char** argv)
{
    // QGuiApplication vì ConnectSessionController gọi QGuiApplication::clipboard() (xem connect_tests.cpp).
    QGuiApplication app(argc, argv);

    // TRƯỚC KHI đụng tới bất kỳ store nào: ép toàn bộ dữ liệu vào thư mục tạm.
    QTemporaryDir dataDir;
    CHECK(dataDir.isValid());
    AppPaths::setDataDirOverride(dataDir.path());

    quint32 seed = static_cast<quint32>(QDateTime::currentMSecsSinceEpoch() & 0x7FFFFFFF);
    for (int i = 1; i + 1 < argc; ++i)
    {
        if (std::strcmp(argv[i], "--seed") == 0)
            seed = static_cast<quint32>(std::strtoul(argv[i + 1], nullptr, 10));
        else if (std::strcmp(argv[i], "--only") == 0)
            g_onlySection = std::atoi(argv[i + 1]);
    }
    g_rng.seed(seed);
    std::printf("connect_stress_tests seed=%u (chay lai dung kich ban: --seed %u)\n", seed, seed);
    std::fflush(stdout);

    QElapsedTimer totalClock;
    totalClock.start();

    // ================================================================================================
    section("1. fuzz giai ma trong bo nho");
    // ================================================================================================
    if (wantSection(1))
    {
        // (a) ProtocolMessage::fromBytes với byte ngẫu nhiên: không crash, không nhận loại ngoài enum.
        int accepted = 0, badType = 0;
        for (int i = 0; i < 60000; ++i)
        {
            const QByteArray junk = randomBytes(static_cast<int>(g_rng.bounded(600)));
            ProtocolMessage m;
            if (ProtocolMessage::fromBytes(junk, &m))
            {
                ++accepted;
                if (static_cast<int>(m.type) < 1 || static_cast<int>(m.type) > 14)
                    ++badType;
            }
        }
        CHECK(badType == 0);
        CHECK(accepted < 600); // byte ngẫu nhiên gần như không bao giờ là một thông điệp hợp lệ

        // (b) Đột biến từ thông điệp HỢP LỆ của cả 14 loại: mọi kiểu cắt cụt đều bị từ chối; lật bit ngẫu
        //     nhiên không crash và không cho ra loại ngoài enum; thừa byte bị từ chối.
        int truncatedAccepted = 0, extraAccepted = 0, flippedBadType = 0, roundTripBroken = 0;
        for (int type = 1; type <= 14; ++type)
        {
            ProtocolMessage m;
            m.type = static_cast<MessageType>(type);
            m.textA = QString::fromUtf8("Tên máy thử nghiệm số ") + QString::number(type);
            m.textB = "peer-id-" + QString::number(type);
            m.longTermKey = randomBytes(32);
            m.nonce = randomBytes(32);
            m.intA = wildInt();
            m.intB = wildInt();
            m.flagA = type % 2 == 0;
            m.flagB = type % 3 == 0;
            const QByteArray good = m.toBytes();
            ProtocolMessage back;
            if (!ProtocolMessage::fromBytes(good, &back) || back.type != m.type || back.textA != m.textA ||
                back.textB != m.textB || back.longTermKey != m.longTermKey || back.nonce != m.nonce ||
                back.intA != m.intA || back.intB != m.intB || back.flagA != m.flagA || back.flagB != m.flagB)
                ++roundTripBroken;
            for (int len = 0; len < good.size(); ++len)
                if (ProtocolMessage::fromBytes(good.left(len), &back))
                    ++truncatedAccepted;
            if (ProtocolMessage::fromBytes(good + QByteArray(1, '\0'), &back))
                ++extraAccepted;
            for (int i = 0; i < 3000; ++i)
            {
                QByteArray mutated = good;
                const int flips = 1 + static_cast<int>(g_rng.bounded(4));
                for (int f = 0; f < flips; ++f)
                {
                    const int pos = static_cast<int>(g_rng.bounded(static_cast<int>(mutated.size())));
                    mutated[pos] = static_cast<char>(mutated.at(pos) ^ (1 << g_rng.bounded(8)));
                }
                if (ProtocolMessage::fromBytes(mutated, &back) &&
                    (static_cast<int>(back.type) < 1 || static_cast<int>(back.type) > 14))
                    ++flippedBadType;
            }
        }
        CHECK(roundTripBroken == 0);
        CHECK(truncatedAccepted == 0);
        CHECK(extraAccepted == 0);
        CHECK(flippedBadType == 0);

        // (c) Độ dài BỊA khổng lồ trong trường chuỗi/mảng byte (4 GB) với gần như không có dữ liệu theo
        //     sau: phải từ chối NGAY, không cấp phát theo con số bịa.
        const qint64 memBefore = privateBytes();
        QElapsedTimer t;
        t.start();
        int hostileAccepted = 0;
        static const quint32 kHostileLengths[] = {0xFFFFFFFEu, 0xFFFFFFF0u, 0x80000000u, 0x7FFFFFFFu, 0x7FFFFFFEu,
                                                  0x40000000u, 0x10000000u, 0x00FFFFFFu};
        for (int round = 0; round < 400; ++round)
        {
            for (quint32 len : kHostileLengths)
            {
                for (int field = 0; field < 4; ++field)
                {
                    // type | textA | textB | longTermKey | nonce : đặt độ dài bịa vào trường thứ 'field',
                    // các trường trước nó rỗng hợp lệ (độ dài 0).
                    QByteArray data(1, static_cast<char>(1 + round % 14));
                    for (int f = 0; f < field; ++f)
                        data += lengthPrefix(0);
                    data += lengthPrefix(len);
                    data += randomBytes(static_cast<int>(g_rng.bounded(40)));
                    ProtocolMessage m;
                    if (ProtocolMessage::fromBytes(data, &m))
                        ++hostileAccepted;
                }
            }
        }
        CHECK(hostileAccepted == 0);
        CHECK(t.elapsed() < 20000);
        const qint64 memAfter = privateBytes();
        CHECK(memBefore > 0 && memAfter > 0 && memAfter - memBefore < 64ll * 1024 * 1024);

        // (d) parsePreamble: byte ngẫu nhiên + đột biến từ preamble hợp lệ. Hễ báo "đọc được, đúng phiên
        //     bản" thì id trả về PHẢI hợp lệ (id này dùng để tra khóa + đưa lên nhật ký).
        int preambleBadId = 0, preambleOk = 0;
        const QByteArray goodPre = ConnectProtocol::buildPreamble(ConnectProtocol::LinkPurpose::Session, "abc-123_x.y");
        for (int i = 0; i < 60000; ++i)
        {
            QByteArray data;
            if (i % 2 == 0)
                data = randomBytes(static_cast<int>(g_rng.bounded(300)));
            else
            {
                data = goodPre;
                const int pos = static_cast<int>(g_rng.bounded(static_cast<int>(data.size())));
                data[pos] = static_cast<char>(g_rng.bounded(256));
                if (g_rng.bounded(4) == 0)
                    data.truncate(static_cast<int>(g_rng.bounded(static_cast<int>(data.size()) + 1)));
                if (g_rng.bounded(4) == 0)
                    data += randomBytes(static_cast<int>(g_rng.bounded(300)));
            }
            int version = -1;
            ConnectProtocol::LinkPurpose purpose = ConnectProtocol::LinkPurpose::Session;
            QString id;
            if (ConnectProtocol::parsePreamble(data, &version, &purpose, &id) && version == ConnectProtocol::kVersion)
            {
                ++preambleOk;
                if (!ConnectProtocol::isValidPeerId(id) ||
                    (purpose != ConnectProtocol::LinkPurpose::Pairing && purpose != ConnectProtocol::LinkPurpose::Session))
                    ++preambleBadId;
            }
        }
        CHECK(preambleBadId == 0);
        CHECK(preambleOk > 0); // có đột biến vẫn hợp lệ (đổi một ký tự trong id) - nhánh "ok" thật sự được chạy
        // id dài quá 255 byte: buildPreamble cắt, không tràn byte độ dài
        const QByteArray longPre = ConnectProtocol::buildPreamble(ConnectProtocol::LinkPurpose::Pairing, QString(5000, 'a'));
        CHECK(longPre.size() <= NetworkSession::kMaxPreambleBytes + 16);
        int v = 0;
        ConnectProtocol::LinkPurpose pp = ConnectProtocol::LinkPurpose::Session;
        QString pid;
        CHECK(!ConnectProtocol::parsePreamble(longPre, &v, &pp, &pid)); // 255 ký tự > kMaxIdChars -> id không hợp lệ

        // (e) CryptoSession: gói ngẫu nhiên không bao giờ giải mã được, ở cả hai kiểu nonce.
        const CryptoSession crypto(CryptoSession::generateRandomKey());
        CHECK(crypto.isValid());
        int forged = 0;
        for (int i = 0; i < 20000; ++i)
        {
            QByteArray plain;
            const QByteArray packet = randomBytes(static_cast<int>(g_rng.bounded(200)));
            if (crypto.decrypt(packet, &plain, nullptr, i % 2 ? QByteArray("aad") : QByteArray()))
                ++forged;
            if (crypto.decryptWithNonce(counterNonce(static_cast<quint64>(i)), QByteArray(), packet, &plain))
                ++forged;
        }
        CHECK(forged == 0);
        // Khóa sai kích thước: không hợp lệ, mọi thao tác trả false chứ không crash
        for (int size : {0, 1, 16, 31, 33, 64})
        {
            const CryptoSession bad(randomBytes(size));
            QByteArray out;
            CHECK(!bad.isValid());
            CHECK(!bad.encrypt("x", &out));
            CHECK(!bad.decrypt(randomBytes(64), &out));
        }
        // deriveSessionKeys với đầu vào thiếu: false
        QByteArray k1, k2;
        CHECK(!CryptoSession::deriveSessionKeys(randomBytes(31), randomBytes(16), randomBytes(16), "a", "b", &k1, &k2));
        CHECK(!CryptoSession::deriveSessionKeys(randomBytes(32), QByteArray(), randomBytes(16), "a", "b", &k1, &k2));
        CHECK(!CryptoSession::deriveSessionKeys(randomBytes(32), randomBytes(16), randomBytes(16), "", "b", &k1, &k2));

        // (f) DPAPI: blob rác không mở được (rỗng), blob thật mở lại đúng, sửa 1 byte thì không mở được.
        int unprotectedJunk = 0;
        for (int i = 0; i < 2000; ++i)
            if (!CryptoSession::unprotectForCurrentUser(randomBytes(1 + static_cast<int>(g_rng.bounded(400)))).isEmpty())
                ++unprotectedJunk;
        CHECK(unprotectedJunk == 0);
        int dpapiBroken = 0, dpapiWrongValue = 0, dpapiRejected = 0;
        for (int i = 0; i < 300; ++i)
        {
            const QByteArray secret = randomBytes(32);
            QByteArray blob = CryptoSession::protectForCurrentUser(secret);
            if (blob.isEmpty() || CryptoSession::unprotectForCurrentUser(blob) != secret)
                ++dpapiBroken;
            if (blob.isEmpty())
                continue;
            // Sửa 1 byte: DPAPI hoặc từ chối (rỗng), hoặc - nếu byte đó nằm ở phần đầu không tham gia kiểm
            // toàn vẹn (vd chuỗi mô tả) - vẫn trả đúng dữ liệu gốc. Không bao giờ được ra một giá trị KHÁC.
            const int pos = static_cast<int>(g_rng.bounded(static_cast<int>(blob.size())));
            blob[pos] = static_cast<char>(blob.at(pos) ^ 0x5A);
            const QByteArray reopened = CryptoSession::unprotectForCurrentUser(blob);
            if (reopened.isEmpty())
                ++dpapiRejected;
            else if (reopened != secret)
                ++dpapiWrongValue;
        }
        CHECK(dpapiBroken == 0);
        CHECK(dpapiWrongValue == 0);
        CHECK(dpapiRejected > 0);
    }

    // ================================================================================================
    section("2. PeerStore / LocalIdentityStore voi tep hong");
    // ================================================================================================
    if (wantSection(2))
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString path = tmp.filePath("peers_fuzz.json");
        auto writeFile = [&](const QByteArray& content) {
            QFile f(path);
            if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                f.write(content);
        };

        // Một tệp hợp lệ làm gốc để đột biến.
        {
            QFile::remove(path);
            PeerStore seedStore(path);
            for (int i = 0; i < 6; ++i)
                seedStore.addOrUpdate(makePeer(QString("peer-%1").arg(i), QString("May %1").arg(i), randomBytes(32),
                                               static_cast<quint16>(40000 + i), i % 2 == 0));
        }
        QFile original(path);
        CHECK(original.open(QIODevice::ReadOnly));
        const QByteArray goodJson = original.readAll();
        original.close();
        CHECK(goodJson.size() > 200);

        // Tệp rác / cắt cụt / sửa byte: nạp không crash, mọi peer nạp được đều hợp lệ, và kho vẫn dùng tiếp
        // được (thêm một peer mới rồi nạp lại thấy đúng peer đó).
        int invalidLoaded = 0, unusableAfter = 0;
        for (int i = 0; i < 250; ++i)
        {
            QByteArray content;
            switch (i % 5)
            {
                case 0: content = randomBytes(static_cast<int>(g_rng.bounded(2000))); break;
                case 1: content = goodJson.left(static_cast<int>(g_rng.bounded(static_cast<int>(goodJson.size())))); break;
                case 2:
                    content = goodJson;
                    for (int f = 0; f < 1 + static_cast<int>(g_rng.bounded(20)); ++f)
                        content[static_cast<int>(g_rng.bounded(static_cast<int>(content.size())))] =
                            static_cast<char>(g_rng.bounded(256));
                    break;
                case 3: content = goodJson + goodJson; break;
                default: content = QByteArray(); break;
            }
            writeFile(content);
            PeerStore s(path);
            for (const PairedPeer& p : s.peers())
                if (!p.isValid() || !ConnectProtocol::isValidPeerId(p.id))
                    ++invalidLoaded;
            const QByteArray key = randomBytes(32);
            s.addOrUpdate(makePeer("moi-them", "May Moi", key, 41000, true));
            PeerStore again(path);
            const PairedPeer* found = again.find("moi-them");
            if (!found || found->longTermKey != key)
                ++unusableAfter;
        }
        CHECK(invalidLoaded == 0);
        CHECK(unusableAfter == 0);

        // JSON đúng cú pháp nhưng sai kiểu/giá trị từng trường: chỉ đúng một mục hợp lệ được nạp.
        {
            const QString goodKey = QString::fromLatin1(CryptoSession::protectForCurrentUser(randomBytes(32)).toBase64());
            const QString shortKey = QString::fromLatin1(CryptoSession::protectForCurrentUser(randomBytes(31)).toBase64());
            QJsonArray arr;
            auto entry = [&](const QJsonValue& id, const QJsonValue& key, const QJsonValue& port, const QJsonValue& side) {
                QJsonObject o;
                o["id"] = id;
                o["machineName"] = QString("<b>Ten</b>\n\tla");
                o["lastAddress"] = "khong-phai-ip";
                o["lastPort"] = port;
                o["longTermKeyProtected"] = key;
                o["side"] = side;
                arr.append(o);
            };
            entry("hop-le", goodKey, 70000, 99);           // cổng + hướng ngoài khoảng -> 0 / None, vẫn nạp
            entry("hop-le", goodKey, 1, 1);                // trùng id -> bỏ
            entry("khoa-ngan", shortKey, 1, 1);            // khóa 31 byte -> bỏ
            entry("khoa-rac", "@@@@", 1, 1);               // Base64 rác -> bỏ
            entry("id co khoang trang", goodKey, 1, 1);    // id sai định dạng -> bỏ
            entry(12345, goodKey, 1, 1);                   // id không phải chuỗi -> bỏ
            entry(QString(200, 'x'), goodKey, 1, 1);       // id quá dài -> bỏ
            entry("khoa-la-so", 42, "abc", "xyz");         // sai kiểu mọi trường -> bỏ
            arr.append(QJsonValue(7));                     // phần tử không phải object
            arr.append(QJsonValue());
            QJsonObject root;
            root["version"] = 2;
            root["peers"] = arr;
            writeFile(QJsonDocument(root).toJson());
            PeerStore s(path);
            CHECK(s.peers().size() == 1);
            const PairedPeer* p = s.find("hop-le");
            CHECK(p != nullptr);
            if (p)
            {
                CHECK(p->lastPort == 0 && p->side == ScreenSide::None);
                CHECK(!p->machineName.contains('<') && !p->machineName.contains('\n'));
            }
            // "peers" không phải mảng / gốc không phải object
            writeFile("{\"peers\": {\"a\": 1}}");
            CHECK(PeerStore(path).peers().isEmpty());
            writeFile("[1,2,3]");
            CHECK(PeerStore(path).peers().isEmpty());
        }

        // Lưu/nạp lặp lại với nhiều peer: không mất, không lệch khóa (DPAPI bọc/mở mỗi lần).
        {
            QFile::remove(path);
            QHash<QString, QByteArray> expected;
            int mismatches = 0;
            for (int round = 0; round < 25; ++round)
            {
                PeerStore s(path);
                for (auto it = expected.constBegin(); it != expected.constEnd(); ++it)
                {
                    const PairedPeer* p = s.find(it.key());
                    if (!p || p->longTermKey != it.value())
                        ++mismatches;
                }
                if (s.peers().size() != expected.size())
                    ++mismatches;
                for (int i = 0; i < 3; ++i)
                {
                    const QString id = QString("vong%1-may%2").arg(round).arg(i);
                    const QByteArray key = randomBytes(32);
                    s.addOrUpdate(makePeer(id, "May " + id, key, static_cast<quint16>(1 + g_rng.bounded(65535)), true));
                    expected.insert(id, key);
                }
                if (round % 5 == 4 && !expected.isEmpty())
                {
                    const QString victim = expected.constBegin().key();
                    s.remove(victim);
                    expected.remove(victim);
                }
            }
            CHECK(mismatches == 0);
            // Không tệp tạm nào của QSaveFile bị bỏ lại
            CHECK(QDir(tmp.path()).entryList(QDir::Files).size() == 1);
        }

        // LocalIdentityStore: tệp danh tính hỏng -> sinh lại danh tính HỢP LỆ, ghi lại được, lần sau đọc ra y nguyên.
        {
            const QString idPath = tmp.filePath("identity_fuzz.json");
            int badIdentity = 0, unstable = 0, notPersisted = 0;
            for (int i = 0; i < 60; ++i)
            {
                QFile f(idPath);
                if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
                {
                    switch (i % 4)
                    {
                        case 0: f.write(randomBytes(static_cast<int>(g_rng.bounded(500)))); break;
                        case 1: f.write("{\"id\": \"co khoang trang\", \"machineName\": 5}"); break;
                        case 2: f.write("{\"id\": 123}"); break;
                        default: break; // tệp rỗng
                    }
                    f.close();
                }
                LocalIdentityStore::instance().setFilePath(idPath);
                const LocalIdentity first = LocalIdentityStore::instance().identity();
                if (!ConnectProtocol::isValidPeerId(first.id) || first.machineName.isEmpty())
                    ++badIdentity;
                // Danh tính vừa sinh lại phải nằm THẬT trên đĩa (tệp hỏng cũ bị ghi đè) - nếu không, mỗi lần
                // chạy ứng dụng ra một id mới và mọi máy đã ghép đôi coi máy này là máy lạ.
                QFile check(idPath);
                if (!check.open(QIODevice::ReadOnly) ||
                    QJsonDocument::fromJson(check.readAll()).object().value("id").toString() != first.id)
                    ++notPersisted;
                check.close();
                LocalIdentityStore::instance().setFilePath(idPath);
                if (LocalIdentityStore::instance().identity().id != first.id)
                    ++unstable;
            }
            CHECK(badIdentity == 0);
            CHECK(notPersisted == 0);
            CHECK(unstable == 0);
        }
    }

    // ================================================================================================
    section("3. NetworkSession: ban don dap hai chieu + khung sat tran");
    // ================================================================================================
    if (wantSection(3))
    {
        QTcpServer server;
        CHECK(server.listen(QHostAddress::LocalHost));
        NetworkSession ini, res;
        bool adopted = false;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [&] {
            if (!adopted && server.hasPendingConnections())
            {
                res.adoptSocket(server.nextPendingConnection(), false);
                adopted = true;
            }
        });
        ini.connectToHost(QHostAddress::LocalHost, server.serverPort());
        CHECK(waitUntil([&] { return adopted && ini.isConnected(); }));
        const QByteArray keyI2R = CryptoSession::generateRandomKey();
        const QByteArray keyR2I = CryptoSession::generateRandomKey();
        ini.setSessionKeys(keyI2R, keyR2I);
        res.setSessionKeys(keyR2I, keyI2R);

        // Mỗi đầu ghi lại: số thứ tự kế tiếp mong đợi, số lần lệch thứ tự, số lần sai nội dung.
        struct Sink
        {
            int next{0};
            int outOfOrder{0};
            int corrupt{0};
            int errors{0};
            int decryptFailures{0};
        } atIni, atRes;
        auto expectedText = [](int seq, int chars) {
            QString text(chars, QChar(u'a' + seq % 26));
            const QString head = QString::number(seq) + ":";
            text.replace(0, head.size(), head);
            return text;
        };
        auto wire = [&](NetworkSession& s, Sink& sink) {
            QObject::connect(&s, &NetworkSession::messageReceived, &s, [&sink, expectedText](ProtocolMessage m) {
                if (m.intA != sink.next)
                    ++sink.outOfOrder;
                if (m.type == MessageType::ClipboardText && m.textA != expectedText(m.intA, m.intB))
                    ++sink.corrupt;
                if (m.type == MessageType::MouseMove && m.intB != -m.intA)
                    ++sink.corrupt;
                sink.next = m.intA + 1;
            });
            QObject::connect(&s, &NetworkSession::errorOccurred, &s, [&sink](const QString&) { ++sink.errors; });
            QObject::connect(&s, &NetworkSession::decryptFailed, &s, [&sink] { ++sink.decryptFailures; });
        };
        wire(ini, atIni);
        wire(res, atRes);

        int seqI = 0, seqR = 0, sendFailures = 0; // số thứ tự do bên mở kết nối / bên nhận kết nối GỬI đi
        auto sendSmall = [&](NetworkSession& s, int& seq) {
            ProtocolMessage m;
            m.type = MessageType::MouseMove;
            m.intA = seq;
            m.intB = -seq;
            if (s.sendMessage(m))
                ++seq;
            else
                ++sendFailures;
        };
        auto sendBig = [&](NetworkSession& s, int& seq, int chars) {
            ProtocolMessage m;
            m.type = MessageType::ClipboardText;
            m.intA = seq;
            m.intB = chars;
            m.textA = expectedText(seq, chars);
            if (s.sendMessage(m))
                ++seq;
            else
                ++sendFailures;
        };

        // (a) 5000 thông điệp nhỏ MỖI chiều, gửi xen kẽ trong MỘT hơi (không bơm vòng lặp sự kiện ở giữa).
        for (int i = 0; i < 5000; ++i)
        {
            sendSmall(ini, seqI);
            sendSmall(res, seqR);
        }
        CHECK(sendFailures == 0);
        CHECK(waitUntil([&] { return atRes.next == seqI && atIni.next == seqR; }, 30000));
        CHECK(atRes.outOfOrder == 0 && atIni.outOfOrder == 0 && atRes.corrupt == 0 && atIni.corrupt == 0);

        // (b) Văn bản clipboard cỡ SÁT TRẦN (đúng kMaxClipboardBytes byte UTF-16) hai chiều, xen thông điệp
        //     nhỏ. Bơm vòng lặp sự kiện giữa các khung lớn - như ứng dụng thật (mỗi lần đổi clipboard là một
        //     sự kiện riêng) - để hàng đợi ghi không chạm ngưỡng "bên kia không nhận" (8 MB).
        const int maxChars = ConnectProtocol::kMaxClipboardBytes / static_cast<int>(sizeof(QChar));
        for (int i = 0; i < 30; ++i)
        {
            sendBig(ini, seqI, maxChars - (i % 3));
            sendBig(res, seqR, maxChars - (i % 2));
            for (int k = 0; k < 20; ++k)
            {
                sendSmall(ini, seqI);
                sendSmall(res, seqR);
            }
            CHECK(waitUntil([&] { return atRes.next == seqI && atIni.next == seqR; }, 30000));
        }
        CHECK(sendFailures == 0);
        CHECK(atRes.outOfOrder == 0 && atIni.outOfOrder == 0 && atRes.corrupt == 0 && atIni.corrupt == 0);

        // (c) Biên chính xác của trần khung đã xác thực: đúng bằng trần -> đi được; hơn 1 byte -> bên GỬI từ
        //     chối (false), KHÔNG đóng kết nối và KHÔNG tiêu tốn bộ đếm (các thông điệp sau vẫn giải mã được).
        {
            ProtocolMessage probeMsg;
            probeMsg.type = MessageType::ClipboardText;
            probeMsg.textA = "x";
            const qint64 base = probeMsg.toBytes().size() - 2; // phần cố định khi nonce rỗng (null)
            const qint64 room = NetworkSession::kAuthenticatedMaxFrameBytes - CryptoSession::kTagBytes - base;
            // 'room' byte còn lại chia cho văn bản (2 byte/ký tự); lẻ thì bù 1 byte bằng trường nonce
            // (QByteArray null và QByteArray 1 byte chênh nhau đúng 1 byte khi tuần tự hóa).
            const int chars = static_cast<int>((room - (room % 2 ? 1 : 0)) / 2);
            ProtocolMessage exact;
            exact.type = MessageType::ClipboardText;
            exact.intA = seqI;
            exact.intB = chars;
            exact.textA = expectedText(seqI, chars);
            if (room % 2)
                exact.nonce = QByteArray(1, 'n');
            CHECK(exact.toBytes().size() + CryptoSession::kTagBytes == NetworkSession::kAuthenticatedMaxFrameBytes);
            // Khung này vượt trần clipboard nhưng đúng trần KHUNG - NetworkSession phải chuyển được nguyên vẹn.
            CHECK(ini.sendMessage(exact));
            ++seqI;
            CHECK(waitUntil([&] { return atRes.next == seqI; }, 30000));

            ProtocolMessage over = exact;
            over.intA = seqI;
            over.nonce = exact.nonce + QByteArray(1, 'n');
            QString err;
            CHECK(!ini.sendMessage(over, &err));
            CHECK(!err.isEmpty());
            CHECK(ini.isConnected() && res.isConnected());
            for (int i = 0; i < 200; ++i)
            {
                sendSmall(ini, seqI);
                sendSmall(res, seqR);
            }
            CHECK(waitUntil([&] { return atRes.next == seqI && atIni.next == seqR; }, 30000));
        }
        CHECK(sendFailures == 0);
        CHECK(atRes.outOfOrder == 0 && atIni.outOfOrder == 0 && atRes.corrupt == 0 && atIni.corrupt == 0);
        CHECK(atRes.errors == 0 && atIni.errors == 0 && atRes.decryptFailures == 0 && atIni.decryptFailures == 0);
        CHECK(ini.isConnected() && res.isConnected());
        std::printf("  [NetworkSession] da chuyen %d + %d thong diep, khong mat/khong lech thu tu.\n", seqI, seqR);

        // (d) Giai đoạn CHƯA xác thực: trần 4 KB - đúng trần đi được, hơn 1 byte bị bên gửi từ chối.
        {
            QTcpServer server2;
            CHECK(server2.listen(QHostAddress::LocalHost));
            NetworkSession a, b;
            bool adopted2 = false;
            QObject::connect(&server2, &QTcpServer::newConnection, &server2, [&] {
                if (!adopted2 && server2.hasPendingConnections())
                {
                    b.adoptSocket(server2.nextPendingConnection(), false);
                    adopted2 = true;
                }
            });
            a.connectToHost(QHostAddress::LocalHost, server2.serverPort());
            CHECK(waitUntil([&] { return adopted2 && a.isConnected(); }));
            const QByteArray key = CryptoSession::generateRandomKey();
            a.setHandshakeKey(key, "ctx");
            b.setHandshakeKey(key, "ctx");
            QList<ProtocolMessage> got;
            QObject::connect(&b, &NetworkSession::messageReceived, &b, [&](ProtocolMessage m) { got << m; });

            ProtocolMessage small;
            small.type = MessageType::PairReject;
            small.textA = "x";
            const qint64 base = small.toBytes().size() - 2;
            const qint64 room = NetworkSession::kUnauthenticatedMaxFrameBytes - CryptoSession::kTagBytes -
                                CryptoSession::kNonceBytes - base;
            ProtocolMessage exact;
            exact.type = MessageType::PairReject;
            exact.textA = QString(static_cast<int>((room - (room % 2 ? 1 : 0)) / 2), QChar('r'));
            if (room % 2)
                exact.nonce = QByteArray(1, 'n');
            CHECK(a.sendMessage(exact));
            CHECK(waitUntil([&] { return got.size() == 1; }));
            ProtocolMessage over = exact;
            over.nonce = exact.nonce + QByteArray(1, 'n');
            CHECK(!a.sendMessage(over));
            CHECK(a.isConnected());
            CHECK(a.sendMessage(small));
            CHECK(waitUntil([&] { return got.size() == 2; }));
        }

        // (e) Bên kia NGỪNG ĐỌC: hàng đợi ghi không được phình vô hạn - vượt kMaxPendingWriteBytes thì
        //     sendMessage() trả false, báo lỗi và đóng kết nối.
        {
            QTcpServer server3;
            CHECK(server3.listen(QHostAddress::LocalHost));
            NetworkSession sender;
            bool adopted3 = false;
            int errors = 0;
            QObject::connect(&server3, &QTcpServer::newConnection, &server3, [&] {
                if (!adopted3 && server3.hasPendingConnections())
                {
                    sender.adoptSocket(server3.nextPendingConnection(), false);
                    adopted3 = true;
                }
            });
            QObject::connect(&sender, &NetworkSession::errorOccurred, &sender, [&](const QString&) { ++errors; });
            QTcpSocket lazy;
            lazy.setReadBufferSize(4096); // đọc đầy 4 KB rồi thôi, không bao giờ lấy ra
            lazy.connectToHost(QHostAddress::LocalHost, server3.serverPort());
            CHECK(waitUntil([&] { return adopted3 && lazy.state() == QAbstractSocket::ConnectedState; }));
            sender.setSessionKeys(keyI2R, keyR2I);

            const qint64 memBefore = privateBytes();
            ProtocolMessage chunk;
            chunk.type = MessageType::ClipboardText;
            chunk.textA = QString(maxChars, QChar('z'));
            int sentOk = 0;
            bool refused = false;
            for (int i = 0; i < 400 && !refused; ++i)
            {
                if (sender.sendMessage(chunk))
                    ++sentOk;
                else
                    refused = true;
                pump(1);
            }
            CHECK(refused);
            CHECK(sentOk >= 1 && sentOk < 400);
            CHECK(errors >= 1);
            CHECK(!sender.isConnected());
            CHECK(!sender.sendMessage(chunk)); // đã đóng: từ chối, không crash
            const qint64 memAfter = privateBytes();
            CHECK(memAfter - memBefore < 96ll * 1024 * 1024);
        }
    }

    // Từ đây: ba ConnectSessionController THẬT trong cùng tiến trình (danh tính + kho peer riêng).
    // id của A < B < C -> kết nối do máy có id nhỏ hơn mở là kết nối "ưu tiên" khi mở chéo cùng lúc.
    QTemporaryDir nodeDir;
    CHECK(nodeDir.isValid());
    Node a("aaaa-stress-a", "May A", nodeDir.filePath("peers_a.json"));
    Node b("bbbb-stress-b", "May B", nodeDir.filePath("peers_b.json"));
    Node cNode("cccc-stress-c", "May C", nodeDir.filePath("peers_c.json"));
    CHECK(a.start() && b.start() && cNode.start());
    CHECK(a.port() != 0 && b.port() != 0 && cNode.port() != 0);

    // ================================================================================================
    section("4. ghep doi: sai ma lap lai, het han, ma cu, hai may cung nhap ma cho nhau");
    // ================================================================================================
    if (wantSection(4))
    {
        // Suy khóa từ mã (PBKDF2 100k vòng) chạy NGAY trên luồng gọi - trong ứng dụng thật là luồng giao
        // diện, cũng là luồng phục vụ hook chuột/phím toàn cục. In ra để biết nó chặn luồng đó bao lâu.
        {
            QElapsedTimer t;
            t.start();
            const QByteArray k1 = CryptoSession::deriveKeyFromPairingCode("123456789");
            const qint64 first = t.restart();
            const QByteArray k2 = CryptoSession::deriveKeyFromPairingCode("123456789");
            CHECK(k1.size() == CryptoSession::kKeyBytes && k1 == k2);
            std::printf("  [PBKDF2] deriveKeyFromPairingCode: %lld ms, %lld ms\n", static_cast<long long>(first),
                        static_cast<long long>(t.elapsed()));
        }

        // (a) Hai controller THẬT, mã sai (đúng định dạng) lặp lại: 4 lần đầu mã còn mở, lần thứ 5 mã bị
        //     hủy và máy tạo mã được báo đúng MỘT lần; mã ĐÚNG nhập sau đó không còn dùng được.
        for (int round = 0; round < 2; ++round)
        {
            a.pairFailed.clear();
            b.pairFailed.clear();
            const int closedBefore = a.pairClosed;
            const QString code = a.c->beginPairingSession();
            for (int attempt = 1; attempt <= PairingCode::kMaxAttempts; ++attempt)
            {
                QString wrong;
                do
                    wrong = PairingCode::generate();
                while (wrong == code);
                const int failedBefore = static_cast<int>(b.pairFailed.size());
                b.c->connectWithCode(QHostAddress::LocalHost, a.port(), wrong);
                CHECK(waitUntil([&] { return b.pairFailed.size() == failedBefore + 1; }));
                CHECK(waitUntil([&] { return a.pending() == 0 && b.pending() == 0; }));
                CHECK(a.c->isPairingSessionOpen() == (attempt < PairingCode::kMaxAttempts));
            }
            CHECK(waitUntil([&] { return a.pairFailed.size() == 1; }));
            CHECK(a.pairClosed == closedBefore + 1);
            const int failedBefore = static_cast<int>(b.pairFailed.size());
            b.c->connectWithCode(QHostAddress::LocalHost, a.port(), code); // mã đúng nhưng đã bị hủy
            CHECK(waitUntil([&] { return b.pairFailed.size() == failedBefore + 1; }));
            pump(100);
            CHECK(a.pairFailed.size() == 1);
            CHECK(a.pairOk == 0 && b.pairOk == 0);
            CHECK(a.store->peers().isEmpty() && b.store->peers().isEmpty());
        }
        lap("4a ma sai x2 vong (controller that)");

        // (b) Dồn dập bằng "máy giả": mỗi vòng một mã mới + các kết nối KHÔNG phải một lần thử mã (đóng
        //     ngay sau preamble, khung độ dài 0, khung độ dài bịa) - không được tính vào giới hạn - rồi đúng
        //     kMaxAttempts khung mã hóa bằng khóa SAI: mã phải bị hủy đúng ở lần thứ kMaxAttempts.
        {
            int wrongState = 0, notClosed = 0;
            for (int round = 0; round < 12; ++round)
            {
                a.c->beginPairingSession();
                notClosed += fuzzBatch(a.port(), 9, [&](RawPeer& p, int i) {
                    p.sendPreamble(ConnectProtocol::LinkPurpose::Pairing, QString("gia-%1").arg(i));
                    switch (i % 3)
                    {
                        case 0: p.sock.abort(); return false;
                        case 1: p.write(lengthPrefix(0)); return true;
                        default: p.write(lengthPrefix(0x00FFFFFFu)); return true;
                    }
                });
                if (!a.c->isPairingSessionOpen())
                    ++wrongState;
                for (int attempt = 1; attempt <= PairingCode::kMaxAttempts; ++attempt)
                {
                    notClosed += fuzzBatch(a.port(), 1, [&](RawPeer& p, int) {
                        p.sendPreamble(ConnectProtocol::LinkPurpose::Pairing, "gia-sai-ma");
                        p.hs = std::make_unique<CryptoSession>(CryptoSession::generateRandomKey());
                        ProtocolMessage req;
                        req.type = MessageType::PairRequest;
                        req.textA = "Ke Do Ma";
                        req.textB = "gia-sai-ma";
                        req.intA = ConnectProtocol::kVersion;
                        req.intB = 1234;
                        // lần chẵn: gói mã hóa đúng dạng bằng khóa sai; lần lẻ: rác đủ dài để coi là một gói
                        p.write(frameOf(attempt % 2 ? p.handshakePacket(req) : randomBytes(60)));
                        return true;
                    });
                    if (a.c->isPairingSessionOpen() != (attempt < PairingCode::kMaxAttempts))
                        ++wrongState;
                }
            }
            CHECK(wrongState == 0);
            CHECK(notClosed == 0);
            CHECK(waitUntil([&] { return a.pending() == 0; }));
            pump(30); // cho tín hiệu pairingFailed (phát trễ một nhịp) của lần hủy mã cuối kịp tới
            CHECK(a.store->peers().isEmpty());
            CHECK(a.pairOk == 0);
        }
        lap("4b gioi han thu ma x12 vong (may gia)");

        // (c) HẾT HẠN thật: cho đồng hồ đếm ngược của mã chạy đủ kExpirySeconds nhịp (phát tín hiệu timeout
        //     của chính QTimer 1 giây đó, không chờ 5 phút). 299 nhịp: còn mở. Nhịp 300: đóng, kết nối ghép
        //     đôi đang chờ bị đóng theo, mã cũ không dùng được nữa.
        {
            a.pairFailed.clear();
            b.pairFailed.clear();
            const QString code = a.c->beginPairingSession();
            QTimer* pairingTimer = nullptr;
            for (QTimer* t : a.c->findChildren<QTimer*>(Qt::FindDirectChildrenOnly))
                if (t->interval() == 1000 && t->isActive())
                    pairingTimer = t;
            CHECK(pairingTimer != nullptr);
            RawPeer waiting;
            CHECK(waiting.open(a.port()));
            waiting.sendPreamble(ConnectProtocol::LinkPurpose::Pairing, "gia-cho-het-han");
            CHECK(waitUntil([&] { return a.pending() == 1; }));
            if (pairingTimer)
            {
                bool invoked = true;
                for (int i = 0; i < PairingCode::kExpirySeconds - 1; ++i)
                    invoked = QMetaObject::invokeMethod(pairingTimer, "timeout", Qt::DirectConnection) && invoked;
                CHECK(invoked);
                CHECK(a.c->isPairingSessionOpen());
                CHECK(!waiting.closed);
                const int closedBefore = a.pairClosed;
                QMetaObject::invokeMethod(pairingTimer, "timeout", Qt::DirectConnection);
                CHECK(!a.c->isPairingSessionOpen());
                CHECK(a.pairClosed == closedBefore + 1);
                CHECK(waiting.waitClosed());
                CHECK(!pairingTimer->isActive());
            }
            b.c->connectWithCode(QHostAddress::LocalHost, a.port(), code);
            CHECK(waitUntil([&] { return b.pairFailed.size() == 1; }));
            CHECK(a.pairOk == 0 && b.pairOk == 0);
            CHECK(a.pairFailed.isEmpty()); // hết hạn không phải lỗi của máy tạo mã - không bật hộp thoại
        }
        lap("4c het han");

        // (d) Kết nối mở từ lúc mã TRƯỚC còn hiệu lực không dùng được cho mã SAU (tạo mã mới / hủy mã đều
        //     đóng nó), lặp nhiều lần; không rò kết nối chờ.
        {
            int survived = 0;
            for (int round = 0; round < 10; ++round)
            {
                a.c->beginPairingSession();
                RawPeer stale;
                stale.open(a.port());
                stale.sendPreamble(ConnectProtocol::LinkPurpose::Pairing, "gia-ma-cu");
                waitUntil([&] { return a.pending() == 1; });
                if (round % 2)
                    a.c->beginPairingSession();
                else
                    a.c->cancelPairingSession();
                if (!stale.waitClosed())
                    ++survived;
            }
            a.c->cancelPairingSession();
            CHECK(survived == 0);
            CHECK(waitUntil([&] { return a.pending() == 0; }));
        }
        lap("4d ket noi cua ma cu x10");

        // (e) Ghép đôi thật B -> A bằng đúng mã, rồi hội tụ về một phiên.
        a.pairFailed.clear();
        b.pairFailed.clear();
        {
            const QString code = a.c->beginPairingSession();
            b.c->connectWithCode(QHostAddress::LocalHost, a.port(), code);
            CHECK(waitUntil([&] { return a.pairOk == 1 && b.pairOk == 1; }));
            CHECK(a.pairFailed.isEmpty() && b.pairFailed.isEmpty());
            CHECK(!a.keyFor(b).isEmpty() && a.keyFor(b) == b.keyFor(a));
            CHECK(waitUntil([&] { return settled(a, b); }, 8000));
        }

        // (f) HAI MÁY CÙNG NHẬP MÃ CHO NHAU: mỗi máy vừa tạo mã vừa nhập mã của máy kia, hai yêu cầu đi chéo
        //     nhau. Kết quả bắt buộc: hai máy giữ CÙNG một khóa dài hạn (nếu mỗi máy giữ khóa do một bên
        //     sinh ra thì cả hai báo "đã ghép đôi" nhưng không bao giờ bắt tay phiên được), không ai bị báo
        //     lỗi, và phiên tự lên. Vòng lẻ chèn độ trễ ngẫu nhiên giữa hai lần bấm để đổi thứ tự xen kẽ.
        {
            int keyMismatch = 0, notConnected = 0, spuriousFailures = 0, notPaired = 0;
            for (int round = 0; round < 10; ++round)
            {
                a.pairFailed.clear();
                b.pairFailed.clear();
                const int okA = a.pairOk, okB = b.pairOk;
                const QString codeA = a.c->beginPairingSession();
                const QString codeB = b.c->beginPairingSession();
                if (round % 4 < 2)
                {
                    a.c->connectWithCode(QHostAddress::LocalHost, b.port(), codeB);
                    if (round % 2)
                        pump(static_cast<int>(g_rng.bounded(6)));
                    b.c->connectWithCode(QHostAddress::LocalHost, a.port(), codeA);
                }
                else
                {
                    b.c->connectWithCode(QHostAddress::LocalHost, a.port(), codeA);
                    if (round % 2)
                        pump(static_cast<int>(g_rng.bounded(6)));
                    a.c->connectWithCode(QHostAddress::LocalHost, b.port(), codeB);
                }
                if (!waitUntil([&] { return a.pairOk > okA && b.pairOk > okB; }, 8000))
                    ++notPaired;
                // chờ mọi kết nối ghép đôi còn dở kết thúc hẳn (kể cả yêu cầu đến sau)
                waitUntil([&] { return !a.c->isPairingSessionOpen() && !b.c->isPairingSessionOpen(); }, 8000);
                pump(150);
                const bool sameKey = a.keyFor(b).size() == CryptoSession::kKeyBytes && a.keyFor(b) == b.keyFor(a);
                if (!sameKey)
                    ++keyMismatch;
                if (!waitUntil([&] { return settled(a, b); }, sameKey ? 8000 : 1500))
                    ++notConnected;
                if (!a.pairFailed.isEmpty() || !b.pairFailed.isEmpty())
                    ++spuriousFailures;
                a.c->cancelPairingSession();
                b.c->cancelPairingSession();
            }
            if (notPaired || keyMismatch || notConnected || spuriousFailures)
                std::printf("  [nhap ma cheo x10] khong ghep doi=%d lech khoa=%d khong len phien=%d bao loi thua=%d\n",
                            notPaired, keyMismatch, notConnected, spuriousFailures);
            CHECK(notPaired == 0);
            CHECK(keyMismatch == 0);
            CHECK(notConnected == 0);
            CHECK(spuriousFailures == 0);
        }
        lap("4f hai may cung nhap ma cho nhau x10");

        // (g) Ghép đôi LẠI theo chiều ngược (A nhập mã của B) trong lúc đang có phiên: khóa mới giống nhau ở
        //     hai máy, phiên cũ (khóa cũ) bị đóng, phiên mới tự lên.
        {
            const QByteArray oldKey = a.keyFor(b);
            const QString code = b.c->beginPairingSession();
            const int okA = a.pairOk, okB = b.pairOk;
            a.c->connectWithCode(QHostAddress::LocalHost, b.port(), code);
            CHECK(waitUntil([&] { return a.pairOk == okA + 1 && b.pairOk == okB + 1; }));
            CHECK(a.keyFor(b) == b.keyFor(a) && a.keyFor(b) != oldKey);
            CHECK(waitUntil([&] { return settled(a, b); }, 8000));
        }
    }

    // Nếu mục 4 không ghép đôi được (lỗi thật) thì các mục sau vẫn cần một cặp khóa để chạy tiếp.
    if (a.keyFor(b).isEmpty() || a.keyFor(b) != b.keyFor(a))
        forcePair(a, b);
    a.pointAt(b);
    b.pointAt(a);
    a.c->reconnectNow();
    b.c->reconnectNow();
    CHECK(waitUntil([&] { return settled(a, b); }, 10000));

    // ================================================================================================
    section("5. vong doi: ngat/ket noi lai, mo cheo cung luc, start/stop don dap, ro ri");
    // ================================================================================================
    if (wantSection(5))
    {
        // (a) Một máy dừng rồi chạy lại (cổng lắng nghe đổi), lặp 60 vòng luân phiên: máy còn lại thấy rớt,
        //     hai máy tự nối lại, không kết nối chờ nào sót, vai trò Idle, đúng MỘT NetworkSession sống.
        int notDropped = 0, notReconnected = 0, leaked = 0, badRole = 0;
        qint64 worstMs = 0;
        for (int round = 0; round < 60; ++round)
        {
            Node& restarting = round % 2 ? a : b;
            Node& staying = round % 2 ? b : a;
            restarting.c->stop();
            if (restarting.c->isRunning() || restarting.pending() != 0 || restarting.connectedTo(staying))
                ++leaked;
            if (!waitUntil([&] { return !staying.connectedTo(restarting); }, 5000))
                ++notDropped;
            QElapsedTimer t;
            t.start();
            restarting.start();
            if (!waitUntil([&] { return settled(a, b); }, 15000))
                ++notReconnected;
            worstMs = qMax(worstMs, t.elapsed());
            pump(20);
            if (a.liveSessions() != 1 || b.liveSessions() != 1)
                ++leaked;
            if (a.c->currentRole() != ControlRole::Idle || b.c->currentRole() != ControlRole::Idle || a.c->isHookActive() ||
                b.c->isHookActive())
                ++badRole;
        }
        CHECK(notDropped == 0);
        CHECK(notReconnected == 0);
        CHECK(leaked == 0);
        CHECK(badRole == 0);
        std::printf("  [ket noi lai x60] cham nhat %lld ms\n", static_cast<long long>(worstMs));
        lap("5a mot may dung/chay lai x60");

        // (b) MỞ CHÉO CÙNG LÚC, 100 vòng: cả hai máy cùng dừng, cùng chạy lại, mỗi máy biết cổng mới của
        //     máy kia, rồi lần thử kết nối đầu tiên của HAI máy chạy trong cùng một nhịp vòng lặp sự kiện.
        //     Phân xử phải cho cả hai giữ CÙNG một kết nối: hai đầu cùng có phiên, không kết nối chờ sót,
        //     và phiên đó đứng vững (không tự rớt rồi lên lại).
        int notConverged = 0, unstable = 0, leakedCross = 0;
        worstMs = 0;
        for (int round = 0; round < 100; ++round)
        {
            a.c->stop();
            b.c->stop();
            if (round % 2)
            {
                b.start();
                a.start();
            }
            else
            {
                a.start();
                b.start();
            }
            a.pointAt(b);
            b.pointAt(a);
            a.ups.clear();
            a.downs.clear();
            b.ups.clear();
            b.downs.clear();
            QElapsedTimer t;
            t.start();
            if (!waitUntil([&] { return settled(a, b); }, 15000))
                ++notConverged;
            worstMs = qMax(worstMs, t.elapsed());
            pump(60);
            if (!settled(a, b) || a.downs.value(b.id) != 0 || b.downs.value(a.id) != 0 || a.ups.value(b.id) != 1 ||
                b.ups.value(a.id) != 1)
                ++unstable;
            if (a.liveSessions() != 1 || b.liveSessions() != 1)
                ++leakedCross;
        }
        CHECK(notConverged == 0);
        CHECK(unstable == 0);
        CHECK(leakedCross == 0);
        std::printf("  [mo cheo x100] hoi tu cham nhat %lld ms\n", static_cast<long long>(worstMs));
        lap("5b mo cheo cung luc x100");

        // (c) start()/stop() DỒN DẬP: 400 lần, phần lớn không cho vòng lặp sự kiện chạy ở giữa (đối tượng
        //     hẹn hủy dồn lại), thỉnh thoảng bơm vài mili giây để kết nối kịp dở dang giữa chừng.
        for (int i = 0; i < 400; ++i)
        {
            Node& n = g_rng.bounded(2) ? a : b;
            switch (g_rng.bounded(4))
            {
                case 0: n.c->stop(); break;
                case 1: n.start(); break;
                case 2:
                    n.c->stop();
                    n.start();
                    break;
                default:
                    n.start();
                    n.c->reconnectNow();
                    n.c->stop();
                    break;
            }
            if (g_rng.bounded(5) == 0)
                pump(static_cast<int>(g_rng.bounded(4)));
        }
        a.c->stop();
        b.c->stop();
        CHECK(!a.c->isRunning() && !b.c->isRunning());
        CHECK(a.pending() == 0 && b.pending() == 0);
        pump(50);
        // Đã dừng hẳn + vòng lặp sự kiện đã chạy: không còn MỘT đối tượng con nào của lần chạy trước.
        CHECK(a.liveSessions() == 0 && b.liveSessions() == 0);
        CHECK(a.liveTimers() == 0 && b.liveTimers() == 0);
        CHECK(a.liveServers() == 0 && b.liveServers() == 0);
        CHECK(a.liveHooks() == 0 && b.liveHooks() == 0);
        CHECK(a.c->currentRole() == ControlRole::Idle && b.c->currentRole() == ControlRole::Idle);
        CHECK(a.start() && b.start());
        a.pointAt(b);
        b.pointAt(a);
        CHECK(waitUntil([&] { return settled(a, b); }, 15000));
        pump(50);
        CHECK(a.liveSessions() == 1 && b.liveSessions() == 1);
        CHECK(a.liveTimers() == 2 && b.liveTimers() == 2); // bộ dò biên + việc định kỳ
        CHECK(a.liveServers() == 1 && b.liveServers() == 1);
        CHECK(a.liveHooks() == 1 && b.liveHooks() == 1);
        lap("5c start/stop don dap x400");

        // (d) HỦY HẲN controller đang có phiên (không gọi stop() trước) rồi tạo lại, 40 vòng: máy kia thấy
        //     rớt (nhận Goodbye từ destructor), nối lại được với controller mới.
        int destroyFailures = 0;
        for (int round = 0; round < 40; ++round)
        {
            Node& victim = round % 2 ? a : b;
            Node& other = round % 2 ? b : a;
            victim.c.reset();
            if (!waitUntil([&] { return !other.c->isPeerConnected(victim.id); }, 5000))
                ++destroyFailures;
            victim.create();
            victim.start();
            if (!waitUntil([&] { return settled(a, b); }, 15000))
                ++destroyFailures;
        }
        CHECK(destroyFailures == 0);
        pump(50);
        CHECK(a.liveSessions() == 1 && b.liveSessions() == 1);
    }

    // ================================================================================================
    section("6. trao quyen dieu khien + chuyen tiep input don dap");
    // ================================================================================================
    if (wantSection(6))
    {
        CHECK(waitUntil([&] { return settled(a, b); }, 10000));
        a.c->setPeerSide(b.id, ScreenSide::Right);
        b.c->setPeerSide(a.id, ScreenSide::Left);
        a.injector.reset();
        b.injector.reset();

        // (a) Trao quyền A->B rồi trả bằng Ctrl+Alt+Home, 200 vòng; mỗi vòng gõ một phím. Sau mỗi vòng: cả
        //     hai Idle, hook của A tắt, B không còn phím nào "kẹt".
        int handoffFailures = 0, stuck = 0;
        for (int round = 0; round < 200; ++round)
        {
            if (!a.c->requestControlForTesting(b.id, ScreenSide::Right) || !a.c->isHookActive())
                ++handoffFailures;
            if (!waitUntil([&] { return b.c->currentRole() == ControlRole::BeingControlled; }))
                ++handoffFailures;
            InputHook* hookA = a.c->hookForTesting();
            const int vk = 0x41 + round % 26;
            hookA->onRawKey(vk, 30, true, false);
            if (round % 3 == 0)
                hookA->onRawKey(vk, 30, false, false); // 2/3 số vòng: phím CÒN GIỮ lúc trả quyền
            if (round % 5 == 0)
                hookA->onRawMouseButton(round % 5, true);
            if (round % 2)
                pressReturnHotkey(hookA); // người ở máy điều khiển trả quyền
            else
            {
                waitUntil([&] { return b.injector.heldKeys.contains(vk) || round % 3 == 0; });
                pressReturnHotkey(b.c->hookForTesting()); // người ở máy BỊ điều khiển ngắt
            }
            if (!waitUntil([&] {
                    return a.c->currentRole() == ControlRole::Idle && b.c->currentRole() == ControlRole::Idle;
                }))
                ++handoffFailures;
            if (a.c->isHookActive() || b.c->isHookActive())
                ++handoffFailures;
            // lần nhả vật lý tới SAU khi đã trả quyền: bị nuốt cho khớp cặp, không gửi đi đâu
            hookA->onRawKey(vk, 30, false, false);
            hookA->onRawMouseButton(round % 5, false);
            pump(2);
            if (!b.injector.heldKeys.isEmpty() || !b.injector.heldButtons.isEmpty())
                ++stuck;
        }
        CHECK(handoffFailures == 0);
        CHECK(stuck == 0);
        CHECK(a.injector.keyEvents == 0 && a.injector.buttonEvents == 0); // A không bao giờ tự tiêm vào chính nó
        CHECK(settled(a, b));

        // (b) Bắn 8000 cử động chuột + 2000 cặp nhấn/nhả phím trong MỘT hơi theo mỗi chiều: tới đủ, đúng thứ tự.
        auto flood = [&](Node& from, Node& to, ScreenSide side) {
            to.injector.reset();
            CHECK(from.c->requestControlForTesting(to.id, side));
            CHECK(waitUntil([&] { return to.c->currentRole() == ControlRole::BeingControlled; }));
            InputHook* hook = from.c->hookForTesting();
            for (int i = 0; i < 8000; ++i)
            {
                hook->onRawMouseMove(1000 + (i % 1000) + 1, 500 - (i % 7), 1000, 500); // dx = i%1000+1, dy = -(i%7)
                if (i % 4 == 0)
                {
                    const int vk = 0x30 + (i / 4) % 10;
                    hook->onRawKey(vk, 2, true, false);
                    hook->onRawKey(vk, 2, false, false);
                }
            }
            CHECK(waitUntil([&] { return to.injector.relativeMoves.size() == 8000 && to.injector.keyEvents == 4000; }, 30000));
            int wrongOrder = 0;
            for (int i = 0; i < to.injector.relativeMoves.size(); ++i)
                if (to.injector.relativeMoves.at(i) != QPoint((i % 1000) + 1, -(i % 7)))
                    ++wrongOrder;
            for (int i = 0; i < to.injector.keyLog.size(); ++i)
            {
                const int vk = 0x30 + (i / 2) % 10;
                if (to.injector.keyLog.at(i) != (i % 2 ? -vk : vk))
                    ++wrongOrder;
            }
            CHECK(wrongOrder == 0);
            CHECK(to.injector.outOfRange == 0);
            pressReturnHotkey(hook);
            CHECK(waitUntil([&] { return to.c->currentRole() == ControlRole::Idle; }));
            CHECK(from.c->currentRole() == ControlRole::Idle && !from.c->isHookActive());
            CHECK(to.injector.heldKeys.isEmpty());
        };
        flood(a, b, ScreenSide::Right);
        flood(b, a, ScreenSide::Left);
        CHECK(settled(a, b));

        // (c) Hai máy cùng đòi điều khiển nhau trong cùng một nhịp, 60 vòng: không ai kẹt ở Controlling
        //     với hook bật; phiên không rớt.
        int deadlocked = 0;
        for (int round = 0; round < 60; ++round)
        {
            const bool okA = a.c->requestControlForTesting(b.id, ScreenSide::Right);
            const bool okB = b.c->requestControlForTesting(a.id, ScreenSide::Left);
            if (!okA || !okB)
                ++deadlocked;
            if (!waitUntil([&] {
                    return a.c->currentRole() == ControlRole::Idle && b.c->currentRole() == ControlRole::Idle;
                }))
                ++deadlocked;
            if (a.c->isHookActive() || b.c->isHookActive())
                ++deadlocked;
        }
        CHECK(deadlocked == 0);
        CHECK(settled(a, b));

        // (d) RỚT KẾT NỐI GIỮA LÚC ĐIỀU KHIỂN, 30 vòng, luân phiên máy biến mất là máy bị điều khiển / máy
        //     đang điều khiển, lúc còn phím + nút chuột đang giữ: hook phải tắt, vai trò về Idle ở máy còn
        //     lại, phím/nút đã tiêm phải được nhả; rồi hai máy nối lại.
        int lockups = 0, stuckAfterDrop = 0, notBack = 0;
        for (int round = 0; round < 30; ++round)
        {
            b.injector.reset();
            if (!a.c->requestControlForTesting(b.id, ScreenSide::Right))
                ++lockups;
            waitUntil([&] { return b.c->currentRole() == ControlRole::BeingControlled; });
            InputHook* hookA = a.c->hookForTesting();
            hookA->onRawKey(VK_SHIFT, 42, true, false);
            hookA->onRawMouseButton(0, true);
            waitUntil([&] { return b.injector.heldKeys.contains(VK_SHIFT) && b.injector.heldButtons.contains(0); });
            if (!b.injector.heldKeys.contains(VK_SHIFT))
                ++lockups;
            Node& vanishing = round % 2 ? a : b;
            Node& remaining = round % 2 ? b : a;
            if (round % 3 == 0)
                vanishing.c.reset(); // hủy thẳng
            else
                vanishing.c->stop();
            if (!waitUntil([&] { return remaining.c->currentRole() == ControlRole::Idle; }))
                ++lockups;
            if (remaining.c->isHookActive())
                ++lockups;
            if (!b.injector.heldKeys.isEmpty() || !b.injector.heldButtons.isEmpty())
                ++stuckAfterDrop;
            if (!vanishing.c)
                vanishing.create();
            // lần nhả vật lý của Shift/nút trái trên A (hook mới hoặc cũ đều phải nuốt/cho qua mà không crash)
            if (InputHook* h = a.c->hookForTesting())
            {
                h->onRawKey(VK_SHIFT, 42, false, false);
                h->onRawMouseButton(0, false);
            }
            vanishing.start();
            if (!waitUntil([&] { return settled(a, b); }, 15000))
                ++notBack;
        }
        CHECK(lockups == 0);
        CHECK(stuckAfterDrop == 0);
        CHECK(notBack == 0);

        // (e) Máy thứ ba C chen vào lúc A đang điều khiển B: B từ chối C (C về Idle, hook C tắt), A vẫn điều
        //     khiển B bình thường; máy đang bị điều khiển không tự đi điều khiển máy khác được.
        forcePair(b, cNode);
        b.c->reconnectNow();
        cNode.c->reconnectNow();
        CHECK(waitUntil([&] { return settled(b, cNode); }, 10000));
        CHECK(settled(a, b));
        b.injector.reset();
        int thirdPartyFailures = 0;
        for (int round = 0; round < 40; ++round)
        {
            if (!a.c->requestControlForTesting(b.id, ScreenSide::Right))
                ++thirdPartyFailures;
            waitUntil([&] { return b.c->currentRole() == ControlRole::BeingControlled; });
            if (!cNode.c->requestControlForTesting(b.id, ScreenSide::Left)) // gửi được lệnh, C tạm bật cờ
                ++thirdPartyFailures;
            cNode.c->hookForTesting()->onRawKey(0x5A, 44, true, false); // phím của C không được tới B
            if (!waitUntil([&] { return cNode.c->currentRole() == ControlRole::Idle && !cNode.c->isHookActive(); }))
                ++thirdPartyFailures;
            cNode.c->hookForTesting()->onRawKey(0x5A, 44, false, false);
            if (b.c->currentRole() != ControlRole::BeingControlled || b.c->activePeerName() != a.name ||
                a.c->currentRole() != ControlRole::Controlling)
                ++thirdPartyFailures;
            if (b.c->requestControlForTesting(cNode.id, ScreenSide::Right)) // đang bị điều khiển -> không được
                ++thirdPartyFailures;
            pressReturnHotkey(a.c->hookForTesting());
            if (!waitUntil([&] { return b.c->currentRole() == ControlRole::Idle; }))
                ++thirdPartyFailures;
        }
        CHECK(thirdPartyFailures == 0);
        CHECK(!b.injector.keyLog.contains(0x5A));
        CHECK(settled(a, b) && settled(b, cNode));
        // C rời cuộc chơi: gỡ ghép đôi ở cả hai phía để B không gõ cửa C mãi trong các mục sau.
        b.c->forgetPeer(cNode.id);
        cNode.c->forgetPeer(b.id);
        CHECK(waitUntil([&] { return !b.c->isPeerConnected(cNode.id) && !cNode.c->isPeerConnected(b.id); }));
        CHECK(waitUntil([&] { return settled(a, b); }, 10000));
    }

    // ================================================================================================
    section("7. fuzz cong lang nghe o moi giai doan bat tay");
    // ================================================================================================
    if (wantSection(7))
    {
        // Một "máy giả" đã ghép đôi với A (không tự kết nối lại - nó không nghe cổng nào).
        const QString fakeId = "ffff-may-gia";
        const QByteArray fakeKey = CryptoSession::generateRandomKey();
        a.store->addOrUpdate(makePeer(fakeId, "May Gia", fakeKey, 1, false));
        CHECK(waitUntil([&] { return settled(a, b); }, 10000));
        a.c->cancelPairingSession();
        a.ups.clear();
        a.downs.clear();
        a.injector.reset();
        pump(100);
        const qint64 memBefore = privateBytes();
        const int handlesBefore = handleCount();
        int framesSent = 0;

        // (a) TRƯỚC preamble: 1500 kết nối, mỗi cái một kiểu rác. Kết nối "phải bị đóng" đều bị đóng ngay
        //     (không chờ hạn bắt tay); không cái nào làm phiên thật A-B rớt.
        {
            int notClosed = 0;
            for (int batch = 0; batch < 125; ++batch)
            {
                notClosed += fuzzBatch(a.port(), 12, [&](RawPeer& p, int) {
                    ++framesSent;
                    switch (g_rng.bounded(19))
                    {
                        case 0: // vài byte ngẫu nhiên rồi tự đóng (4 byte đầu có thể là một độ dài hợp lệ -> không chắc bị đóng)
                            p.write(randomBytes(1 + static_cast<int>(g_rng.bounded(64))));
                            return false;
                        case 1: p.write(lengthPrefix(0)); return true;
                        case 2: p.write(lengthPrefix(0xFFFFFFFFu)); return true;
                        case 3: p.write(lengthPrefix(257) + randomBytes(257)); return true;
                        case 4: p.write(frameOf(randomBytes(256))); return true; // "preamble" rác đúng trần
                        case 5: // độ dài cắt cụt
                            p.write(lengthPrefix(40).left(1 + static_cast<int>(g_rng.bounded(3))));
                            return false;
                        case 6: // khai 100 byte, chỉ gửi 50
                            p.write(lengthPrefix(100) + randomBytes(50));
                            return false;
                        case 7: // đúng chữ ký, sai phiên bản
                        {
                            QByteArray pre = ConnectProtocol::buildPreamble(ConnectProtocol::LinkPurpose::Session, fakeId);
                            pre[4] = static_cast<char>(g_rng.bounded(2) ? 1 : 3 + g_rng.bounded(250));
                            p.write(frameOf(pre) + frameOf(randomBytes(40)));
                            return true;
                        }
                        case 8: // ghép đôi khi KHÔNG có mã nào đang mở
                            p.sendPreamble(ConnectProtocol::LinkPurpose::Pairing, "gia-ghep-doi");
                            p.write(frameOf(randomBytes(60)));
                            return true;
                        case 9: // phiên từ id lạ
                            p.sendPreamble(ConnectProtocol::LinkPurpose::Session, "khong-ai-biet");
                            return true;
                        case 10: // khai id của CHÍNH máy A
                            p.sendPreamble(ConnectProtocol::LinkPurpose::Session, a.id);
                            return true;
                        case 11: // khai id của B (đang có phiên thật) + khung rác
                            p.sendPreamble(ConnectProtocol::LinkPurpose::Session, b.id);
                            p.write(frameOf(randomBytes(28 + static_cast<int>(g_rng.bounded(100)))));
                            return true;
                        case 12: // mục đích không tồn tại
                        {
                            QByteArray pre = ConnectProtocol::buildPreamble(ConnectProtocol::LinkPurpose::Session, fakeId);
                            pre[5] = static_cast<char>(g_rng.bounded(2) ? 0 : 3 + g_rng.bounded(250));
                            p.write(frameOf(pre));
                            return true;
                        }
                        case 13: // byte độ dài id không khớp
                        {
                            QByteArray pre = ConnectProtocol::buildPreamble(ConnectProtocol::LinkPurpose::Session, fakeId);
                            pre[6] = static_cast<char>(pre.at(6) + 1 + g_rng.bounded(5));
                            p.write(frameOf(pre));
                            return true;
                        }
                        case 14: // id chứa ký tự cấm
                        {
                            QByteArray pre = ConnectProtocol::buildPreamble(ConnectProtocol::LinkPurpose::Session, fakeId);
                            static const char kBad[] = {' ', '/', '\0', '\n', '<', '\x80', '\xFF'};
                            pre[7 + static_cast<int>(g_rng.bounded(4))] = kBad[g_rng.bounded(7)];
                            p.write(frameOf(pre));
                            return true;
                        }
                        case 15: // một cục 10 KB ngẫu nhiên (4 byte đầu gần như chắc chắn là độ dài quá trần)
                        {
                            QByteArray blob = randomBytes(10240);
                            blob[0] = '\x7F';
                            p.write(blob);
                            return true;
                        }
                        case 16: p.sock.abort(); return false; // nối rồi cắt ngay
                        case 17: // preamble hợp lệ của máy giả, rồi độ dài vượt trần chưa xác thực
                            p.sendPreamble(ConnectProtocol::LinkPurpose::Session, fakeId);
                            p.write(lengthPrefix(NetworkSession::kUnauthenticatedMaxFrameBytes + 1));
                            return true;
                        default: // preamble hợp lệ của máy giả, rồi một khung rác đủ mọi cỡ
                        {
                            static const int kSizes[] = {1, 11, 12, 27, 28, 29, 64, 1000, 4096};
                            p.sendPreamble(ConnectProtocol::LinkPurpose::Session, fakeId);
                            p.write(frameOf(randomBytes(kSizes[g_rng.bounded(9)])));
                            return true;
                        }
                    }
                });
            }
            CHECK(notClosed == 0);
            CHECK(waitUntil([&] { return a.pending() == 0; }, a.handshakeTimeoutMs + 3000));
            CHECK(settled(a, b));
            CHECK(a.downs.value(b.id) == 0);
            CHECK(a.ups.value(fakeId) == 0);
        }
        lap("7a truoc preamble x1500 ket noi");

        // (b) Bước chờ SessionHello (sau preamble hợp lệ): thông điệp mã hóa ĐÚNG khóa nhưng sai loại / sai
        //     nội dung / sai chiều (phản xạ) / sai ngữ cảnh, hoặc Hello hợp lệ theo sau bởi rác. 720 kết nối.
        {
            int notClosed = 0;
            for (int batch = 0; batch < 60; ++batch)
            {
                notClosed += fuzzBatch(a.port(), 12, [&](RawPeer& p, int) {
                    p.hs = std::make_unique<CryptoSession>(fakeKey);
                    p.sendPreamble(ConnectProtocol::LinkPurpose::Session, fakeId);
                    ProtocolMessage hello = p.helloMessage(fakeId);
                    ++framesSent;
                    switch (g_rng.bounded(11))
                    {
                        case 0: // loại không thuộc bước này
                        {
                            static const MessageType kWrong[] = {MessageType::PairRequest, MessageType::PairAccept,
                                                                 MessageType::PairReject, MessageType::SessionHelloAck,
                                                                 MessageType::SessionConfirm, MessageType::Heartbeat,
                                                                 MessageType::ControlHandoff, MessageType::KeyEvent,
                                                                 MessageType::ClipboardText, MessageType::Goodbye};
                            hello.type = kWrong[g_rng.bounded(10)];
                            hello.longTermKey = randomBytes(32);
                            p.sendHandshake(hello);
                            return true;
                        }
                        case 1: hello.textB = b.id; p.sendHandshake(hello); return true; // id khác preamble
                        case 2: // nonce sai cỡ
                        {
                            static const int kSizes[] = {0, 1, 15, 17, 32, 3000};
                            hello.nonce = randomBytes(kSizes[g_rng.bounded(6)]);
                            p.sendHandshake(hello);
                            return true;
                        }
                        case 3: hello.intA = g_rng.bounded(2) ? 1 : 3; p.sendHandshake(hello); return true; // sai phiên bản
                        case 4: p.write(frameOf(p.handshakePacketAs(hello, false))); return true; // nhãn chiều "R" (phản xạ)
                        case 5: // mã hóa dưới preamble KHÁC (ngữ cảnh khác)
                        {
                            QByteArray packet;
                            p.hs->encrypt(hello.toBytes(), &packet, nullptr,
                                          handshakeAad(true, ConnectProtocol::buildPreamble(ConnectProtocol::LinkPurpose::Pairing, fakeId)));
                            p.write(frameOf(packet));
                            return true;
                        }
                        case 6: // khóa sai
                            p.hs = std::make_unique<CryptoSession>(CryptoSession::generateRandomKey());
                            p.sendHandshake(hello);
                            return true;
                        case 7: // Hello hợp lệ + rác trong CÙNG một lần ghi
                            p.write(frameOf(p.handshakePacket(hello)) + frameOf(randomBytes(40)));
                            ++framesSent;
                            return true;
                        case 8: // Hello hợp lệ hai lần liên tiếp
                            p.write(frameOf(p.handshakePacket(hello)) + frameOf(p.handshakePacket(hello)));
                            ++framesSent;
                            return true;
                        case 9: // Hello với mọi trường "khó chịu"
                            hello.textA = QString(300, QChar(0x202E)) + "<b>";
                            hello.intB = wildInt();
                            hello.longTermKey = randomBytes(500);
                            hello.flagA = hello.flagB = true;
                            p.sendHandshake(hello); // hợp lệ về giao thức -> A trả HelloAck và CHỜ Confirm
                            p.write(frameOf(randomBytes(33)));
                            ++framesSent;
                            return true;
                        default: // sửa 1 bit của gói hợp lệ
                        {
                            QByteArray packet = p.handshakePacket(hello);
                            const int pos = static_cast<int>(g_rng.bounded(static_cast<int>(packet.size())));
                            packet[pos] = static_cast<char>(packet.at(pos) ^ (1 << g_rng.bounded(8)));
                            p.write(frameOf(packet));
                            return true;
                        }
                    }
                });
            }
            CHECK(notClosed == 0);
            CHECK(waitUntil([&] { return a.pending() == 0; }, a.handshakeTimeoutMs + 3000));
            CHECK(a.ups.value(fakeId) == 0);
            CHECK(settled(a, b) && a.downs.value(b.id) == 0);
        }
        lap("7b cho SessionHello x720 ket noi");

        // (c) Bước chờ SessionConfirm (đã trao đổi Hello/HelloAck THẬT, A đã chuyển sang khóa phiên): rác,
        //     Confirm bằng khóa chiều ngược, lệch bộ đếm, sai loại, sai id, hoặc im lặng tới hết hạn bắt tay.
        //     Không trường hợp nào được đăng ký phiên. 360 kết nối.
        {
            int notClosed = 0, helloFailed = 0;
            for (int batch = 0; batch < 30; ++batch)
            {
                notClosed += fuzzBatch(a.port(), 12, [&](RawPeer& p, int i) {
                    if (!p.helloExchange(fakeId, a.id, fakeKey))
                    {
                        ++helloFailed;
                        return false;
                    }
                    ++framesSent;
                    const ProtocolMessage confirm = p.confirmMessage(fakeId);
                    switch (batch == 0 && i < 4 ? 6 : static_cast<int>(g_rng.bounded(6)))
                    {
                        case 0: p.write(frameOf(randomBytes(1 + static_cast<int>(g_rng.bounded(200))))); return true;
                        case 1: // mã hóa bằng khóa của chiều NGƯỢC LẠI
                        {
                            QByteArray packet;
                            p.rxKey->encryptWithNonce(counterNonce(0), QByteArray(), confirm.toBytes(), &packet);
                            p.write(frameOf(packet));
                            return true;
                        }
                        case 2: p.write(frameOf(p.sessionPacket(confirm, 1 + g_rng.bounded(5)))); return true; // bộ đếm lệch
                        case 3: // giải mã được nhưng sai loại
                        {
                            ProtocolMessage wrong = confirm;
                            static const MessageType kWrong[] = {MessageType::Heartbeat, MessageType::ControlHandoff,
                                                                 MessageType::SessionHello, MessageType::PairAccept,
                                                                 MessageType::KeyEvent, MessageType::Goodbye};
                            wrong.type = kWrong[g_rng.bounded(6)];
                            p.sendSession(wrong);
                            return true;
                        }
                        case 4: // Confirm khai id khác
                        {
                            ProtocolMessage wrong = confirm;
                            wrong.textB = g_rng.bounded(2) ? b.id : QString();
                            p.sendSession(wrong);
                            return true;
                        }
                        case 5: p.write(frameOf(p.handshakePacket(confirm))); return true; // vẫn dùng khóa bắt tay
                        default: return true; // im lặng: phải bị đóng khi hết hạn bắt tay
                    }
                }, a.handshakeTimeoutMs + 4000);
            }
            CHECK(helloFailed == 0);
            CHECK(notClosed == 0);
            CHECK(waitUntil([&] { return a.pending() == 0; }, a.handshakeTimeoutMs + 3000));
            CHECK(a.ups.value(fakeId) == 0);
            CHECK(settled(a, b) && a.downs.value(b.id) == 0);
        }
        lap("7c cho SessionConfirm x360 ket noi");

        // (d) VƯỢT TRẦN kết nối chờ: 150 kết nối im lặng mở dồn. Số kết nối chờ không bao giờ vượt trần 16,
        //     cái cũ bị đẩy ra, và một máy thật vẫn bắt tay được giữa lúc đó; hết hạn thì sạch.
        {
            std::vector<std::unique_ptr<RawPeer>> idle;
            int maxPending = 0;
            for (int i = 0; i < 150; ++i)
            {
                idle.push_back(std::make_unique<RawPeer>());
                idle.back()->sock.connectToHost(QHostAddress::LocalHost, a.port());
                if (i % 10 == 9)
                {
                    pump(5);
                    maxPending = qMax(maxPending, a.pending());
                }
            }
            waitUntil([&] {
                for (const auto& p : idle)
                    if (!p->isOpen() && !p->closed)
                        return false;
                return true;
            }, 8000);
            pump(30);
            maxPending = qMax(maxPending, a.pending());
            CHECK(maxPending <= 16);
            int evicted = 0;
            for (const auto& p : idle)
                if (p->closed)
                    ++evicted;
            CHECK(evicted >= 150 - 16);
            // Giữa lúc hàng chờ đang đầy kết nối rác: máy giả thật vẫn bắt tay được.
            {
                RawPeer real;
                CHECK(real.establish(a.port(), fakeId, a.id, fakeKey));
                CHECK(waitUntil([&] { return a.c->isPeerConnected(fakeId); }));
                CHECK(real.probe());
                ProtocolMessage bye;
                bye.type = MessageType::Goodbye;
                real.sendSession(bye);
                CHECK(waitUntil([&] { return !a.c->isPeerConnected(fakeId); }));
            }
            // "Nhỏ giọt": 12 kết nối gửi preamble hợp lệ từng byte một - vẫn bị đóng đúng hạn bắt tay.
            std::vector<std::unique_ptr<RawPeer>> slow;
            for (int i = 0; i < 12; ++i)
            {
                slow.push_back(std::make_unique<RawPeer>());
                slow.back()->sock.connectToHost(QHostAddress::LocalHost, a.port());
            }
            const QByteArray drip = frameOf(ConnectProtocol::buildPreamble(ConnectProtocol::LinkPurpose::Session, fakeId));
            QElapsedTimer t;
            t.start();
            CHECK(waitUntil([&] {
                for (const auto& p : slow)
                    if (!p->isOpen())
                        return false;
                return true;
            }, 8000));
            int offset = 0;
            auto allSlowClosed = [&] {
                for (const auto& p : slow)
                    if (!p->closed)
                        return false;
                return true;
            };
            while (!allSlowClosed() && t.elapsed() < a.handshakeTimeoutMs + 5000)
            {
                if (offset < drip.size() - 1) // không bao giờ gửi byte cuối
                {
                    for (const auto& p : slow)
                        p->write(drip.mid(offset, 1));
                    ++offset;
                }
                pump(100);
            }
            CHECK(allSlowClosed());
            CHECK(t.elapsed() >= a.handshakeTimeoutMs - 300);
            CHECK(waitUntil([&] { return a.pending() == 0; }, a.handshakeTimeoutMs + 3000));
            for (const auto& p : idle)
                p->sock.abort();
            CHECK(settled(a, b) && a.downs.value(b.id) == 0);
        }
        lap("7d vuot tran + nho giot");

        // (e) PHÁT LẠI: ghi nguyên văn mọi byte của một lần bắt tay THÀNH CÔNG, rồi phát lại 200 lần từ kết
        //     nối khác - lúc phiên gốc còn sống và sau khi nó đã đóng. Không lần nào được đăng ký phiên,
        //     phiên gốc không bị ảnh hưởng. Cả byte A gửi ra (HelloAck/Confirm) dội ngược lại A cũng vô dụng.
        {
            a.ups.clear();
            a.downs.clear();
            RawPeer original;
            CHECK(original.establish(a.port(), fakeId, a.id, fakeKey));
            CHECK(waitUntil([&] { return a.c->isPeerConnected(fakeId); }));
            const QByteArray recorded = original.sent;
            CHECK(recorded.size() > 100);
            CHECK(original.probe());
            // preamble của máy giả + nguyên văn những gì A đã gửi về (HelloAck, Confirm, Heartbeat)
            const QByteArray reflected = original.sent.left(4 + original.preamble.size()) + original.received;
            int replayNotClosed = 0;
            for (int round = 0; round < 10; ++round)
            {
                replayNotClosed += fuzzBatch(a.port(), 10, [&](RawPeer& p, int i) {
                    ++framesSent;
                    if (i % 5 == 4)
                        p.write(reflected); // dội ngược byte của chính A
                    else if (i % 5 == 3)
                        p.write(recorded.left(static_cast<int>(g_rng.bounded(static_cast<int>(recorded.size())))) +
                                randomBytes(400)); // phát lại cắt cụt + rác (đủ dài để lấp đầy khung đang dở)
                    else
                        p.write(recorded);
                    return true;
                }, a.handshakeTimeoutMs + 4000);
                if (round == 4)
                {
                    CHECK(a.c->isPeerConnected(fakeId));
                    CHECK(original.probe()); // phiên gốc vẫn sống, bộ đếm không lệch
                    CHECK(a.ups.value(fakeId) == 1 && a.downs.value(fakeId) == 0);
                    original.sock.abort(); // nửa sau: phát lại khi phiên gốc đã mất
                    CHECK(waitUntil([&] { return !a.c->isPeerConnected(fakeId); }));
                }
            }
            CHECK(replayNotClosed == 0);
            CHECK(waitUntil([&] { return a.pending() == 0; }, a.handshakeTimeoutMs + 3000));
            CHECK(!a.c->isPeerConnected(fakeId));
            CHECK(a.ups.value(fakeId) == 1 && a.downs.value(fakeId) == 1);
            CHECK(settled(a, b) && a.downs.value(b.id) == 0);
        }
        lap("7e phat lai x100");

        // (f) Phiên ĐÃ XÁC THỰC nhận 30.000 thông điệp hợp lệ về mật mã nhưng nội dung "khó chịu" (số nguyên
        //     ở mọi biên, hướng/nút/mã phím vô nghĩa, văn bản dài, trường thừa): phiên không rớt, bộ đếm
        //     không lệch, mọi giá trị đưa xuống injector đều đã được kẹp, và khi máy giả thôi điều khiển thì
        //     không phím/nút nào còn bị giữ.
        {
            a.ups.clear();
            a.downs.clear();
            a.injector.reset();
            RawPeer peer;
            CHECK(peer.establish(a.port(), fakeId, a.id, fakeKey));
            CHECK(waitUntil([&] { return a.c->isPeerConnected(fakeId); }));
            for (int i = 0; i < 30000; ++i)
            {
                ProtocolMessage m;
                switch (g_rng.bounded(9))
                {
                    case 0: m.type = MessageType::MouseMove; break;
                    case 1: m.type = MessageType::MouseButton; break;
                    case 2: m.type = MessageType::MouseWheel; break;
                    case 3:
                    case 4: m.type = MessageType::KeyEvent; break;
                    case 5:
                        m.type = MessageType::ClipboardText;
                        m.textA = QString(static_cast<int>(g_rng.bounded(3000)), QChar(0x4E00 + g_rng.bounded(100)));
                        break;
                    case 6: m.type = MessageType::Heartbeat; break;
                    default: m.type = MessageType::ControlHandoff; break;
                }
                m.intA = wildInt();
                m.intB = wildInt();
                if (m.type == MessageType::ControlHandoff && g_rng.bounded(3))
                    m.intA = static_cast<int>(g_rng.bounded(7)) - 1; // -1..5: quanh khoảng hợp lệ 0..4
                if (m.type == MessageType::KeyEvent && g_rng.bounded(2))
                    m.intA = static_cast<int>(g_rng.bounded(260)) - 2;
                if (m.type == MessageType::KeyEvent && g_rng.bounded(2))
                    m.intB = static_cast<int>(g_rng.bounded(0x10010)) - 8;
                if (m.type == MessageType::MouseButton && g_rng.bounded(2))
                    m.intA = static_cast<int>(g_rng.bounded(8)) - 2;
                m.flagA = g_rng.bounded(2);
                m.flagB = g_rng.bounded(2);
                if (m.type == MessageType::Heartbeat)
                    m.flagA = g_rng.bounded(20) == 0; // thỉnh thoảng mới đòi trả lời
                if (g_rng.bounded(10) == 0)
                {
                    m.textB = QString(200, 'q');
                    m.longTermKey = randomBytes(64);
                    m.nonce = randomBytes(64);
                }
                peer.sendSession(m);
                ++framesSent;
                if (i % 500 == 499)
                    pump(1);
            }
            CHECK(!peer.closed);
            // Mốc "A đã xử lý HẾT 30.000 thông điệp": thôi điều khiển, nhận điều khiển lại, nhấn F12 - thông
            // điệp được xử lý đúng thứ tự nên F12 tới injector nghĩa là mọi thứ trước nó đã được xử lý.
            ProtocolMessage release;
            release.type = MessageType::ControlHandoff;
            release.intA = static_cast<int>(ScreenSide::None);
            ProtocolMessage take;
            take.type = MessageType::ControlHandoff;
            take.intA = static_cast<int>(ScreenSide::Top);
            ProtocolMessage key;
            key.type = MessageType::KeyEvent;
            key.intA = VK_F12;
            key.intB = 88;
            key.flagA = true;
            peer.sendSession(release);
            peer.sendSession(take);
            peer.sendSession(key);
            CHECK(waitUntil([&] { return a.injector.heldKeys.contains(VK_F12); }, 30000));
            CHECK(a.injector.heldKeys.size() == 1 && a.injector.heldButtons.isEmpty()); // 'release' đã nhả hết phím cũ
            CHECK(peer.probe(20000)); // mọi khung A gửi về đều giải mã được theo đúng thứ tự bộ đếm
            CHECK(a.c->isPeerConnected(fakeId));
            CHECK(a.downs.value(fakeId) == 0);
            CHECK(a.injector.outOfRange == 0);
            CHECK(a.injector.relativeMoves.size() > 100 && a.injector.keyEvents > 100); // có thật sự đi tới injector
            peer.sendSession(release);
            CHECK(waitUntil([&] { return a.c->currentRole() == ControlRole::Idle; }));
            CHECK(a.injector.heldKeys.isEmpty() && a.injector.heldButtons.isEmpty()); // F12 đang giữ được tự nhả
            CHECK(!a.c->isHookActive());
            // Thôi điều khiển rồi thì input của máy giả không còn tới injector nữa (mốc: nhận điều khiển lại)
            const int keysBefore = a.injector.keyEvents;
            key.intA = 0x41;
            peer.sendSession(key);
            peer.sendSession(take);
            CHECK(waitUntil([&] { return a.c->currentRole() == ControlRole::BeingControlled; }));
            CHECK(a.injector.keyEvents == keysBefore);
            peer.sendSession(release);
            CHECK(waitUntil([&] { return a.c->currentRole() == ControlRole::Idle; }));
            CHECK(settled(a, b) && a.downs.value(b.id) == 0);
            peer.sock.abort();
            CHECK(waitUntil([&] { return !a.c->isPeerConnected(fakeId); }));
        }
        lap("7f phien da xac thuc nhan 30000 thong diep kho chiu");

        // (g) Vi phạm trên phiên đã xác thực, mỗi kiểu 40 phiên mới: phát lại chính khung vừa gửi, rác, nhảy
        //     bộ đếm, loại thông điệp cấm (kể cả PairAccept hòng ghi đè khóa), Goodbye, độ dài vượt trần, đứt
        //     kết nối lúc đang điều khiển và còn giữ phím. Lần nào A cũng đóng phiên, nhả phím đã tiêm, và
        //     khóa của máy giả trong kho KHÔNG đổi. Cuối cùng: 100 lần "chiếm chỗ" (phiên mới cùng id thay
        //     phiên cũ) không rò phiên nào.
        {
            a.ups.clear();
            a.downs.clear();
            a.injector.reset();
            int establishFailed = 0, notDropped = 0, stuck = 0;
            for (int kind = 0; kind < 8; ++kind)
            {
                for (int round = 0; round < (kind == 7 ? 4 : 40); ++round)
                {
                    RawPeer peer;
                    if (!peer.establish(a.port(), fakeId, a.id, fakeKey) ||
                        !waitUntil([&] { return a.c->isPeerConnected(fakeId); }))
                    {
                        ++establishFailed;
                        continue;
                    }
                    ProtocolMessage m;
                    m.type = MessageType::Heartbeat;
                    ++framesSent;
                    switch (kind)
                    {
                        case 0: // phát lại ngay trong phiên
                            peer.sendSession(m);
                            peer.write(peer.lastSessionFrame);
                            break;
                        case 1: peer.write(frameOf(randomBytes(16 + static_cast<int>(g_rng.bounded(300))))); break;
                        case 2: // bỏ sót một số trong bộ đếm
                            ++peer.txCounter;
                            peer.sendSession(m);
                            break;
                        case 3:
                        {
                            static const MessageType kForbidden[] = {MessageType::PairRequest, MessageType::PairAccept,
                                                                     MessageType::PairReject, MessageType::SessionHello,
                                                                     MessageType::SessionHelloAck, MessageType::SessionConfirm};
                            m.type = kForbidden[round % 6];
                            m.textA = "Ke Mao Danh";
                            m.textB = round % 2 ? fakeId : b.id;
                            m.longTermKey = randomBytes(32);
                            m.nonce = randomBytes(32);
                            m.intA = ConnectProtocol::kVersion;
                            peer.sendSession(m);
                            break;
                        }
                        case 4:
                            m.type = MessageType::Goodbye;
                            peer.sendSession(m);
                            break;
                        case 5: peer.write(lengthPrefix(NetworkSession::kAuthenticatedMaxFrameBytes + 1)); break;
                        case 6: // đang điều khiển A, còn giữ phím + nút, rồi đứt hẳn
                        {
                            m.type = MessageType::ControlHandoff;
                            m.intA = static_cast<int>(ScreenSide::Left);
                            m.intB = 500;
                            peer.sendSession(m);
                            ProtocolMessage key;
                            key.type = MessageType::KeyEvent;
                            key.intA = VK_CONTROL;
                            key.intB = 29;
                            key.flagA = true;
                            peer.sendSession(key);
                            ProtocolMessage button;
                            button.type = MessageType::MouseButton;
                            button.intA = 1;
                            button.flagA = true;
                            peer.sendSession(button);
                            if (!waitUntil([&] {
                                    return a.c->currentRole() == ControlRole::BeingControlled &&
                                           a.injector.heldKeys.contains(VK_CONTROL) && a.injector.heldButtons.contains(1);
                                }))
                                ++stuck;
                            peer.sock.abort();
                            break;
                        }
                        default: // một khung rác đúng bằng trần 2 MB (A phải đọc hết rồi mới biết là rác)
                            peer.write(frameOf(randomBytes(NetworkSession::kAuthenticatedMaxFrameBytes)));
                            break;
                    }
                    if (!waitUntil([&] { return !a.c->isPeerConnected(fakeId); }, 10000))
                        ++notDropped;
                    if (kind != 6 && !peer.waitClosed())
                        ++notDropped;
                    if (a.c->currentRole() != ControlRole::Idle || !a.injector.heldKeys.isEmpty() ||
                        !a.injector.heldButtons.isEmpty())
                        ++stuck;
                }
            }
            CHECK(establishFailed == 0);
            CHECK(notDropped == 0);
            CHECK(stuck == 0);
            CHECK(a.ups.value(fakeId) == a.downs.value(fakeId));
            const PairedPeer* stored = a.store->find(fakeId);
            CHECK(stored != nullptr && stored->longTermKey == fakeKey);
            CHECK(a.keyFor(b) == b.keyFor(a)); // khóa của cặp A-B cũng không bị đụng tới

            // "Chiếm chỗ": phiên mới (cùng id, cùng chiều) thay phiên cũ - phiên cũ phải bị đóng.
            int takeoverFailures = 0;
            std::unique_ptr<RawPeer> current;
            for (int round = 0; round < 100; ++round)
            {
                auto next = std::make_unique<RawPeer>();
                if (!next->establish(a.port(), fakeId, a.id, fakeKey))
                    ++takeoverFailures;
                if (current && !current->waitClosed())
                    ++takeoverFailures;
                if (!a.c->isPeerConnected(fakeId) || !next->probe())
                    ++takeoverFailures;
                current = std::move(next);
            }
            CHECK(takeoverFailures == 0);
            pump(30);
            CHECK(a.liveSessions() == 2); // phiên với B + phiên với máy giả
            current->sock.abort();
            CHECK(waitUntil([&] { return !a.c->isPeerConnected(fakeId); }));
            CHECK(settled(a, b) && a.downs.value(b.id) == 0);
        }
        lap("7g vi pham tren phien da xac thuc x284 + chiem cho x100");

        // (h) Kết nối CHƯA XÁC THỰC không được phép cản một kết nối ĐÃ xác thực: một máy lạ chỉ cần mở kết
        //     nối tới B và khai id của A trong preamble (không cần khóa) rồi nằm im. Khi đó kết nối do chính
        //     B mở tới A (đã bắt tay xong, đã xác thực hai chiều) vẫn phải được B nhận.
        {
            a.c->setPeerAutoConnect(b.id, false); // chỉ B chủ động nối - để A không tự "cứu" bằng chiều ngược lại
            b.c->setPeerAutoConnect(a.id, false);
            b.c->stop();
            CHECK(waitUntil([&] { return !a.connectedTo(b); }));
            CHECK(b.start());
            b.pointAt(a);
            pump(50);
            CHECK(!b.connectedTo(a) && b.pending() == 0);
            RawPeer squatter;
            CHECK(squatter.open(b.port()));
            squatter.sendPreamble(ConnectProtocol::LinkPurpose::Session, a.id);
            CHECK(waitUntil([&] { return b.pending() == 1; }));
            a.ups.clear();
            a.downs.clear();
            b.c->setPeerAutoConnect(a.id, true); // B mở kết nối tới A ngay
            CHECK(waitUntil([&] { return b.connectedTo(a) && a.connectedTo(b); }, b.handshakeTimeoutMs - 500));
            pump(100);
            CHECK(b.connectedTo(a) && a.connectedTo(b));
            CHECK(a.downs.value(b.id) == 0); // không chập chờn: lên một lần và đứng vững
            CHECK(squatter.waitClosed());    // kết nối mạo danh bị dọn khi phiên thật đăng ký
            a.c->setPeerAutoConnect(b.id, true);
            CHECK(waitUntil([&] { return settled(a, b); }, 10000));
        }

        lap("7h ket noi chua xac thuc khong can ket noi da xac thuc");

        // (i) Chiều ngược lại - controller (B) là bên MỞ kết nối, "máy xấu" đã ghép đôi trả lời bậy ở từng
        //     bước: cắt ngay, rác, độ dài bịa, dội ngược chính SessionHello của B, HelloAck sai nonce/id/phiên
        //     bản, Confirm sai, sai loại (kể cả PairAccept hòng ghi đè khóa), im lặng. B chỉ được đăng ký
        //     phiên ở đúng hai kịch bản hợp lệ; mọi kịch bản khác kết nối phải được dọn, không rò.
        const QString evilId = "eeee-may-xau";
        const QByteArray evilKey = CryptoSession::generateRandomKey();
        EvilServer evil;
        CHECK(evil.listen(QHostAddress::LocalHost));
        b.store->addOrUpdate(makePeer(evilId, "May Xau", evilKey, evil.serverPort(), false));
        // Bảo B mở ĐÚNG MỘT kết nối tới máy xấu ngay bây giờ (bật tự kết nối = thử ngay), rồi tắt lại để B không
        // tự gõ cửa thêm lần nào ngoài kịch bản.
        auto dial = [&]() -> RawPeer* {
            const size_t before = evil.accepted.size();
            b.c->setPeerAutoConnect(evilId, true);
            b.c->setPeerAutoConnect(evilId, false);
            if (!waitUntil([&] { return evil.accepted.size() > before; }, 8000))
                return nullptr;
            return evil.accepted.back().get();
        };
        {
            b.ups.clear();
            b.downs.clear();
            int scriptFailed = 0, notCleaned = 0, wronglyUp = 0, notUp = 0, validUps = 0;
            for (int round = 0; round < 300; ++round)
            {
                const int kind = round < 17 ? round : static_cast<int>(g_rng.bounded(16)); // 16 = im lặng: chỉ một lần
                RawPeer* r = dial();
                if (!r)
                {
                    ++scriptFailed;
                    continue;
                }
                ++framesSent;
                ProtocolMessage ack;
                bool expectUp = false;
                if (kind == 0)
                    r->sock.abort();
                else if (kind == 1)
                    r->write(frameOf(randomBytes(1 + static_cast<int>(g_rng.bounded(300)))));
                else if (kind == 2)
                    r->write(lengthPrefix(NetworkSession::kUnauthenticatedMaxFrameBytes + 1 + g_rng.bounded(1000000)));
                else if (!r->acceptHello(evilId, b.id, evilKey, &ack))
                    ++scriptFailed;
                else
                {
                    switch (kind)
                    {
                        case 3: r->write(frameOf(randomBytes(60))); break;
                        case 4: r->write(r->firstEncryptedFrame); break; // dội ngược SessionHello của chính B
                        case 5: // không lặp lại đúng nonce của B
                            ack.nonce = randomBytes(ConnectProtocol::kNonceBytes) + ack.nonce.mid(ConnectProtocol::kNonceBytes);
                            r->sendHandshake(ack);
                            break;
                        case 6:
                        {
                            // lặp lại đúng nonce của B nhưng phần nonce của bên nhận sai cỡ (không bao giờ đúng 16 byte)
                            static const int kTail[] = {0, 1, 15, 17, 32, 2000};
                            ack.nonce = ack.nonce.left(ConnectProtocol::kNonceBytes) + randomBytes(kTail[g_rng.bounded(6)]);
                            r->sendHandshake(ack);
                            break;
                        }
                        case 7:
                            if (g_rng.bounded(2))
                                ack.textB = "id-khac";
                            else
                                ack.intA = g_rng.bounded(2) ? 1 : 3;
                            r->sendHandshake(ack);
                            break;
                        case 8: // HelloAck + Confirm của bên nhận gửi liền một hơi (không chờ Confirm của B): hợp lệ
                            r->write(frameOf(r->handshakePacket(ack)) + frameOf(r->sessionPacket(r->confirmMessage(evilId), 0)));
                            r->txCounter = 1;
                            expectUp = true;
                            break;
                        case 9:
                            r->sendHandshake(ack);
                            r->write(frameOf(randomBytes(40)));
                            break;
                        case 10:
                        {
                            ProtocolMessage confirm = r->confirmMessage(evilId);
                            confirm.textB = g_rng.bounded(2) ? b.id : QString("id-khac");
                            r->sendHandshake(ack);
                            r->sendSession(confirm);
                            break;
                        }
                        case 11:
                            r->sendHandshake(ack);
                            r->write(frameOf(r->sessionPacket(r->confirmMessage(evilId), 1 + g_rng.bounded(4))));
                            break;
                        case 12:
                        {
                            ProtocolMessage wrong;
                            wrong.type = g_rng.bounded(2) ? MessageType::Heartbeat : MessageType::ControlHandoff;
                            wrong.intA = 1;
                            r->sendHandshake(ack);
                            r->sendSession(wrong);
                            break;
                        }
                        case 13: // sai loại ngay ở bước HelloAck, kể cả PairAccept mang một khóa khác
                        {
                            static const MessageType kWrong[] = {MessageType::SessionHello, MessageType::PairAccept,
                                                                 MessageType::SessionConfirm, MessageType::PairReject,
                                                                 MessageType::Heartbeat, MessageType::Goodbye};
                            ack.type = kWrong[g_rng.bounded(6)];
                            ack.longTermKey = randomBytes(32);
                            r->sendHandshake(ack);
                            break;
                        }
                        case 14: // HelloAck hai lần
                            r->write(frameOf(r->handshakePacket(ack)) + frameOf(r->handshakePacket(ack)));
                            break;
                        case 15: // bắt tay ĐÚNG từng bước
                        {
                            ProtocolMessage confirm;
                            r->sendHandshake(ack);
                            if (r->readSession(&confirm) && confirm.type == MessageType::SessionConfirm && confirm.textB == b.id)
                                r->sendSession(r->confirmMessage(evilId));
                            else
                                ++scriptFailed;
                            expectUp = true;
                            break;
                        }
                        default: break; // im lặng sau SessionHello: B phải tự bỏ khi hết hạn bắt tay
                    }
                }
                if (expectUp)
                {
                    if (waitUntil([&] { return b.c->isPeerConnected(evilId); }, 5000))
                        ++validUps;
                    else
                        ++notUp;
                    if (round % 2)
                    {
                        ProtocolMessage bye;
                        bye.type = MessageType::Goodbye;
                        r->sendSession(bye);
                    }
                    else
                        r->sock.abort();
                    if (!waitUntil([&] { return !b.c->isPeerConnected(evilId) && b.pending() == 0; }, 5000))
                        ++notCleaned;
                }
                else
                {
                    if (!waitUntil([&] { return b.pending() == 0; }, b.handshakeTimeoutMs + 4000))
                        ++notCleaned;
                    if (b.c->isPeerConnected(evilId))
                        ++wronglyUp;
                }
                evil.accepted.clear();
            }
            if (scriptFailed || notCleaned || wronglyUp || notUp)
                std::printf("  [may xau, B mo ket noi] kich ban hong=%d khong don=%d len phien sai=%d khong len=%d\n", scriptFailed,
                            notCleaned, wronglyUp, notUp);
            CHECK(scriptFailed == 0);
            CHECK(notCleaned == 0);
            CHECK(wronglyUp == 0);
            CHECK(notUp == 0);
            CHECK(validUps >= 2);
            CHECK(b.ups.value(evilId) == validUps && b.downs.value(evilId) == validUps);
            CHECK(b.keyFor(a) == a.keyFor(b));
            const PairedPeer* stored = b.store->find(evilId);
            CHECK(stored != nullptr && stored->longTermKey == evilKey); // PairAccept lạc bước không ghi đè được khóa
            CHECK(settled(a, b) && b.downs.value(a.id) == 0);
        }
        lap("7i controller la ben mo ket noi, may kia tra loi bay x300");

        // (j) Controller là bên NHẬP MÃ (connectWithCode), máy tạo mã trả lời bậy. Mỗi lần bấm "Ghép đôi" phải
        //     cho ra ĐÚNG MỘT kết quả (một pairingFailed hoặc một pairingSucceeded - không bao giờ hai, không
        //     bao giờ không có gì), và chỉ PairAccept hợp lệ mới thêm được máy vào kho.
        {
            const QString evilCode = "135792468";
            const QString evilPairId = "eeee-ghep-doi";
            const QByteArray evilCodeKey = CryptoSession::deriveKeyFromPairingCode(evilCode);
            int scriptFailed = 0, wrongOutcome = 0, notCleaned = 0, storeWrong = 0, unsanitized = 0;
            for (int kind = 0; kind < 12; ++kind)
            {
                b.pairFailed.clear();
                const int okBefore = b.pairOk;
                const size_t before = evil.accepted.size();
                b.c->connectWithCode(QHostAddress::LocalHost, evil.serverPort(), evilCode);
                if (!waitUntil([&] { return evil.accepted.size() > before; }, 8000))
                {
                    ++scriptFailed;
                    continue;
                }
                RawPeer* r = evil.accepted.back().get();
                ++framesSent;
                ProtocolMessage request, reply;
                reply.type = MessageType::PairAccept;
                reply.textA = "May Xau";
                reply.textB = evilPairId;
                reply.longTermKey = randomBytes(32);
                reply.intA = ConnectProtocol::kVersion;
                reply.intB = evil.serverPort();
                bool expectOk = false;
                if (kind == 0)
                    r->sock.abort();
                else if (kind == 1)
                    r->write(frameOf(randomBytes(80)));
                else if (!r->acceptFirstMessage(ConnectProtocol::LinkPurpose::Pairing, b.id, evilCodeKey, &request) ||
                         request.type != MessageType::PairRequest || request.intB != b.port())
                    ++scriptFailed;
                else
                {
                    switch (kind)
                    {
                        case 2: // lý do từ chối chứa thẻ, ký tự điều khiển, ký tự đảo chiều, và rất dài
                            reply.type = MessageType::PairReject;
                            reply.textA = QString("<b>Ly do</b>\n\r\t") + QString(1500, QChar(0x202E)) + QString(380, 'x');
                            r->sendHandshake(reply);
                            break;
                        case 3: r->write(r->firstEncryptedFrame); break; // dội ngược PairRequest của chính B
                        case 4: reply.longTermKey = randomBytes(31); r->sendHandshake(reply); break;
                        case 5: reply.textB = b.id; r->sendHandshake(reply); break; // khai id của chính B
                        case 6: reply.textB = "id co khoang trang"; r->sendHandshake(reply); break;
                        case 7: reply.intA = 1; r->sendHandshake(reply); break;
                        case 8: reply.type = MessageType::SessionHelloAck; r->sendHandshake(reply); break;
                        case 9: // PairAccept hợp lệ hai lần liền
                            r->write(frameOf(r->handshakePacket(reply)) + frameOf(r->handshakePacket(reply)));
                            expectOk = true;
                            break;
                        case 10: break; // im lặng: B báo hết thời gian chờ
                        default: // PairAccept hợp lệ rồi tới rác
                            r->write(frameOf(r->handshakePacket(reply)) + frameOf(randomBytes(50)));
                            expectOk = true;
                            break;
                    }
                }
                auto outcomes = [&] { return static_cast<int>(b.pairFailed.size()) + (b.pairOk - okBefore); };
                waitUntil([&] { return outcomes() >= 1; }, b.handshakeTimeoutMs + 4000);
                pump(200); // một kết quả thứ hai (nếu có lỗi) sẽ kịp lộ ra
                if (outcomes() != 1 || (b.pairOk - okBefore == 1) != expectOk)
                    ++wrongOutcome;
                if (kind == 2 && !b.pairFailed.isEmpty())
                {
                    const QString reason = b.pairFailed.first();
                    if (reason.contains('<') || reason.contains('\n') || reason.contains(QChar(0x202E)) || reason.size() > 300 ||
                        !reason.contains("Ly do"))
                        ++unsanitized;
                }
                const PairedPeer* stored = b.store->find(evilPairId);
                if (expectOk ? (!stored || stored->longTermKey != reply.longTermKey) : stored != nullptr)
                    ++storeWrong;
                b.c->forgetPeer(evilPairId); // trước khi lần tự kết nối (hẹn 300 ms sau ghép đôi) kịp chạy
                if (!waitUntil([&] { return b.pending() == 0; }, b.handshakeTimeoutMs + 4000))
                    ++notCleaned;
                evil.accepted.clear();
            }
            if (scriptFailed || wrongOutcome || notCleaned || storeWrong || unsanitized)
                std::printf("  [may xau, B nhap ma] kich ban hong=%d sai ket qua=%d khong don=%d kho sai=%d ly do ban=%d\n",
                            scriptFailed, wrongOutcome, notCleaned, storeWrong, unsanitized);
            CHECK(scriptFailed == 0);
            CHECK(wrongOutcome == 0);
            CHECK(notCleaned == 0);
            CHECK(storeWrong == 0);
            CHECK(unsanitized == 0);
            CHECK(b.keyFor(a) == a.keyFor(b));
            CHECK(settled(a, b) && b.downs.value(a.id) == 0);
        }
        lap("7j controller la ben nhap ma, may tao ma tra loi bay x12");

        // (k) HẠN CHỜ trên phiên đã xác thực. B giữ phiên "ưu tiên" (do B mở, id B nhỏ hơn) tới máy xấu; máy
        //     xấu mở thêm một kết nối MỚI theo chiều ngược lại và bắt tay xong:
        //     - phiên cũ còn sống (trả lời Heartbeat dò): B giữ phiên cũ, bỏ kết nối mới;
        //     - phiên cũ đã chết mà TCP chưa báo (không trả lời): B bỏ phiên cũ sau hạn dò ~2,5 giây (không
        //       phải chờ đủ 10 giây im lặng), lần nối kế tiếp của máy kia thành công.
        //     Cuối cùng: B đang ĐIỀU KHIỂN máy kia thì máy kia im bặt (rút dây mạng) - sau hạn im lặng ~10 giây
        //     hook phải tắt, vai trò về Idle, không cần người dùng làm gì.
        {
            b.ups.clear();
            b.downs.clear();
            RawPeer* oldSession = dial();
            CHECK(oldSession != nullptr);
            if (oldSession)
            {
                CHECK(oldSession->acceptSession(evilId, b.id, evilKey));
                CHECK(waitUntil([&] { return b.c->isPeerConnected(evilId) && b.pending() == 0; }));
                auto openCompetingLink = [&](RawPeer& fresh) {
                    fresh.announcedPort = evil.serverPort();
                    return fresh.open(b.port()) && fresh.helloExchange(evilId, b.id, evilKey);
                };
                {
                    RawPeer fresh;
                    CHECK(openCompetingLink(fresh));
                    fresh.sendSession(fresh.confirmMessage(evilId));
                    CHECK(fresh.waitClosed()); // B từ chối kết nối mới...
                    bool probed = false;       // ...và dò phiên cũ
                    ProtocolMessage m;
                    QElapsedTimer t;
                    t.start();
                    while (!probed && t.elapsed() < 4000 && oldSession->readSession(&m, 4000))
                        probed = m.type == MessageType::Heartbeat && m.flagA;
                    CHECK(probed);
                    ProtocolMessage alive;
                    alive.type = MessageType::Heartbeat;
                    oldSession->sendSession(alive);
                    pump(3500); // quá hạn dò: đã trả lời nên phiên cũ KHÔNG bị đóng
                    CHECK(b.c->isPeerConnected(evilId));
                    CHECK(b.downs.value(evilId) == 0 && !oldSession->closed);
                }
                {
                    RawPeer fresh;
                    CHECK(openCompetingLink(fresh));
                    fresh.sendSession(fresh.confirmMessage(evilId));
                    QElapsedTimer t;
                    t.start();
                    CHECK(fresh.waitClosed());
                    CHECK(waitUntil([&] { return !b.c->isPeerConnected(evilId); }, 8000)); // phiên cũ không trả lời lần dò này
                    CHECK(t.elapsed() > 1800 && t.elapsed() < 7000);
                    CHECK(oldSession->waitClosed());
                }
                RawPeer controlled;
                controlled.announcedPort = evil.serverPort();
                CHECK(controlled.establish(b.port(), evilId, b.id, evilKey));
                CHECK(waitUntil([&] { return b.c->isPeerConnected(evilId); }));
                CHECK(b.c->requestControlForTesting(evilId, ScreenSide::Left));
                CHECK(b.c->currentRole() == ControlRole::Controlling && b.c->isHookActive());
                QElapsedTimer t;
                t.start(); // từ đây máy kia không gửi gì nữa
                CHECK(waitUntil([&] { return b.c->currentRole() == ControlRole::Idle; }, 20000));
                CHECK(t.elapsed() > 8000 && t.elapsed() < 15000);
                CHECK(!b.c->isHookActive());
                CHECK(!b.c->isPeerConnected(evilId));
                CHECK(controlled.waitClosed());
                CHECK(settled(a, b) && b.downs.value(a.id) == 0); // suốt lúc đó phiên A-B vẫn sống nhờ Heartbeat
            }
            b.c->forgetPeer(evilId);
            evil.accepted.clear();
            evil.close();
        }
        lap("7k han do phien cu + han im lang khi dang dieu khien");

        // Tổng kết mục 7: không rò phiên, bộ nhớ + số handle không phình sau hàng chục nghìn khung.
        pump(300);
        CHECK(a.pending() == 0 && b.pending() == 0);
        CHECK(a.liveSessions() == 1 && b.liveSessions() == 1);
        const qint64 memAfter = privateBytes();
        const int handlesAfter = handleCount();
        std::printf("  [fuzz] %d ket noi/khung bat thuong; bo nho rieng %+lld KB, handle %+d\n", framesSent,
                    static_cast<long long>((memAfter - memBefore) / 1024), handlesAfter - handlesBefore);
        CHECK(memAfter - memBefore < 64ll * 1024 * 1024);
        CHECK(handlesAfter - handlesBefore < 300);
    }

    // ================================================================================================
    section("8. sau tat ca: ket noi hop le van hoat dong");
    // ================================================================================================
    if (wantSection(8))
    {
        CHECK(waitUntil([&] { return settled(a, b); }, 10000));
        b.injector.reset();
        CHECK(a.c->requestControlForTesting(b.id, ScreenSide::Right));
        CHECK(waitUntil([&] { return b.c->currentRole() == ControlRole::BeingControlled; }));
        InputHook* hookA = a.c->hookForTesting();
        CHECK(hookA && !hookA->isInstalled()); // suốt bộ test không có hook toàn cục thật nào được cài
        hookA->onRawKey(0x41, 30, true, false);
        hookA->onRawKey(0x41, 30, false, false);
        CHECK(waitUntil([&] { return b.injector.keyEvents == 2; }));
        pressReturnHotkey(hookA);
        CHECK(waitUntil([&] { return b.c->currentRole() == ControlRole::Idle; }));

        // Kho trên đĩa nạp lại vẫn ra đúng khóa đang dùng (không bị các vòng lặp ở trên làm hỏng tệp).
        PeerStore reloadedA(a.storePath), reloadedB(b.storePath);
        const PairedPeer* pa = reloadedA.find(b.id);
        const PairedPeer* pb = reloadedB.find(a.id);
        CHECK(pa && pb && pa->longTermKey == pb->longTermKey && pa->longTermKey == a.keyFor(b));

        a.c->stop();
        b.c->stop();
        cNode.c->stop();
        pump(50);
        CHECK(a.liveSessions() == 0 && b.liveSessions() == 0 && cNode.liveSessions() == 0);
        CHECK(a.liveTimers() == 0 && b.liveTimers() == 0 && cNode.liveTimers() == 0);
        CHECK(!a.c->isHookActive() && !b.c->isHookActive() && !cNode.c->isHookActive());
    }

    section("");
    std::printf("tong thoi gian: %lld ms\n", static_cast<long long>(totalClock.elapsed()));
    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
