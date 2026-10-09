// Kiểm thử lõi Connect Together (mã ghép đôi, giao thức, mã hóa AES-GCM, bắt tay phiên, máy trạng thái
// trao quyền điều khiển). Chỉ dùng TCP loopback - không cần mạng/phần cứng/máy thứ hai.
//
// AN TOÀN CHO MÁY CHẠY TEST: không bao giờ tiêm input thật (ConnectSessionController được cấu hình dùng
// một InputInjector giả), không bao giờ cài hook toàn cục ở trạng thái active (controller trong test không
// cài hook; mục "vòng đời cài/gỡ hook thật" chỉ cài hook ở trạng thái trong suốt), không đụng clipboard
// thật, và mọi dữ liệu ghép đôi nằm trong một thư mục tạm (AppPaths::setDataDirOverride).
// Build: cmake --build build --target connect_tests && build\connect_tests.exe
#include <QGuiApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <QtEndian>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "core/AppPaths.h"
#include "tools/connect/engine/ConnectSessionController.h"
#include "tools/connect/engine/CryptoSession.h"
#include "tools/connect/engine/EdgeDetector.h"
#include "tools/connect/engine/InputHook.h"
#include "tools/connect/engine/InputInjector.h"
#include "tools/connect/engine/LocalIdentityStore.h"
#include "tools/connect/engine/NetworkSession.h"
#include "tools/connect/engine/PairingCode.h"
#include "tools/connect/engine/PeerDiscovery.h"
#include "tools/connect/engine/PeerStore.h"
#include "tools/connect/engine/ProtocolMessage.h"

/// Chạy vòng lặp sự kiện cho tới khi predicate() đúng hoặc hết thời gian chờ. Trả về true nếu đạt được
/// predicate trước timeout. Dùng cho test bất đồng bộ (TCP/UDP) mà không cần thêm Qt6::Test.
template <typename Func>
static bool waitUntil(Func predicate, int timeoutMs = 3000)
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
    poll.start(10);
    giveUp.start(timeoutMs);
    loop.exec();
    return predicate();
}

/// Bơm vòng lặp sự kiện đúng 'ms' mili giây (để khẳng định một việc KHÔNG xảy ra).
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

/// InputInjector giả: chỉ GHI NHẬN, không gọi SendInput/SetCursorPos - chuột/bàn phím của người đang ngồi
/// máy chạy test không bao giờ bị đụng tới.
struct FakeInjector : InputInjector
{
    struct Key { int vk; int scan; bool pressed; bool extended; };
    QList<QPoint> absoluteMoves;
    QList<QPoint> relativeMoves;
    QList<Key> keys;
    QList<QPair<int, bool>> buttons;
    int wheelEvents{0};

    void moveRelative(int dx, int dy) override { relativeMoves << QPoint(dx, dy); }
    void moveAbsolute(int x, int y) override { absoluteMoves << QPoint(x, y); }
    void mouseButton(int button, bool pressed) override { buttons << qMakePair(button, pressed); }
    void mouseWheel(int, int) override { ++wheelEvents; }
    void keyEvent(int vk, int scan, bool pressed, bool extended) override { keys << Key{vk, scan, pressed, extended}; }

    int countKey(int vk, bool pressed) const
    {
        int n = 0;
        for (const Key& k : keys)
            if (k.vk == vk && k.pressed == pressed)
                ++n;
        return n;
    }
};

/// Một cặp NetworkSession nối với nhau qua TCP loopback (bỏ qua preamble) - để thử đóng khung/mã hóa.
struct LoopPair
{
    QTcpServer server;
    NetworkSession initiator;
    NetworkSession responder;
    bool adopted{false};

    bool open()
    {
        if (!server.listen(QHostAddress::LocalHost))
            return false;
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            if (!adopted && server.hasPendingConnections())
            {
                responder.adoptSocket(server.nextPendingConnection(), false);
                adopted = true;
            }
        });
        initiator.connectToHost(QHostAddress::LocalHost, server.serverPort());
        return waitUntil([this] { return adopted && initiator.isConnected(); });
    }
};

/// Một máy đã ghép đôi GIẢ đóng vai bên mở kết nối: tự làm đủ bắt tay phiên (preamble -> SessionHello ->
/// SessionHelloAck -> suy khóa phiên -> SessionConfirm), độc lập với code của controller.
struct FakeSessionPeer
{
    NetworkSession session;
    QString myId;
    QString peerId;
    QString name{"May Gia"};
    QByteArray longTermKey;
    QByteArray nonceI;
    bool sendConfirm{true};
    bool gotHelloAck{false};
    bool established{false};
    bool closed{false};
    QList<ProtocolMessage> received;

    void connectTo(quint16 port)
    {
        QObject::connect(&session, &NetworkSession::connected, &session, [this] {
            const QByteArray preamble = ConnectProtocol::buildPreamble(ConnectProtocol::LinkPurpose::Session, myId);
            session.setHandshakeKey(longTermKey, preamble);
            session.sendPreamble(preamble);
            nonceI = CryptoSession::randomBytes(ConnectProtocol::kNonceBytes);
            ProtocolMessage hello;
            hello.type = MessageType::SessionHello;
            hello.textA = name;
            hello.textB = myId;
            hello.nonce = nonceI;
            hello.intA = ConnectProtocol::kVersion;
            hello.intB = 45678;
            session.sendMessage(hello);
        });
        QObject::connect(&session, &NetworkSession::messageReceived, &session, [this](ProtocolMessage m) {
            if (m.type == MessageType::SessionHelloAck && !gotHelloAck)
            {
                gotHelloAck = true;
                QByteArray i2r, r2i;
                CryptoSession::deriveSessionKeys(longTermKey, nonceI, m.nonce.mid(ConnectProtocol::kNonceBytes), myId,
                                                 peerId, &i2r, &r2i);
                session.setSessionKeys(i2r, r2i);
                if (sendConfirm)
                {
                    ProtocolMessage confirm;
                    confirm.type = MessageType::SessionConfirm;
                    confirm.textB = myId;
                    session.sendMessage(confirm);
                }
            }
            else if (m.type == MessageType::SessionConfirm)
            {
                established = true;
            }
            else
            {
                received << m;
            }
        });
        QObject::connect(&session, &NetworkSession::disconnected, &session, [this] { closed = true; });
        session.connectToHost(QHostAddress::LocalHost, port);
    }
};

static PairedPeer makePeer(const QString& id, const QString& name, const QByteArray& key, quint16 port)
{
    PairedPeer p;
    p.id = id;
    p.machineName = name;
    p.lastAddress = "127.0.0.1";
    p.lastPort = port;
    p.longTermKey = key;
    p.autoConnect = true;
    p.pairedAt = QDateTime::currentDateTime();
    return p;
}

int main(int argc, char** argv)
{
    // QGuiApplication (không chỉ QCoreApplication) vì ConnectSessionController gọi
    // QGuiApplication::clipboard() - gọi API đó khi app thật chỉ là QCoreApplication là hành vi không
    // xác định (UB).
    QGuiApplication app(argc, argv);

    // TRƯỚC KHI đụng tới bất kỳ store nào: ép toàn bộ dữ liệu vào thư mục tạm - test không được đọc/ghi
    // khóa ghép đôi + danh tính thật của người dùng trên máy đang chạy test.
    QTemporaryDir dataDir;
    CHECK(dataDir.isValid());
    AppPaths::setDataDirOverride(dataDir.path());

    // ---- PairingCode ----
    {
        const QString a = PairingCode::generate();
        const QString b = PairingCode::generate();
        CHECK(PairingCode::isValidFormat(a));
        CHECK(a.size() == PairingCode::kLength);
        CHECK(a != b); // xác suất trùng ~1e-9, coi như luôn khác

        CHECK(PairingCode::normalize("123 456 789") == "123456789");
        CHECK(PairingCode::normalize("123-456-789") == "123456789");
        CHECK(PairingCode::normalize("12345678") == ""); // thiếu 1 số -> không hợp lệ
        CHECK(PairingCode::normalize("1234567890") == ""); // thừa 1 số
        CHECK(PairingCode::normalize("12345678a") == "");
        CHECK(PairingCode::normalize("") == "");

        CHECK(PairingCode::formatForDisplay("123456789") == "123 456 789");
        CHECK(!PairingCode::isValidFormat("12345678"));
        CHECK(!PairingCode::isValidFormat("1234567a9"));

        // Sinh thử nhiều lần không bao giờ ra chuỗi không hợp lệ
        for (int i = 0; i < 200; ++i)
            CHECK(PairingCode::isValidFormat(PairingCode::generate()));
    }

    // ---- ProtocolMessage: round-trip mọi loại thông điệp ----
    {
        ProtocolMessage m;
        m.type = MessageType::PairRequest;
        m.textA = "MAY-CUA-AN";
        m.textB = "peer-id-123";
        ProtocolMessage out;
        CHECK(ProtocolMessage::fromBytes(m.toBytes(), &out));
        CHECK(out.type == MessageType::PairRequest && out.textA == m.textA && out.textB == m.textB);

        ProtocolMessage key;
        key.type = MessageType::PairAccept;
        key.textA = "MAY-CUA-BAN";
        key.longTermKey = CryptoSession::generateRandomKey();
        ProtocolMessage keyOut;
        CHECK(ProtocolMessage::fromBytes(key.toBytes(), &keyOut));
        CHECK(keyOut.longTermKey == key.longTermKey);
        CHECK(keyOut.longTermKey.size() == 32);

        ProtocolMessage mv;
        mv.type = MessageType::MouseMove;
        mv.intA = -1234;
        mv.intB = 5678;
        ProtocolMessage mvOut;
        CHECK(ProtocolMessage::fromBytes(mv.toBytes(), &mvOut));
        CHECK(mvOut.intA == -1234 && mvOut.intB == 5678);

        ProtocolMessage key2;
        key2.type = MessageType::KeyEvent;
        key2.intA = 65; // VK_A
        key2.intB = 30; // scancode
        key2.flagA = true;  // pressed
        key2.flagB = false; // not extended
        ProtocolMessage key2Out;
        CHECK(ProtocolMessage::fromBytes(key2.toBytes(), &key2Out));
        CHECK(key2Out.intA == 65 && key2Out.flagA == true && key2Out.flagB == false);

        ProtocolMessage clip;
        clip.type = MessageType::ClipboardText;
        clip.textA = QString::fromUtf8("Xin chào Việt Nam 🎉");
        ProtocolMessage clipOut;
        CHECK(ProtocolMessage::fromBytes(clip.toBytes(), &clipOut));
        CHECK(clipOut.textA == clip.textA);

        // Thông điệp bắt tay phiên bản 2: nonce đi/về nguyên vẹn, loại mới nằm trong khoảng hợp lệ
        ProtocolMessage ack;
        ack.type = MessageType::SessionHelloAck;
        ack.nonce = CryptoSession::randomBytes(32);
        ack.intA = ConnectProtocol::kVersion;
        ProtocolMessage ackOut;
        CHECK(ProtocolMessage::fromBytes(ack.toBytes(), &ackOut));
        CHECK(ackOut.type == MessageType::SessionHelloAck && ackOut.nonce == ack.nonce && ackOut.nonce.size() == 32);
        ProtocolMessage confirm;
        confirm.type = MessageType::SessionConfirm;
        ProtocolMessage confirmOut;
        CHECK(ProtocolMessage::fromBytes(confirm.toBytes(), &confirmOut));
        CHECK(confirmOut.type == MessageType::SessionConfirm);

        // Dữ liệu rác / rỗng không được làm crash, phải trả về false
        ProtocolMessage junk;
        CHECK(!ProtocolMessage::fromBytes(QByteArray(), &junk));
        CHECK(!ProtocolMessage::fromBytes(QByteArray("\xFF\xFE\xFD", 3), &junk));
        CHECK(!ProtocolMessage::fromBytes(QByteArray(1, '\x00'), &junk)); // type=0, ngoài enum hợp lệ
        CHECK(!ProtocolMessage::fromBytes(mv.toBytes() + QByteArray("thua"), &junk)); // thừa byte phía sau
        QByteArray badType = mv.toBytes();
        badType[0] = char(99); // loại ngoài khoảng
        CHECK(!ProtocolMessage::fromBytes(badType, &junk));
    }

    // ---- ConnectProtocol: preamble + kiểm id/tên máy ----
    {
        const QByteArray pre = ConnectProtocol::buildPreamble(ConnectProtocol::LinkPurpose::Session, "abc-123");
        int version = 0;
        ConnectProtocol::LinkPurpose purpose = ConnectProtocol::LinkPurpose::Pairing;
        QString id;
        CHECK(ConnectProtocol::parsePreamble(pre, &version, &purpose, &id));
        CHECK(version == ConnectProtocol::kVersion);
        CHECK(purpose == ConnectProtocol::LinkPurpose::Session);
        CHECK(id == "abc-123");
        CHECK(pre.size() <= NetworkSession::kMaxPreambleBytes);

        // Rác / gói của bản 1 (không có chữ ký "OFCT") -> false
        CHECK(!ConnectProtocol::parsePreamble(QByteArray(), &version, &purpose, &id));
        CHECK(!ConnectProtocol::parsePreamble(QByteArray(40, '\x7F'), &version, &purpose, &id));
        // Khác phiên bản: vẫn đọc được số phiên bản để báo cho rõ, không đọc phần sau
        QByteArray other = pre;
        other[4] = char(9);
        version = 0;
        CHECK(ConnectProtocol::parsePreamble(other, &version, &purpose, &id));
        CHECK(version == 9);
        // Mục đích lạ / độ dài id không khớp / id chứa ký tự lạ -> false
        QByteArray badPurpose = pre;
        badPurpose[5] = char(7);
        CHECK(!ConnectProtocol::parsePreamble(badPurpose, &version, &purpose, &id));
        CHECK(!ConnectProtocol::parsePreamble(pre + "x", &version, &purpose, &id));
        CHECK(!ConnectProtocol::parsePreamble(pre.left(pre.size() - 1), &version, &purpose, &id));

        CHECK(ConnectProtocol::isValidPeerId("3f2c1a9e-0b7d-4c55-9a10-aa00bb11cc22"));
        CHECK(!ConnectProtocol::isValidPeerId(""));
        CHECK(!ConnectProtocol::isValidPeerId(QString(ConnectProtocol::kMaxIdChars + 1, 'a')));
        CHECK(!ConnectProtocol::isValidPeerId("co khoang trang"));
        CHECK(!ConnectProtocol::isValidPeerId(QString::fromUtf8("mã-việt")));

        CHECK(ConnectProtocol::sanitizeMachineName(QString::fromUtf8("Máy Bên Trái")) == QString::fromUtf8("Máy Bên Trái"));
        CHECK(ConnectProtocol::sanitizeMachineName("a\nb<i>c</i>") == "abic/i");
        CHECK(ConnectProtocol::sanitizeMachineName(QString(500, 'x')).size() == ConnectProtocol::kMaxNameChars);
        CHECK(!ConnectProtocol::sanitizeMachineName("").isEmpty());
    }

    // ---- CryptoSession: round-trip, phát hiện sai khóa, phát hiện bị sửa dữ liệu ----
    {
        const QByteArray keyA = CryptoSession::generateRandomKey();
        const QByteArray keyB = CryptoSession::generateRandomKey();
        CHECK(keyA.size() == 32 && keyB.size() == 32);
        CHECK(keyA != keyB);
        CHECK(CryptoSession::randomBytes(13).size() == 13); // không còn bắt buộc bội số của 4
        CHECK(CryptoSession::randomBytes(0).isEmpty());

        CryptoSession sessA(keyA);
        CryptoSession sessA2(keyA); // mô phỏng máy B dùng ĐÚNG khóa giống máy A
        CryptoSession sessWrong(keyB);
        CHECK(sessA.isValid() && sessWrong.isValid());
        CHECK(!CryptoSession(QByteArray(31, 'k')).isValid()); // khóa sai kích thước

        const QByteArray plain = QString::fromUtf8("Thông điệp bí mật giữa 2 máy 🔒").toUtf8();

        QByteArray packet;
        QString err;
        CHECK(sessA.encrypt(plain, &packet, &err));
        CHECK(!packet.isEmpty());
        CHECK(packet.size() >= plain.size() + 12 + 16);

        QByteArray decrypted;
        CHECK(sessA2.decrypt(packet, &decrypted, &err)); // đúng khóa -> giải mã được
        CHECK(decrypted == plain);

        QByteArray failedDecrypt;
        QString wrongErr;
        CHECK(!sessWrong.decrypt(packet, &failedDecrypt, &wrongErr)); // sai khóa -> phải thất bại rõ ràng
        CHECK(!wrongErr.isEmpty());

        // Sửa 1 byte trong phần bản mã -> tag không khớp -> giải mã thất bại (phát hiện can thiệp)
        QByteArray tampered = packet;
        tampered[tampered.size() / 2] = tampered[tampered.size() / 2] ^ 0xFF;
        QByteArray tamperedOut;
        CHECK(!sessA2.decrypt(tampered, &tamperedOut, &err));

        // Gói tin quá ngắn không được crash
        QByteArray tooShort;
        CHECK(!sessA2.decrypt(QByteArray("abc"), &tooShort, &err));
        CHECK(!sessA2.decrypt(QByteArray(), &tooShort, &err));

        // Mỗi lần mã hóa dùng nonce khác nhau -> 2 gói tin của cùng nội dung phải khác nhau
        QByteArray packet2;
        CHECK(sessA.encrypt(plain, &packet2, &err));
        CHECK(packet != packet2);
        QByteArray decrypted2;
        CHECK(sessA2.decrypt(packet2, &decrypted2, &err));
        CHECK(decrypted2 == plain);

        // Bản tin rỗng vẫn round-trip được
        QByteArray emptyPacket, emptyOut;
        CHECK(sessA.encrypt(QByteArray(), &emptyPacket, &err));
        CHECK(sessA2.decrypt(emptyPacket, &emptyOut, &err));
        CHECK(emptyOut.isEmpty());

        // Bản tin lớn (1MB) round-trip được (mô phỏng dữ liệu clipboard lớn)
        QByteArray big(1024 * 1024, 'x');
        QByteArray bigPacket, bigOut;
        CHECK(sessA.encrypt(big, &bigPacket, &err));
        CHECK(sessA2.decrypt(bigPacket, &bigOut, &err));
        CHECK(bigOut == big);

        // Dữ liệu kèm theo (AAD): phải khớp ở hai đầu - đây là thứ chặn gói bắt tay bị dội ngược về bên gửi
        QByteArray aadPacket, aadOut;
        CHECK(sessA.encrypt(plain, &aadPacket, &err, QByteArray("chieu-I")));
        CHECK(sessA2.decrypt(aadPacket, &aadOut, &err, QByteArray("chieu-I")));
        CHECK(aadOut == plain);
        CHECK(!sessA2.decrypt(aadPacket, &aadOut, &err, QByteArray("chieu-R")));
        CHECK(!sessA2.decrypt(aadPacket, &aadOut, &err));

        // Nonce do nơi gọi cấp (bộ đếm của khóa phiên): sai nonce = gói phát lại/lệch thứ tự -> thất bại
        const QByteArray n0(12, '\0');
        QByteArray n1(12, '\0');
        n1[11] = 1;
        QByteArray ct, pt;
        CHECK(sessA.encryptWithNonce(n0, QByteArray(), plain, &ct, &err));
        CHECK(ct.size() == plain.size() + 16); // nonce KHÔNG nằm trong gói
        CHECK(sessA2.decryptWithNonce(n0, QByteArray(), ct, &pt, &err));
        CHECK(pt == plain);
        CHECK(!sessA2.decryptWithNonce(n1, QByteArray(), ct, &pt, &err));
        CHECK(!sessA.encryptWithNonce(QByteArray(11, '\0'), QByteArray(), plain, &ct, &err)); // nonce sai kích thước
    }

    // ---- CryptoSession: HMAC-SHA256 (CNG), suy khóa phiên, DPAPI ----
    {
        // RFC 4231, test case 1
        const QByteArray mac = CryptoSession::hmacSha256(QByteArray(20, '\x0b'), QByteArray("Hi There"));
        CHECK(mac.toHex() == "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");

        const QByteArray ltk = CryptoSession::generateRandomKey();
        const QByteArray nI = CryptoSession::randomBytes(16);
        const QByteArray nR = CryptoSession::randomBytes(16);
        QByteArray i2r, r2i, i2rAgain, r2iAgain;
        CHECK(CryptoSession::deriveSessionKeys(ltk, nI, nR, "id-a", "id-b", &i2r, &r2i));
        CHECK(i2r.size() == 32 && r2i.size() == 32);
        CHECK(i2r != r2i);                 // mỗi chiều một khóa (chống phản xạ)
        CHECK(i2r != ltk && r2i != ltk);
        CHECK(CryptoSession::deriveSessionKeys(ltk, nI, nR, "id-a", "id-b", &i2rAgain, &r2iAgain));
        CHECK(i2r == i2rAgain && r2i == r2iAgain); // hai máy tính độc lập ra cùng kết quả

        QByteArray k1, k2;
        CHECK(CryptoSession::deriveSessionKeys(ltk, nI, CryptoSession::randomBytes(16), "id-a", "id-b", &k1, &k2));
        CHECK(k1 != i2r && k2 != r2i);     // nonce bên nhận khác -> khóa phiên khác (chống phát lại)
        CHECK(CryptoSession::deriveSessionKeys(ltk, CryptoSession::randomBytes(16), nR, "id-a", "id-b", &k1, &k2));
        CHECK(k1 != i2r && k2 != r2i);
        CHECK(CryptoSession::deriveSessionKeys(ltk, nI, nR, "id-b", "id-a", &k1, &k2));
        CHECK(k1 != i2r && k2 != r2i);     // đổi vai hai máy -> khóa khác
        CHECK(!CryptoSession::deriveSessionKeys(QByteArray(8, 'k'), nI, nR, "id-a", "id-b", &k1, &k2));
        CHECK(!CryptoSession::deriveSessionKeys(ltk, QByteArray(), nR, "id-a", "id-b", &k1, &k2));

        // DPAPI: bọc/mở được trong cùng tài khoản; blob không chứa khóa thô; blob bị sửa thì không mở được
        const QByteArray blob = CryptoSession::protectForCurrentUser(ltk);
        CHECK(!blob.isEmpty());
        CHECK(!blob.contains(ltk));
        CHECK(CryptoSession::unprotectForCurrentUser(blob) == ltk);
        QByteArray brokenBlob = blob;
        brokenBlob[brokenBlob.size() - 3] = brokenBlob[brokenBlob.size() - 3] ^ 0x5A;
        CHECK(CryptoSession::unprotectForCurrentUser(brokenBlob).isEmpty());
        CHECK(CryptoSession::unprotectForCurrentUser(QByteArray("khong phai blob")).isEmpty());
        CHECK(CryptoSession::protectForCurrentUser(QByteArray()).isEmpty());
    }

    // ---- CryptoSession::deriveKeyFromPairingCode: cùng mã -> cùng khóa, khác mã -> khác khóa ----
    {
        const QByteArray k1 = CryptoSession::deriveKeyFromPairingCode("123456789");
        const QByteArray k1b = CryptoSession::deriveKeyFromPairingCode("123456789");
        const QByteArray k2 = CryptoSession::deriveKeyFromPairingCode("987654321");
        CHECK(k1.size() == 32);
        CHECK(k1 == k1b);
        CHECK(k1 != k2);

        // Hai máy nhập cùng mã ghép đôi phải mã hóa/giải mã được cho nhau
        CryptoSession sA(k1);
        CryptoSession sB(k1b);
        QByteArray pkt, out;
        QString err;
        CHECK(sA.encrypt(QByteArray("hello"), &pkt, &err));
        CHECK(sB.decrypt(pkt, &out, &err));
        CHECK(out == QByteArray("hello"));

        // Hai máy nhập KHÁC mã ghép đôi không giải mã được cho nhau (chặn ghép đôi nhầm/dò mã)
        CryptoSession sC(k2);
        QByteArray failOut;
        CHECK(!sC.decrypt(pkt, &failOut, &err));
    }

    // ---- PeerStore: lưu/nạp lại, khóa dài hạn được bọc DPAPI, di trú tệp cũ ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        PeerStore::instance().setFilePath(tmp.filePath("peers.json"));
        PeerStore::instance().clear();

        PairedPeer p1;
        p1.id = "peer-aaa";
        p1.machineName = "May A";
        p1.lastAddress = "192.168.1.10";
        p1.lastPort = 54321;
        p1.longTermKey = CryptoSession::generateRandomKey();
        p1.side = ScreenSide::Right;
        p1.autoConnect = true;
        p1.pairedAt = QDateTime::currentDateTime();
        CHECK(p1.isValid());

        PairedPeer p2 = p1;
        p2.id = "peer-bbb";
        p2.machineName = QString::fromUtf8("Máy Bên Trái");
        p2.side = ScreenSide::Left;
        p2.longTermKey = CryptoSession::generateRandomKey();

        PeerStore::instance().addOrUpdate(p1);
        PeerStore::instance().addOrUpdate(p2);
        CHECK(PeerStore::instance().peers().size() == 2);

        PairedPeer* found = PeerStore::instance().find("peer-aaa");
        CHECK(found != nullptr && found->machineName == "May A");
        CHECK(PeerStore::instance().find("khong-ton-tai") == nullptr);

        // Cập nhật (cùng id) phải ghi đè, không tạo bản ghi mới
        PairedPeer p1updated = p1;
        p1updated.machineName = "May A (doi ten)";
        PeerStore::instance().addOrUpdate(p1updated);
        CHECK(PeerStore::instance().peers().size() == 2);
        CHECK(PeerStore::instance().find("peer-aaa")->machineName == "May A (doi ten)");

        // Tệp trên đĩa KHÔNG được chứa khóa thô (Base64) - chỉ có blob DPAPI
        {
            QFile onDisk(tmp.filePath("peers.json"));
            CHECK(onDisk.open(QIODevice::ReadOnly));
            const QByteArray raw = onDisk.readAll();
            CHECK(raw.contains("longTermKeyProtected"));
            CHECK(!raw.contains("\"longTermKey\""));
            CHECK(!raw.contains(p1.longTermKey.toBase64()));
            CHECK(!raw.contains(p2.longTermKey.toBase64()));
        }

        // Nạp lại từ file phải khớp dữ liệu đã lưu, kể cả khóa nhị phân 32 byte
        CHECK(PeerStore::instance().load());
        CHECK(PeerStore::instance().peers().size() == 2);
        PairedPeer* reloaded = PeerStore::instance().find("peer-bbb");
        CHECK(reloaded != nullptr);
        CHECK(reloaded->longTermKey == p2.longTermKey);
        CHECK(reloaded->longTermKey.size() == 32);
        CHECK(reloaded->side == ScreenSide::Left);
        CHECK(reloaded->machineName == QString::fromUtf8("Máy Bên Trái"));

        // updateEndpoint: ghi nhận địa chỉ/cổng/tên đã xác nhận; giá trị rỗng/0 không ghi đè
        PeerStore::instance().updateEndpoint("peer-bbb", "10.0.0.7", 40001, "Ten Moi");
        CHECK(PeerStore::instance().find("peer-bbb")->lastAddress == "10.0.0.7");
        CHECK(PeerStore::instance().find("peer-bbb")->lastPort == 40001);
        PeerStore::instance().updateEndpoint("peer-bbb", QString(), 0, QString());
        CHECK(PeerStore::instance().find("peer-bbb")->lastAddress == "10.0.0.7");
        CHECK(PeerStore::instance().find("peer-bbb")->machineName == "Ten Moi");
        CHECK(PeerStore::instance().load());
        CHECK(PeerStore::instance().find("peer-bbb")->lastPort == 40001);

        PeerStore::instance().remove("peer-aaa");
        CHECK(PeerStore::instance().peers().size() == 1);
        CHECK(PeerStore::instance().find("peer-aaa") == nullptr);

        PeerStore::instance().clear();
        CHECK(PeerStore::instance().peers().isEmpty());

        // File không tồn tại / hỏng -> load() trả false, không crash
        PeerStore::instance().setFilePath(tmp.filePath("khong_ton_tai.json"));
        CHECK(!PeerStore::instance().load());
        QFile bad(tmp.filePath("hong.json"));
        CHECK(bad.open(QIODevice::WriteOnly));
        bad.write("{ day khong phai json hop le");
        bad.close();
        PeerStore::instance().setFilePath(tmp.filePath("hong.json"));
        CHECK(!PeerStore::instance().load());

        // Tệp của bản cũ (khóa Base64 thô): vẫn đọc được, và được ghi lại NGAY sang dạng đã bảo vệ
        const QByteArray legacyKey = CryptoSession::generateRandomKey();
        QJsonObject legacyPeer;
        legacyPeer["id"] = "peer-cu";
        legacyPeer["machineName"] = "May Ban Cu";
        legacyPeer["lastAddress"] = "192.168.1.77";
        legacyPeer["lastPort"] = 47632;
        legacyPeer["longTermKey"] = QString::fromLatin1(legacyKey.toBase64());
        legacyPeer["side"] = 99; // giá trị ngoài enum trong tệp bị sửa tay -> về None, không dùng bừa
        QJsonObject legacyRoot;
        legacyRoot["version"] = "1.0";
        legacyRoot["peers"] = QJsonArray{legacyPeer};
        const QString legacyPath = tmp.filePath("legacy.json");
        {
            QFile legacyFile(legacyPath);
            CHECK(legacyFile.open(QIODevice::WriteOnly));
            legacyFile.write(QJsonDocument(legacyRoot).toJson());
        }
        {
            PeerStore legacyStore(legacyPath);
            CHECK(legacyStore.peers().size() == 1);
            CHECK(legacyStore.find("peer-cu") != nullptr && legacyStore.find("peer-cu")->longTermKey == legacyKey);
            CHECK(legacyStore.find("peer-cu")->side == ScreenSide::None);
            QFile migrated(legacyPath);
            CHECK(migrated.open(QIODevice::ReadOnly));
            const QByteArray raw = migrated.readAll();
            CHECK(raw.contains("longTermKeyProtected"));
            CHECK(!raw.contains(legacyKey.toBase64()));
        }
        {
            PeerStore again(legacyPath); // đọc lại bản đã di trú
            CHECK(again.find("peer-cu") != nullptr && again.find("peer-cu")->longTermKey == legacyKey);
        }

        PeerStore::instance().setFilePath(tmp.filePath("peers.json"));
        PeerStore::instance().load();
        PeerStore::instance().clear();
    }

    // ---- NetworkSession: TCP loopback thật, giai đoạn bắt tay (nonce ngẫu nhiên + AAD theo chiều) ----
    {
        LoopPair pair;
        CHECK(pair.open());
        const QByteArray sharedKey = CryptoSession::generateRandomKey();
        pair.initiator.setHandshakeKey(sharedKey, "ngu-canh");
        pair.responder.setHandshakeKey(sharedKey, "ngu-canh");
        CHECK(pair.initiator.side() == NetworkSession::Side::Initiator);
        CHECK(pair.responder.side() == NetworkSession::Side::Responder);

        // Gửi 1 chiều: bên mở -> bên nhận
        QList<ProtocolMessage> serverReceived;
        QObject::connect(&pair.responder, &NetworkSession::messageReceived,
                         [&](ProtocolMessage m) { serverReceived.push_back(m); });

        ProtocolMessage mv;
        mv.type = MessageType::MouseMove;
        mv.intA = 42;
        mv.intB = -7;
        QString sendErr;
        CHECK(pair.initiator.sendMessage(mv, &sendErr));
        CHECK(waitUntil([&] { return !serverReceived.isEmpty(); }));
        CHECK(serverReceived.first().type == MessageType::MouseMove);
        CHECK(serverReceived.first().intA == 42 && serverReceived.first().intB == -7);

        // Gửi chiều ngược lại: bên nhận -> bên mở
        QList<ProtocolMessage> clientReceived;
        QObject::connect(&pair.initiator, &NetworkSession::messageReceived,
                         [&](ProtocolMessage m) { clientReceived.push_back(m); });

        ProtocolMessage clip;
        clip.type = MessageType::ClipboardText;
        clip.textA = QString::fromUtf8("Đồng bộ clipboard 2 máy 📋");
        CHECK(pair.responder.sendMessage(clip, &sendErr));
        CHECK(waitUntil([&] { return !clientReceived.isEmpty(); }));
        CHECK(clientReceived.first().textA == clip.textA);

        // Chưa có khóa phiên: thông điệp lớn bị TỪ CHỐI ngay ở phía gửi (không làm rớt kết nối)
        ProtocolMessage tooBigForHandshake;
        tooBigForHandshake.type = MessageType::ClipboardText;
        tooBigForHandshake.textA = QString(NetworkSession::kUnauthenticatedMaxFrameBytes, QChar('A'));
        sendErr.clear();
        CHECK(!pair.initiator.sendMessage(tooBigForHandshake, &sendErr));
        CHECK(!sendErr.isEmpty());
        CHECK(pair.initiator.isConnected());

        // Đóng kết nối: đầu kia phải thấy disconnected
        bool serverSawDisconnect = false;
        QObject::connect(&pair.responder, &NetworkSession::disconnected, [&] { serverSawDisconnect = true; });
        pair.initiator.disconnectSession();
        CHECK(waitUntil([&] { return !pair.initiator.isConnected(); }));
        CHECK(waitUntil([&] { return serverSawDisconnect; }));
    }

    // ---- NetworkSession: giai đoạn phiên (khóa riêng mỗi chiều + bộ đếm) - khung lớn, gửi dồn dập ----
    {
        LoopPair pair;
        CHECK(pair.open());
        const QByteArray keyI2R = CryptoSession::generateRandomKey();
        const QByteArray keyR2I = CryptoSession::generateRandomKey();
        pair.initiator.setSessionKeys(keyI2R, keyR2I);
        pair.responder.setSessionKeys(keyR2I, keyI2R);
        CHECK(pair.initiator.hasSessionKeys() && pair.responder.hasSessionKeys());

        QList<ProtocolMessage> serverReceived, clientReceived;
        QObject::connect(&pair.responder, &NetworkSession::messageReceived,
                         [&](ProtocolMessage m) { serverReceived.push_back(m); });
        QObject::connect(&pair.initiator, &NetworkSession::messageReceived,
                         [&](ProtocolMessage m) { clientReceived.push_back(m); });

        // Payload lớn (1 MB văn bản - đúng trần clipboard, vượt xa 1 gói TCP) phải ráp khung đúng
        QString sendErr;
        ProtocolMessage bigMsg;
        bigMsg.type = MessageType::ClipboardText;
        bigMsg.textA = QString(ConnectProtocol::kMaxClipboardBytes / 2, QChar('A'));
        CHECK(pair.initiator.sendMessage(bigMsg, &sendErr));
        CHECK(waitUntil([&] { return !serverReceived.isEmpty(); }, 6000));
        CHECK(!serverReceived.isEmpty() && serverReceived.first().textA == bigMsg.textA);

        // Nhiều thông điệp liên tiếp không bị trộn khung, đúng thứ tự (bộ đếm hai đầu khớp nhau)
        serverReceived.clear();
        for (int i = 0; i < 50; ++i)
        {
            ProtocolMessage k;
            k.type = MessageType::KeyEvent;
            k.intA = i;
            CHECK(pair.initiator.sendMessage(k, &sendErr));
        }
        CHECK(waitUntil([&] { return serverReceived.size() == 50; }));
        for (int i = 0; i < serverReceived.size() && i < 50; ++i)
            CHECK(serverReceived[i].intA == i);

        // Chiều ngược lại dùng khóa + bộ đếm riêng
        ProtocolMessage back;
        back.type = MessageType::Heartbeat;
        CHECK(pair.responder.sendMessage(back, &sendErr));
        CHECK(waitUntil([&] { return !clientReceived.isEmpty(); }));

        // Vượt trần khung: từ chối ở phía GỬI, phiên vẫn sống và bộ đếm không lệch (gói kế tiếp vẫn tới)
        ProtocolMessage tooBig;
        tooBig.type = MessageType::ClipboardText;
        tooBig.textA = QString(NetworkSession::kAuthenticatedMaxFrameBytes, QChar('B')); // 2 byte/ký tự -> gấp đôi trần
        sendErr.clear();
        CHECK(!pair.initiator.sendMessage(tooBig, &sendErr));
        CHECK(!sendErr.isEmpty());
        CHECK(pair.initiator.isConnected());
        serverReceived.clear();
        ProtocolMessage after;
        after.type = MessageType::KeyEvent;
        after.intA = 777;
        CHECK(pair.initiator.sendMessage(after, &sendErr));
        CHECK(waitUntil([&] { return !serverReceived.isEmpty(); }));
        CHECK(!serverReceived.isEmpty() && serverReceived.first().intA == 777);
    }

    // ---- NetworkSession: sai khóa -> báo decryptFailed, không phát thông điệp, ĐÓNG kết nối ----
    {
        LoopPair pair;
        CHECK(pair.open());
        pair.initiator.setHandshakeKey(CryptoSession::generateRandomKey()); // cố tình khác khóa
        pair.responder.setHandshakeKey(CryptoSession::generateRandomKey());

        int received = 0, decryptFailures = 0, errors = 0;
        bool responderClosed = false;
        QObject::connect(&pair.responder, &NetworkSession::messageReceived, [&](ProtocolMessage) { ++received; });
        QObject::connect(&pair.responder, &NetworkSession::decryptFailed, [&] { ++decryptFailures; });
        QObject::connect(&pair.responder, &NetworkSession::errorOccurred, [&](const QString&) { ++errors; });
        QObject::connect(&pair.responder, &NetworkSession::disconnected, [&] { responderClosed = true; });

        ProtocolMessage probe;
        probe.type = MessageType::Heartbeat;
        QString sendErr;
        CHECK(pair.initiator.sendMessage(probe, &sendErr));
        CHECK(waitUntil([&] { return decryptFailures == 1; }));
        CHECK(received == 0);            // gói sai khóa KHÔNG được chấp nhận
        CHECK(errors >= 1);
        CHECK(waitUntil([&] { return responderClosed; }));
        CHECK(!pair.responder.isConnected());
        CHECK(pair.server.isListening()); // server vẫn sống sau khi nhận gói sai khóa
    }

    // ---- NetworkSession: khung giả/dữ liệu rác gửi thẳng qua socket TCP thô ----
    {
        // (a) 4 byte độ dài khai quá lớn so với một preamble -> đóng ngay, không đệm chờ dữ liệu
        {
            QTcpServer server;
            CHECK(server.listen(QHostAddress::LocalHost));
            NetworkSession victim;
            bool adopted = false, preambleSeen = false, errorSeen = false;
            QObject::connect(&server, &QTcpServer::newConnection, [&] {
                victim.adoptSocket(server.nextPendingConnection()); // chờ preamble như controller thật
                adopted = true;
            });
            QObject::connect(&victim, &NetworkSession::preambleReceived, [&](QByteArray) { preambleSeen = true; });
            QObject::connect(&victim, &NetworkSession::errorOccurred, [&](const QString&) { errorSeen = true; });

            QTcpSocket rogue;
            rogue.connectToHost(QHostAddress::LocalHost, server.serverPort());
            CHECK(waitUntil([&] { return adopted && rogue.state() == QAbstractSocket::ConnectedState; }));
            rogue.write(QByteArray::fromHex("00FFFFFF")); // "khung" 16 MB
            rogue.flush();
            CHECK(waitUntil([&] { return errorSeen; }));
            CHECK(!preambleSeen);
            CHECK(waitUntil([&] { return rogue.state() == QAbstractSocket::UnconnectedState; })); // bị đóng thật
            CHECK(server.isListening());
        }
        // (b) độ dài 0 -> đóng
        {
            QTcpServer server;
            CHECK(server.listen(QHostAddress::LocalHost));
            NetworkSession victim;
            bool adopted = false, errorSeen = false;
            QObject::connect(&server, &QTcpServer::newConnection, [&] {
                victim.adoptSocket(server.nextPendingConnection());
                adopted = true;
            });
            QObject::connect(&victim, &NetworkSession::errorOccurred, [&](const QString&) { errorSeen = true; });
            QTcpSocket rogue;
            rogue.connectToHost(QHostAddress::LocalHost, server.serverPort());
            CHECK(waitUntil([&] { return adopted && rogue.state() == QAbstractSocket::ConnectedState; }));
            rogue.write(QByteArray::fromHex("00000000"));
            rogue.flush();
            CHECK(waitUntil([&] { return errorSeen; }));
            CHECK(waitUntil([&] { return rogue.state() == QAbstractSocket::UnconnectedState; }));
        }
        // (c) preamble hợp lệ về kích thước rồi tới một khung rác: preamble được trao lên, khung rác không
        //     giải mã được -> decryptFailed + đóng; không thông điệp nào được phát ra
        {
            QTcpServer server;
            CHECK(server.listen(QHostAddress::LocalHost));
            NetworkSession victim;
            bool adopted = false;
            int preambles = 0, messages = 0, decryptFailures = 0;
            QObject::connect(&server, &QTcpServer::newConnection, [&] {
                victim.adoptSocket(server.nextPendingConnection());
                adopted = true;
            });
            QObject::connect(&victim, &NetworkSession::preambleReceived, [&](QByteArray) {
                ++preambles;
                victim.setHandshakeKey(CryptoSession::generateRandomKey());
            });
            QObject::connect(&victim, &NetworkSession::messageReceived, [&](ProtocolMessage) { ++messages; });
            QObject::connect(&victim, &NetworkSession::decryptFailed, [&] { ++decryptFailures; });

            QTcpSocket rogue;
            rogue.connectToHost(QHostAddress::LocalHost, server.serverPort());
            CHECK(waitUntil([&] { return adopted && rogue.state() == QAbstractSocket::ConnectedState; }));
            rogue.write(QByteArray::fromHex("00000008") + QByteArray("preamble") + QByteArray::fromHex("0000001F") +
                        QByteArray(31, '\x00'));
            rogue.flush();
            CHECK(waitUntil([&] { return decryptFailures == 1; }));
            CHECK(preambles == 1);
            CHECK(messages == 0);
            CHECK(waitUntil([&] { return rogue.state() == QAbstractSocket::UnconnectedState; }));
        }
        // (d) có preamble nhưng nơi dùng không đặt khóa: dữ liệu tiếp theo làm đóng kết nối
        {
            QTcpServer server;
            CHECK(server.listen(QHostAddress::LocalHost));
            NetworkSession victim;
            bool adopted = false;
            int messages = 0;
            QObject::connect(&server, &QTcpServer::newConnection, [&] {
                victim.adoptSocket(server.nextPendingConnection());
                adopted = true;
            });
            QObject::connect(&victim, &NetworkSession::messageReceived, [&](ProtocolMessage) { ++messages; });
            QTcpSocket rogue;
            rogue.connectToHost(QHostAddress::LocalHost, server.serverPort());
            CHECK(waitUntil([&] { return adopted && rogue.state() == QAbstractSocket::ConnectedState; }));
            rogue.write(QByteArray::fromHex("00000004") + QByteArray("abcd") + QByteArray::fromHex("00000020") +
                        QByteArray(32, 'z'));
            rogue.flush();
            CHECK(waitUntil([&] { return rogue.state() == QAbstractSocket::UnconnectedState; }));
            CHECK(messages == 0);
        }
    }

    // ---- NetworkSession: PHÁT LẠI và PHẢN XẠ bị từ chối ----
    {
        // Bắt nguyên các byte một NetworkSession gửi ra bằng một socket thô ở đầu kia.
        const QByteArray keyI2R = CryptoSession::generateRandomKey();
        const QByteArray keyR2I = CryptoSession::generateRandomKey();

        QTcpServer tapServer;
        CHECK(tapServer.listen(QHostAddress::LocalHost));
        QTcpSocket* tap = nullptr;
        QObject::connect(&tapServer, &QTcpServer::newConnection, [&] { tap = tapServer.nextPendingConnection(); });

        NetworkSession sender;
        int senderDecryptFailures = 0, senderMessages = 0;
        QObject::connect(&sender, &NetworkSession::decryptFailed, [&] { ++senderDecryptFailures; });
        QObject::connect(&sender, &NetworkSession::messageReceived, [&](ProtocolMessage) { ++senderMessages; });
        sender.connectToHost(QHostAddress::LocalHost, tapServer.serverPort());
        CHECK(waitUntil([&] { return tap != nullptr && sender.isConnected(); }));
        sender.setSessionKeys(keyI2R, keyR2I);

        ProtocolMessage keyMsg;
        keyMsg.type = MessageType::KeyEvent;
        keyMsg.intA = 0x41;
        keyMsg.flagA = true;
        QString err;
        CHECK(sender.sendMessage(keyMsg, &err));
        CHECK(waitUntil([&] { return tap->bytesAvailable() >= 4 + 16; }));
        const QByteArray capturedFrame = tap->readAll(); // đúng 1 khung: gói thứ 0 của chiều I->R
        CHECK(capturedFrame.size() > 4 + 16);

        // (1) PHÁT LẠI: bên nhận thật nhận khung lần đầu (bộ đếm 0) -> OK; gửi lại đúng khung đó -> bộ đếm
        //     của nó đã là 1 nên giải mã thất bại, không có thông điệp thứ hai, kết nối bị đóng.
        {
            QTcpServer server;
            CHECK(server.listen(QHostAddress::LocalHost));
            NetworkSession receiver;
            bool adopted = false;
            int messages = 0, decryptFailures = 0;
            QObject::connect(&server, &QTcpServer::newConnection, [&] {
                receiver.adoptSocket(server.nextPendingConnection(), false);
                receiver.setSessionKeys(keyR2I, keyI2R);
                adopted = true;
            });
            QObject::connect(&receiver, &NetworkSession::messageReceived, [&](ProtocolMessage) { ++messages; });
            QObject::connect(&receiver, &NetworkSession::decryptFailed, [&] { ++decryptFailures; });

            QTcpSocket attacker;
            attacker.connectToHost(QHostAddress::LocalHost, server.serverPort());
            CHECK(waitUntil([&] { return adopted && attacker.state() == QAbstractSocket::ConnectedState; }));
            attacker.write(capturedFrame);
            attacker.flush();
            CHECK(waitUntil([&] { return messages == 1; }));
            attacker.write(capturedFrame); // phát lại y nguyên
            attacker.flush();
            CHECK(waitUntil([&] { return decryptFailures == 1; }));
            CHECK(messages == 1);
            CHECK(waitUntil([&] { return attacker.state() == QAbstractSocket::UnconnectedState; }));
        }

        // (2) PHẢN XẠ ở giai đoạn phiên: dội khung của chính sender về lại sender -> khóa nhận của nó là
        //     K_r2i (khác K_i2r đã mã hóa khung) nên không giải mã được.
        tap->write(capturedFrame);
        tap->flush();
        CHECK(waitUntil([&] { return senderDecryptFailures == 1; }));
        CHECK(senderMessages == 0);

        // (3) PHẢN XẠ ở giai đoạn bắt tay: cùng MỘT khóa cho hai chiều, nhưng AAD mang chiều gửi nên gói
        //     của chính mình dội lại vẫn bị từ chối.
        {
            QTcpServer echoServer;
            CHECK(echoServer.listen(QHostAddress::LocalHost));
            QTcpSocket* echo = nullptr;
            QObject::connect(&echoServer, &QTcpServer::newConnection, [&] {
                echo = echoServer.nextPendingConnection();
                QObject::connect(echo, &QTcpSocket::readyRead, echo, [&] { echo->write(echo->readAll()); });
            });
            NetworkSession victim;
            int messages = 0, decryptFailures = 0;
            QObject::connect(&victim, &NetworkSession::messageReceived, [&](ProtocolMessage) { ++messages; });
            QObject::connect(&victim, &NetworkSession::decryptFailed, [&] { ++decryptFailures; });
            victim.connectToHost(QHostAddress::LocalHost, echoServer.serverPort());
            CHECK(waitUntil([&] { return echo != nullptr && victim.isConnected(); }));
            victim.setHandshakeKey(CryptoSession::generateRandomKey(), "ctx");
            ProtocolMessage hello;
            hello.type = MessageType::SessionHello;
            hello.textB = "nan-nhan";
            CHECK(victim.sendMessage(hello, &err));
            CHECK(waitUntil([&] { return decryptFailures == 1; }));
            CHECK(messages == 0);
        }
    }

    // ---- PeerDiscovery: khám phá LAN qua UDP broadcast (best-effort trong sandbox) ----
    {
        PeerDiscovery a, b;
        QString errA, errB;
        const bool startedA = a.start("disc-id-aaa", "May A", 11001, &errA);
        const bool startedB = b.start("disc-id-bbb", "May B", 11002, &errB);

        // Môi trường build có thể chặn UDP broadcast (container/VM) - không bắt buộc phải thành công,
        // nhưng nếu start() thất bại thì PHẢI có thông báo lỗi rõ ràng, không được âm thầm "coi như xong".
        CHECK(startedA == errA.isEmpty());
        CHECK(startedB == errB.isEmpty());

        if (startedA && startedB)
        {
            QList<QString> aFound, bFound;
            QList<int> versionsSeenByA;
            QObject::connect(&a, &PeerDiscovery::peerAnnounced, [&](QString id, QString, QHostAddress, quint16, int v) {
                aFound.push_back(id);
                if (id == "disc-id-bbb")
                    versionsSeenByA.push_back(v);
            });
            QObject::connect(&b, &PeerDiscovery::peerAnnounced,
                             [&](QString id, QString, QHostAddress, quint16, int) { bFound.push_back(id); });

            const bool sawEachOther = waitUntil(
                [&] {
                    return aFound.contains(QString("disc-id-bbb")) && bFound.contains(QString("disc-id-aaa"));
                },
                5000);

            if (sawEachOther)
            {
                CHECK(!aFound.contains(QString("disc-id-aaa"))); // không bao giờ thấy chính mình
                CHECK(!bFound.contains(QString("disc-id-bbb")));
                CHECK(!versionsSeenByA.isEmpty() && versionsSeenByA.first() == ConnectProtocol::kVersion);
                std::printf("  [PeerDiscovery] broadcast LAN hoạt động trong môi trường này.\n");
            }
            else
            {
                std::printf("  [PeerDiscovery] khong thay broadcast qua lai trong sandbox nay (co the bi chan) - "
                           "bo qua, khong tinh la loi vi day la gioi han moi truong, khong phai code.\n");
            }
        }

        a.stop();
        b.stop();
        CHECK(!a.isRunning() && !b.isRunning());
    }

    // ---- EdgeDetector: toán học thuần, không cần màn hình thật ----
    {
        const QRect bounds(0, 0, 1920, 1080); // vùng gộp màn hình cục bộ (virtual desktop)

        // Không có láng giềng nào -> không bao giờ kích hoạt, dù đứng đúng mép
        CHECK(EdgeDetector::detectCrossing(QPoint(0, 500), bounds, {}) == ScreenSide::None);
        CHECK(EdgeDetector::detectCrossing(QPoint(1919, 500), bounds, {}) == ScreenSide::None);

        // Giữa màn hình, có láng giềng mọi phía -> vẫn không kích hoạt
        const QSet<ScreenSide> allSides = {ScreenSide::Left, ScreenSide::Right, ScreenSide::Top, ScreenSide::Bottom};
        CHECK(EdgeDetector::detectCrossing(QPoint(960, 540), bounds, allSides) == ScreenSide::None);

        // Chạm đúng từng biên với láng giềng tương ứng
        CHECK(EdgeDetector::detectCrossing(QPoint(0, 540), bounds, {ScreenSide::Left}) == ScreenSide::Left);
        CHECK(EdgeDetector::detectCrossing(QPoint(1919, 540), bounds, {ScreenSide::Right}) == ScreenSide::Right);
        CHECK(EdgeDetector::detectCrossing(QPoint(960, 0), bounds, {ScreenSide::Top}) == ScreenSide::Top);
        CHECK(EdgeDetector::detectCrossing(QPoint(960, 1079), bounds, {ScreenSide::Bottom}) == ScreenSide::Bottom);

        // Chạm biên nhưng phía đó KHÔNG có láng giềng -> không kích hoạt
        CHECK(EdgeDetector::detectCrossing(QPoint(0, 540), bounds, {ScreenSide::Right}) == ScreenSide::None);

        // Vượt hẳn ra ngoài vùng (không chỉ chạm đúng mép) vẫn được tính là đã chạm biên
        CHECK(EdgeDetector::detectCrossing(QPoint(-50, 540), bounds, {ScreenSide::Left}) == ScreenSide::Left);
        CHECK(EdgeDetector::detectCrossing(QPoint(2000, 540), bounds, {ScreenSide::Right}) == ScreenSide::Right);

        // Vùng rỗng không được crash, trả về None
        CHECK(EdgeDetector::detectCrossing(QPoint(0, 0), QRect(), allSides) == ScreenSide::None);

        // Vị trí chuẩn hóa dọc biên
        CHECK(std::abs(EdgeDetector::normalizedPositionAlongEdge(QPoint(0, 0), bounds, ScreenSide::Left) - 0.0) < 0.01);
        CHECK(std::abs(EdgeDetector::normalizedPositionAlongEdge(QPoint(0, 1080), bounds, ScreenSide::Left) - 1.0) < 0.01);
        CHECK(std::abs(EdgeDetector::normalizedPositionAlongEdge(QPoint(0, 540), bounds, ScreenSide::Left) - 0.5) < 0.01);
        CHECK(std::abs(EdgeDetector::normalizedPositionAlongEdge(QPoint(960, 0), bounds, ScreenSide::Top) - 0.5) < 0.01);
        // Giá trị ngoài vùng vẫn phải kẹp về [0,1], không âm/không vượt quá 1
        CHECK(EdgeDetector::normalizedPositionAlongEdge(QPoint(0, -500), bounds, ScreenSide::Left) == 0.0);
        CHECK(EdgeDetector::normalizedPositionAlongEdge(QPoint(0, 5000), bounds, ScreenSide::Left) == 1.0);

        // Điểm vào khi nhận quyền điều khiển: đi vào từ biên nào thì nằm sát biên đó, lùi vài pixel
        const QPoint enterLeft = EdgeDetector::entryPoint(bounds, ScreenSide::Left, 0.5);
        CHECK(enterLeft.x() > bounds.left() && enterLeft.x() < bounds.left() + 10);
        CHECK(std::abs(enterLeft.y() - 540) < 5);

        const QPoint enterRight = EdgeDetector::entryPoint(bounds, ScreenSide::Right, 0.25);
        CHECK(enterRight.x() < bounds.right() && enterRight.x() > bounds.right() - 10);
        CHECK(std::abs(enterRight.y() - 270) < 5);

        const QPoint enterTop = EdgeDetector::entryPoint(bounds, ScreenSide::Top, 0.75);
        CHECK(enterTop.y() > bounds.top() && enterTop.y() < bounds.top() + 10);
        CHECK(std::abs(enterTop.x() - 1440) < 5);

        const QPoint enterBottom = EdgeDetector::entryPoint(bounds, ScreenSide::Bottom, 0.0);
        CHECK(enterBottom.y() < bounds.bottom() && enterBottom.y() > bounds.bottom() - 10);
        CHECK(std::abs(enterBottom.x() - 0) < 5);

        // Toàn bộ quy trình: phát hiện chạm biên bên gửi -> tính vị trí -> đổi hướng -> tính điểm vào bên nhận
        const QRect senderBounds(0, 0, 1920, 1080);
        const QPoint crossingPoint(1919, 800); // gần đáy, chạm biên phải
        const ScreenSide crossedSide = EdgeDetector::detectCrossing(crossingPoint, senderBounds, {ScreenSide::Right});
        CHECK(crossedSide == ScreenSide::Right);
        const double frac = EdgeDetector::normalizedPositionAlongEdge(crossingPoint, senderBounds, crossedSide);
        const ScreenSide receiverEntrySide = oppositeSide(crossedSide);
        CHECK(receiverEntrySide == ScreenSide::Left);
        const QRect receiverBounds(0, 0, 2560, 1440); // máy nhận có độ phân giải KHÁC - vẫn phải hoạt động đúng
        const QPoint entry = EdgeDetector::entryPoint(receiverBounds, receiverEntrySide, frac);
        CHECK(entry.x() < receiverBounds.left() + 10); // vào từ bên trái của máy nhận
        CHECK(std::abs(entry.y() - static_cast<int>(frac * 1440)) < 5); // tỉ lệ đúng theo độ phân giải máy nhận

        // Điểm đặt lại con trỏ khi LẤY LẠI quyền điều khiển: giữ vị trí dọc biên, lùi hẳn vào trong - và
        // quan trọng nhất là KHÔNG còn bị coi là "chạm biên" (nếu còn thì sẽ trao quyền đi ngay lần nữa).
        const struct { ScreenSide side; QPoint cross; } returns[] = {
            {ScreenSide::Right, QPoint(1919, 800)}, {ScreenSide::Left, QPoint(0, 300)},
            {ScreenSide::Top, QPoint(700, 0)},      {ScreenSide::Bottom, QPoint(1200, 1079)}};
        for (const auto& r : returns)
        {
            const QPoint back = EdgeDetector::returnPoint(bounds, r.side, r.cross, 32);
            CHECK(bounds.contains(back));
            CHECK(EdgeDetector::detectCrossing(back, bounds, allSides) == ScreenSide::None);
            if (r.side == ScreenSide::Left || r.side == ScreenSide::Right)
                CHECK(back.y() == r.cross.y());
            else
                CHECK(back.x() == r.cross.x());
        }
        CHECK(EdgeDetector::returnPoint(bounds, ScreenSide::Right, QPoint(1919, 800), 32).x() == 1919 - 32);
        // Con trỏ nằm ngoài vùng (màn hình vừa đổi) vẫn được kéo về trong vùng; vùng rỗng không crash
        CHECK(bounds.contains(EdgeDetector::returnPoint(bounds, ScreenSide::Left, QPoint(-500, 9999), 32)));
        CHECK(EdgeDetector::returnPoint(QRect(), ScreenSide::Left, QPoint(5, 5), 32) == QPoint(5, 5));
    }

    // ---- InputHook: chỉ test logic thuần (tính delta, cổng active, nhận diện hotkey) qua gọi thẳng
    // onRaw*() - KHÔNG gọi install() với active=true và KHÔNG dùng InputInjector thật trong test tự
    // động, vì làm vậy sẽ thật sự khóa/chiếm chuột và bàn phím của người đang ngồi máy chạy test này.
    {
        InputHook hook;
        CHECK(!hook.isInstalled());
        CHECK(!hook.isActive());

        // Khi KHÔNG active: sự kiện thô đi qua bình thường (không bị "nuốt"), không phát tín hiệu
        int moveSignals = 0, buttonSignals = 0, wheelSignals = 0, keySignals = 0, hotkeySignals = 0;
        QPoint lastDelta;
        QList<QPair<int, bool>> keyEvents, buttonEvents;
        QObject::connect(&hook, &InputHook::mouseMoveRelative, [&](int dx, int dy) {
            ++moveSignals;
            lastDelta = QPoint(dx, dy);
        });
        QObject::connect(&hook, &InputHook::mouseButtonChanged, [&](int b, bool p) {
            ++buttonSignals;
            buttonEvents << qMakePair(b, p);
        });
        QObject::connect(&hook, &InputHook::mouseWheelMoved, [&](int, int) { ++wheelSignals; });
        QObject::connect(&hook, &InputHook::keyChanged, [&](int vk, int, bool p, bool) {
            ++keySignals;
            keyEvents << qMakePair(vk, p);
        });
        QObject::connect(&hook, &InputHook::returnHotkeyPressed, [&]() { ++hotkeySignals; });

        CHECK(hook.onRawMouseMove(100, 100, 90, 90) == false); // không active -> không nuốt sự kiện
        CHECK(moveSignals == 0);
        CHECK(hook.onRawMouseButton(0, true) == false);
        CHECK(hook.onRawMouseButton(0, false) == false);
        CHECK(buttonSignals == 0);

        // Bật active: sự kiện bị nuốt (trả về true) và phát tín hiệu tương ứng
        hook.setActive(true);
        CHECK(hook.isActive());

        // Độ dịch = vị trí hook báo - vị trí con trỏ thật. Con trỏ ĐỨNG YÊN (100,100) vì sự kiện bị nuốt;
        // rê đều tay sang phải 10px mỗi lần phải ra (10,0) Ở MỌI LẦN - bản cũ lấy hiệu hai sự kiện liên
        // tiếp nên từ lần thứ hai trở đi ra (0,0), tức chuột máy kia không nhúc nhích.
        CHECK(hook.onRawMouseMove(110, 100, 100, 100) == true);
        CHECK(moveSignals == 1 && lastDelta == QPoint(10, 0));
        CHECK(hook.onRawMouseMove(110, 100, 100, 100) == true);
        CHECK(moveSignals == 2 && lastDelta == QPoint(10, 0));
        CHECK(hook.onRawMouseMove(95, 108, 100, 100) == true);
        CHECK(moveSignals == 3 && lastDelta == QPoint(-5, 8));
        CHECK(hook.onRawMouseMove(100, 100, 100, 100) == true); // không dịch chuyển: vẫn nuốt, không phát tín hiệu
        CHECK(moveSignals == 3);

        CHECK(hook.onRawMouseButton(1, true) == true);
        CHECK(buttonSignals == 1);
        CHECK(hook.onRawMouseButton(1, false) == true);
        CHECK(buttonSignals == 2);
        CHECK(hook.onRawMouseButton(9, true) == false); // mã nút vô nghĩa
        CHECK(hook.onRawMouseWheel(120, 0) == true);
        CHECK(wheelSignals == 1);
        CHECK(hook.onRawKey(0x41, 30, true, false) == true); // phím 'A' thường
        CHECK(keySignals == 1);
        CHECK(hook.onRawKey(0x41, 30, false, false) == true);
        CHECK(keySignals == 2);

        // Tắt active: quay về trạng thái đi qua bình thường
        hook.setActive(false);
        CHECK(hook.onRawMouseMove(200, 200, 100, 100) == false);
        CHECK(moveSignals == 3); // không tăng thêm

        // Phím GIỮ SẴN từ trước khi chia sẻ điều khiển thuộc về máy này: lần nhấn lặp + lần nhả của nó phải
        // đi qua cục bộ (không nuốt, không chuyển tiếp) - nếu nuốt lần nhả thì máy này kẹt phím.
        keyEvents.clear();
        CHECK(hook.onRawKey(VK_SHIFT, 42, true, false) == false); // nhấn lúc chưa active -> máy này thấy
        hook.setActive(true);
        CHECK(hook.onRawKey(VK_SHIFT, 42, true, false) == false); // lặp do giữ phím
        CHECK(hook.onRawKey(VK_SHIFT, 42, false, false) == false); // nhả -> máy này thấy
        CHECK(keyEvents.isEmpty());
        CHECK(hook.onRawKey(VK_SHIFT, 42, true, false) == true);  // nhấn MỚI sau đó -> của máy kia
        CHECK(hook.onRawKey(VK_SHIFT, 42, false, false) == true);
        CHECK(keyEvents.size() == 2);

        // Phím đã nuốt lần NHẤN, rồi trả quyền điều khiển trước khi nhả: lần nhả vẫn bị nuốt (máy này chưa
        // từng thấy lần nhấn) nhưng không chuyển tiếp nữa (controller đã tự gửi lệnh nhả sang máy kia).
        keyEvents.clear();
        CHECK(hook.onRawKey(0x42, 48, true, false) == true);
        hook.setActive(false);
        CHECK(hook.onRawKey(0x42, 48, false, false) == true);
        CHECK(keyEvents.size() == 1 && keyEvents.first() == qMakePair(0x42, true));
        CHECK(hook.onRawKey(0x42, 48, false, false) == false); // lần nhả "thừa" sau đó đi qua bình thường

        // Nút chuột: cùng quy tắc
        buttonEvents.clear();
        CHECK(hook.onRawMouseButton(0, true) == false);  // nhấn lúc chưa active (đang kéo cửa sổ)
        hook.setActive(true);
        CHECK(hook.onRawMouseButton(0, false) == false); // nhả -> máy này thấy, kết thúc thao tác kéo
        CHECK(buttonEvents.isEmpty());
        CHECK(hook.onRawMouseButton(2, true) == true);
        hook.setActive(false);
        CHECK(hook.onRawMouseButton(2, false) == true);
        CHECK(buttonEvents.size() == 1);

        // Phím nóng Ctrl+Alt+Home: luôn phát returnHotkeyPressed bất kể active hay không, và việc có
        // "nuốt" tổ hợp phím hay không phụ thuộc trạng thái active tại thời điểm nhấn.
        CHECK(hotkeySignals == 0);
        hook.onRawKey(VK_CONTROL, 29, true, true);
        hook.onRawKey(VK_MENU, 56, true, true);
        const bool consumedWhenInactive = hook.onRawKey(VK_HOME, 71, true, true);
        CHECK(hotkeySignals == 1);
        CHECK(consumedWhenInactive == false); // không active -> không nuốt, để phím Home đi qua bình thường
        CHECK(hook.onRawKey(VK_HOME, 71, false, true) == false);

        hook.setActive(true);
        const bool consumedWhenActive = hook.onRawKey(VK_HOME, 71, true, true);
        CHECK(hotkeySignals == 2);
        CHECK(consumedWhenActive == true); // active -> nuốt tổ hợp phím nóng, không gửi Home sang máy kia

        // Nơi nhận tín hiệu tắt active NGAY trong lúc xử lý (đúng như ConnectSessionController làm): lần
        // nhấn Home vẫn phải bị nuốt (bản cũ đọc lại m_active sau khi phát tín hiệu nên để lọt), và lần nhả
        // của nó cũng bị nuốt, không phát keyChanged nào cho Home.
        hook.onRawKey(VK_HOME, 71, false, true);
        keyEvents.clear();
        const auto deactivateOnHotkey =
            QObject::connect(&hook, &InputHook::returnHotkeyPressed, [&]() { hook.setActive(false); });
        CHECK(hook.onRawKey(VK_HOME, 71, true, true) == true);
        CHECK(!hook.isActive());
        CHECK(hook.onRawKey(VK_HOME, 71, false, true) == true);
        CHECK(keyEvents.isEmpty());
        QObject::disconnect(deactivateOnHotkey);

        // Nhả Ctrl thì tổ hợp không còn đủ điều kiện nữa
        hook.onRawKey(VK_CONTROL, 29, false, true);
        const int hotkeyCountBefore = hotkeySignals;
        hook.onRawKey(VK_HOME, 71, true, true);
        CHECK(hotkeySignals == hotkeyCountBefore); // thiếu Ctrl -> không tính là phím nóng nữa
    }

    // ---- InputHook: vòng đời cài/gỡ hook THẬT (nhưng không bao giờ setActive(true) lúc đã cài -
    // hook mặc định active=false nên hoàn toàn trong suốt, không ảnh hưởng chuột/bàn phím thật) ----
    {
        InputHook hook;
        QString err;
        const bool installed = hook.install(&err);
        CHECK(installed);
        CHECK(err.isEmpty());
        CHECK(hook.isInstalled());
        CHECK(!hook.isActive()); // mặc định không active - trong suốt với người dùng thật

        // Bơm vòng lặp sự kiện một chút trong lúc hook đang cài: hook toàn cục WH_MOUSE_LL/WH_KEYBOARD_LL
        // được Windows gọi lại trong lúc bơm message - nếu có hoạt động chuột/phím thật xảy ra đúng lúc
        // test chạy, để nó được xử lý (và CallNextHookEx bình thường vì active=false) thay vì có thể còn
        // "treo" lúc uninstall() chạy ngay sau.
        app.processEvents();

        CHECK(hook.install(&err)); // gọi lại install() khi đã cài -> idempotent, vẫn trả true

        hook.uninstall();
        app.processEvents();
        CHECK(!hook.isInstalled());
        hook.uninstall(); // gọi lại uninstall() khi chưa cài -> không crash

        // Cài lại được sau khi đã gỡ
        CHECK(hook.install(&err));
        CHECK(hook.isInstalled());
        app.processEvents();
        hook.uninstall();
        app.processEvents();
    }

    // ---- ConnectSessionController với "máy giả": ghép đôi cả 2 chiều, giới hạn số lần thử mã, bắt tay
    // phiên, chống phát lại/phản xạ, hạn bắt tay, trần kết nối chờ. Controller chạy ở chế độ kiểm thử:
    // KHÔNG cài hook, KHÔNG khám phá LAN, KHÔNG đụng clipboard, tiêm input qua FakeInjector. ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        LocalIdentityStore::instance().setFilePath(tmp.filePath("identity.json"));
        const LocalIdentity myIdentity = LocalIdentityStore::instance().identity();
        CHECK(!myIdentity.id.isEmpty());
        CHECK(ConnectProtocol::isValidPeerId(myIdentity.id));

        PeerStore peerStore(tmp.filePath("peers.json"));
        FakeInjector injector;

        ConnectSessionController controller;
        ConnectSessionController::TestConfig cfg;
        cfg.store = &peerStore;
        cfg.injector = &injector;
        cfg.installGlobalHook = false;
        cfg.enableDiscovery = false;
        cfg.enableClipboardSync = false;
        cfg.usePreferredPort = false;
        cfg.handshakeTimeoutMs = 1200;
        cfg.housekeepingIntervalMs = 50;
        controller.configureForTesting(cfg);

        QString startErr;
        CHECK(controller.start(&startErr));
        CHECK(startErr.isEmpty());
        CHECK(controller.start(&startErr)); // idempotent
        CHECK(controller.listenPort() != 0);
        CHECK(controller.localIdentity().id == myIdentity.id);
        CHECK(!controller.isHookActive());

        QList<QPair<QString, QString>> pairingSucceededEvents; // (peerId, machineName)
        QObject::connect(&controller, &ConnectSessionController::pairingSucceeded,
                         [&](QString id, QString name) { pairingSucceededEvents.push_back({id, name}); });
        QList<QString> pairingFailedReasons;
        QObject::connect(&controller, &ConnectSessionController::pairingFailed,
                         [&](QString reason) { pairingFailedReasons.push_back(reason); });
        QList<QPair<QString, bool>> connectionEvents;
        QObject::connect(&controller, &ConnectSessionController::peerConnectionChanged,
                         [&](QString id, bool on) { connectionEvents.push_back({id, on}); });

        // Một "máy giả" đóng vai bên NHẬP mã: preamble(Pairing) + PairRequest bằng khóa suy từ 'code'.
        struct FakePairInitiator
        {
            NetworkSession session;
            QList<ProtocolMessage> received;
            bool closed{false};
            int versionField{ConnectProtocol::kVersion};
            void run(quint16 port, const QString& code, const QString& id, int announcedPort)
            {
                QObject::connect(&session, &NetworkSession::messageReceived, &session,
                                 [this](ProtocolMessage m) { received.push_back(m); });
                QObject::connect(&session, &NetworkSession::disconnected, &session, [this] { closed = true; });
                QObject::connect(&session, &NetworkSession::connected, &session, [this, code, id, announcedPort] {
                    const QByteArray preamble = ConnectProtocol::buildPreamble(ConnectProtocol::LinkPurpose::Pairing, id);
                    session.setHandshakeKey(CryptoSession::deriveKeyFromPairingCode(code), preamble);
                    session.sendPreamble(preamble);
                    ProtocolMessage req;
                    req.type = MessageType::PairRequest;
                    req.textA = "May Gia A";
                    req.textB = id;
                    req.intA = versionField;
                    req.intB = announcedPort;
                    session.sendMessage(req);
                });
                session.connectToHost(QHostAddress::LocalHost, port);
            }
        };

        // ---- A. Nhận ghép đôi: controller sinh mã, "máy giả" kết nối vào và nhập đúng mã đó ----
        const QString rawCode = controller.beginPairingSession();
        CHECK(PairingCode::isValidFormat(rawCode));
        CHECK(controller.isPairingSessionOpen());
        {
            FakePairInitiator fakeA;
            fakeA.run(controller.listenPort(), rawCode, "fake-peer-id-aaa", 40123);

            CHECK(waitUntil([&] { return !fakeA.received.isEmpty(); }));
            CHECK(!fakeA.received.isEmpty() && fakeA.received.first().type == MessageType::PairAccept);
            if (!fakeA.received.isEmpty())
            {
                CHECK(fakeA.received.first().textA == myIdentity.machineName);
                CHECK(fakeA.received.first().textB == myIdentity.id);
                CHECK(fakeA.received.first().longTermKey.size() == 32);
                CHECK(fakeA.received.first().intB == controller.listenPort()); // cổng LẮNG NGHE của controller
            }

            CHECK(waitUntil([&] { return !pairingSucceededEvents.isEmpty(); }));
            CHECK(pairingSucceededEvents.first().first == "fake-peer-id-aaa");
            CHECK(pairingSucceededEvents.first().second == "May Gia A");
            CHECK(!controller.isPairingSessionOpen()); // mã ghép đôi tự đóng ngay sau khi dùng

            const QList<PairedPeer> peers = controller.pairedPeers();
            const auto it = std::find_if(peers.begin(), peers.end(),
                                         [](const PairedPeer& p) { return p.id == "fake-peer-id-aaa"; });
            CHECK(it != peers.end());
            if (it != peers.end() && !fakeA.received.isEmpty())
            {
                CHECK(it->machineName == "May Gia A");
                CHECK(it->longTermKey == fakeA.received.first().longTermKey);
                CHECK(it->autoConnect == true);
                // Cổng lưu lại phải là cổng lắng nghe máy kia TỰ KHAI, không phải cổng nguồn tạm thời của
                // kết nối ghép đôi (bản cũ lưu cổng nguồn - nối lại vào đó không bao giờ được).
                CHECK(it->lastPort == 40123);
            }
            // Máy giả này không nghe cổng nào - tắt tự kết nối lại NGAY (trước khi lần thử đầu, hẹn 300 ms
            // sau khi ghép đôi, kịp chạy) để các mục sau đếm số kết nối đang chờ không bị nhiễu.
            controller.setPeerAutoConnect("fake-peer-id-aaa", false);
            CHECK(waitUntil([&] { return fakeA.closed; })); // controller đóng kết nối ghép đôi sau khi xong
        }
        CHECK(pairingFailedReasons.isEmpty()); // ghép đôi thành công KHÔNG được kèm một báo lỗi nào

        // ---- B. Chủ động ghép đôi: controller tự nhập mã do "máy giả B" tạo ra ----
        {
            QTcpServer fakeAcceptorServer;
            CHECK(fakeAcceptorServer.listen(QHostAddress::LocalHost));
            const QString fixedCode = "135792468";
            CHECK(PairingCode::isValidFormat(fixedCode));

            NetworkSession fakePeerB;
            bool fakePeerBAdopted = false;
            QObject::connect(&fakeAcceptorServer, &QTcpServer::newConnection, [&] {
                if (!fakePeerBAdopted && fakeAcceptorServer.hasPendingConnections())
                {
                    fakePeerB.adoptSocket(fakeAcceptorServer.nextPendingConnection());
                    fakePeerBAdopted = true;
                }
            });
            QByteArray preambleSeenByB;
            QObject::connect(&fakePeerB, &NetworkSession::preambleReceived, [&](QByteArray preamble) {
                preambleSeenByB = preamble;
                fakePeerB.setHandshakeKey(CryptoSession::deriveKeyFromPairingCode(fixedCode), preamble);
            });

            QList<ProtocolMessage> fakePeerBReceived;
            QObject::connect(&fakePeerB, &NetworkSession::messageReceived, [&](ProtocolMessage m) {
                fakePeerBReceived.push_back(m);
                if (m.type == MessageType::PairRequest)
                {
                    ProtocolMessage accept;
                    accept.type = MessageType::PairAccept;
                    accept.textA = "May Gia B";
                    accept.textB = "fake-peer-id-bbb";
                    accept.longTermKey = CryptoSession::generateRandomKey();
                    accept.intA = ConnectProtocol::kVersion;
                    QString e;
                    fakePeerB.sendMessage(accept, &e);
                }
            });

            controller.connectWithCode(QHostAddress::LocalHost, fakeAcceptorServer.serverPort(), fixedCode);
            CHECK(waitUntil([&] { return fakePeerBAdopted; }));
            CHECK(waitUntil([&] { return !fakePeerBReceived.isEmpty(); }));
            int preVersion = 0;
            ConnectProtocol::LinkPurpose prePurpose = ConnectProtocol::LinkPurpose::Session;
            QString preId;
            CHECK(ConnectProtocol::parsePreamble(preambleSeenByB, &preVersion, &prePurpose, &preId));
            CHECK(prePurpose == ConnectProtocol::LinkPurpose::Pairing && preId == myIdentity.id);
            CHECK(!fakePeerBReceived.isEmpty() && fakePeerBReceived.first().type == MessageType::PairRequest);
            if (!fakePeerBReceived.isEmpty())
            {
                CHECK(fakePeerBReceived.first().textA == myIdentity.machineName);
                CHECK(fakePeerBReceived.first().textB == myIdentity.id);
                CHECK(fakePeerBReceived.first().intA == ConnectProtocol::kVersion);
                CHECK(fakePeerBReceived.first().intB == controller.listenPort());
            }

            CHECK(waitUntil([&] { return pairingSucceededEvents.size() >= 2; }));
            const auto& secondEvent = pairingSucceededEvents.last();
            CHECK(secondEvent.first == "fake-peer-id-bbb");
            CHECK(secondEvent.second == "May Gia B");
            CHECK(controller.pairedPeers().size() == 2);
            controller.setPeerAutoConnect("fake-peer-id-bbb", false); // như trên: máy giả, không nối lại
            // Phía chủ động: cổng lưu lại = cổng vừa nối tới (đúng là cổng lắng nghe của máy kia)
            CHECK(peerStore.find("fake-peer-id-bbb") != nullptr &&
                  peerStore.find("fake-peer-id-bbb")->lastPort == fakeAcceptorServer.serverPort());
        }
        pump(150);
        CHECK(pairingFailedReasons.isEmpty());
        CHECK(waitUntil([&] { return controller.pendingLinkCountForTesting() == 0; }, 4000));

        // ---- C1. Mã sai ĐỊNH DẠNG: báo pairingFailed ngay, không mở kết nối, không thêm peer ----
        {
            QTcpServer unusedServer;
            CHECK(unusedServer.listen(QHostAddress::LocalHost));
            bool anyConnection = false;
            QObject::connect(&unusedServer, &QTcpServer::newConnection, [&] { anyConnection = true; });
            pairingFailedReasons.clear();
            controller.connectWithCode(QHostAddress::LocalHost, unusedServer.serverPort(), "abc");
            CHECK(waitUntil([&] { return !pairingFailedReasons.isEmpty(); }));
            pump(100);
            CHECK(!anyConnection);
            CHECK(controller.pairedPeers().size() == 2); // không tăng thêm
        }

        // ---- C2. Mã ĐÚNG ĐỊNH DẠNG nhưng SAI + giới hạn số lần thử ----
        {
            pairingFailedReasons.clear();
            const QString goodCode = controller.beginPairingSession();
            const QString wrongCode = goodCode == "111111111" ? "222222222" : "111111111";
            const int succeededBefore = pairingSucceededEvents.size();

            // kMaxAttempts - 1 lần sai: mỗi lần bị đóng kết nối, không ghép đôi, mã VẪN còn mở
            for (int i = 0; i < PairingCode::kMaxAttempts - 1; ++i)
            {
                FakePairInitiator wrong;
                wrong.run(controller.listenPort(), wrongCode, "ke-do-ma", 40500);
                CHECK(waitUntil([&] { return wrong.closed; }));
                CHECK(wrong.received.isEmpty()); // không PairAccept, không cả PairReject (chưa có khóa chung)
                CHECK(controller.isPairingSessionOpen());
            }
            CHECK(pairingSucceededEvents.size() == succeededBefore);
            CHECK(peerStore.find("ke-do-ma") == nullptr);
            CHECK(pairingFailedReasons.isEmpty());

            // Lần sai thứ kMaxAttempts: mã bị HỦY, giao diện được báo
            {
                FakePairInitiator wrong;
                wrong.run(controller.listenPort(), wrongCode, "ke-do-ma", 40500);
                CHECK(waitUntil([&] { return wrong.closed; }));
            }
            CHECK(waitUntil([&] { return !controller.isPairingSessionOpen(); }));
            CHECK(waitUntil([&] { return !pairingFailedReasons.isEmpty(); }));

            // Sau khi mã bị hủy, kể cả mã ĐÚNG cũng không ghép đôi được nữa
            {
                FakePairInitiator late;
                late.run(controller.listenPort(), goodCode, "den-muon", 40501);
                CHECK(waitUntil([&] { return late.closed; }));
                CHECK(late.received.isEmpty());
            }
            CHECK(pairingSucceededEvents.size() == succeededBefore);
            CHECK(peerStore.find("den-muon") == nullptr);
            CHECK(controller.pairedPeers().size() == 2);

            // Không mở mã thì yêu cầu ghép đôi nào cũng bị đóng ngay
            {
                FakePairInitiator noWindow;
                noWindow.run(controller.listenPort(), goodCode, "khong-co-ma", 40502);
                CHECK(waitUntil([&] { return noWindow.closed; }));
                CHECK(noWindow.received.isEmpty());
            }

            // Kết nối mở từ lúc mã TRƯỚC còn hiệu lực không dùng lại được cho mã SAU
            const QString codeOne = controller.beginPairingSession();
            NetworkSession lingering;
            bool lingeringClosed = false;
            QList<ProtocolMessage> lingeringReceived;
            QObject::connect(&lingering, &NetworkSession::disconnected, [&] { lingeringClosed = true; });
            QObject::connect(&lingering, &NetworkSession::messageReceived,
                             [&](ProtocolMessage m) { lingeringReceived.push_back(m); });
            QObject::connect(&lingering, &NetworkSession::connected, [&] {
                const QByteArray preamble = ConnectProtocol::buildPreamble(ConnectProtocol::LinkPurpose::Pairing, "nam-cho");
                lingering.setHandshakeKey(CryptoSession::deriveKeyFromPairingCode(codeOne), preamble);
                lingering.sendPreamble(preamble); // chỉ preamble, CHƯA gửi PairRequest
            });
            lingering.connectToHost(QHostAddress::LocalHost, controller.listenPort());
            CHECK(waitUntil([&] { return controller.pendingLinkCountForTesting() == 1; }));
            controller.beginPairingSession(); // mã mới -> kết nối còn dở của mã cũ bị đóng
            CHECK(waitUntil([&] { return lingeringClosed; }));
            CHECK(lingeringReceived.isEmpty());
            CHECK(peerStore.find("nam-cho") == nullptr);

            // Đúng mã nhưng nội dung yêu cầu không hợp lệ (khác phiên bản giao thức / tự khai id của chính
            // controller): được trả lời PairReject rõ ràng (đã có khóa chung), KHÔNG ghép đôi, mã vẫn còn mở.
            const QString codeThree = controller.beginPairingSession();
            {
                FakePairInitiator oldVersion;
                oldVersion.versionField = 1;
                oldVersion.run(controller.listenPort(), codeThree, "ban-cu", 40600);
                CHECK(waitUntil([&] { return !oldVersion.received.isEmpty(); }));
                CHECK(!oldVersion.received.isEmpty() && oldVersion.received.first().type == MessageType::PairReject);
                CHECK(waitUntil([&] { return oldVersion.closed; }));

                FakePairInitiator selfId;
                selfId.run(controller.listenPort(), codeThree, myIdentity.id, 40601);
                CHECK(waitUntil([&] { return !selfId.received.isEmpty(); }));
                CHECK(!selfId.received.isEmpty() && selfId.received.first().type == MessageType::PairReject);
                CHECK(waitUntil([&] { return selfId.closed; }));
            }
            CHECK(peerStore.find("ban-cu") == nullptr && peerStore.find(myIdentity.id) == nullptr);
            CHECK(pairingSucceededEvents.size() == succeededBefore);
            CHECK(controller.isPairingSessionOpen());
            controller.cancelPairingSession();
            CHECK(!controller.isPairingSessionOpen());
        }

        // ---- D. Bắt tay phiên với một máy đã ghép đôi (giả) + ràng buộc thông điệp theo trạng thái ----
        const QByteArray fakeKey = CryptoSession::generateRandomKey();
        PairedPeer fakePaired = makePeer("fake-session-peer", "May Phien Gia", fakeKey, 0);
        fakePaired.autoConnect = false; // chỉ máy giả chủ động nối vào
        peerStore.addOrUpdate(fakePaired);
        {
            // D1. Bắt tay đủ 4 bước -> CẢ HAI đầu coi là đã có phiên
            connectionEvents.clear();
            FakeSessionPeer peer;
            peer.myId = "fake-session-peer";
            peer.peerId = myIdentity.id;
            peer.name = "May Phien Gia";
            peer.longTermKey = fakeKey;
            peer.connectTo(controller.listenPort());
            CHECK(waitUntil([&] { return peer.established; }));
            CHECK(waitUntil([&] { return controller.isPeerConnected("fake-session-peer"); }));
            CHECK(connectionEvents.contains(qMakePair(QString("fake-session-peer"), true)));
            CHECK(controller.pendingLinkCountForTesting() == 0);
            // Địa chỉ + cổng lắng nghe máy kia khai trong SessionHello chỉ được ghi SAU khi bắt tay xong
            CHECK(peerStore.find("fake-session-peer")->lastPort == 45678);
            CHECK(peerStore.find("fake-session-peer")->lastAddress == "127.0.0.1");

            // D2. Sự kiện input khi KHÔNG ở vai trò bị điều khiển -> bỏ qua
            QString e;
            ProtocolMessage key;
            key.type = MessageType::KeyEvent;
            key.intA = 0x41;
            key.intB = 30;
            key.flagA = true;
            CHECK(peer.session.sendMessage(key, &e));
            pump(150);
            CHECK(injector.keys.isEmpty());
            CHECK(controller.currentRole() == ControlRole::Idle);

            // D3. Trao quyền điều khiển: máy đã xác thực gửi ControlHandoff -> BỊ điều khiển, con trỏ được
            //     đặt vào điểm vào (qua injector GIẢ), sau đó input của ĐÚNG máy đó mới được tiêm
            ProtocolMessage handoff;
            handoff.type = MessageType::ControlHandoff;
            handoff.intA = static_cast<int>(ScreenSide::Left);
            handoff.intB = 500;
            CHECK(peer.session.sendMessage(handoff, &e));
            CHECK(waitUntil([&] { return controller.currentRole() == ControlRole::BeingControlled; }));
            CHECK(injector.absoluteMoves.size() == 1);
            CHECK(controller.activePeerName() == "May Phien Gia");
            CHECK(peer.session.sendMessage(key, &e));
            CHECK(waitUntil([&] { return injector.countKey(0x41, true) == 1; }));
            ProtocolMessage move;
            move.type = MessageType::MouseMove;
            move.intA = 5;
            move.intB = -3;
            CHECK(peer.session.sendMessage(move, &e));
            CHECK(waitUntil([&] { return injector.relativeMoves.size() == 1; }));
            CHECK(!injector.relativeMoves.isEmpty() && injector.relativeMoves.first() == QPoint(5, -3));
            // Giá trị vô lý từ mạng không tới được injector
            ProtocolMessage badKey = key;
            badKey.intA = 99999;
            CHECK(peer.session.sendMessage(badKey, &e));
            ProtocolMessage badButton;
            badButton.type = MessageType::MouseButton;
            badButton.intA = 42;
            badButton.flagA = true;
            CHECK(peer.session.sendMessage(badButton, &e));
            pump(150);
            CHECK(injector.keys.size() == 1);
            CHECK(injector.buttons.isEmpty());

            // D4. Một máy đã ghép đôi KHÁC (cũng xác thực đàng hoàng) chen vào: input của nó bị bỏ qua, lệnh
            //     "thôi điều khiển" của nó không có tác dụng, yêu cầu điều khiển của nó bị từ chối rõ ràng
            const QByteArray otherKey = CryptoSession::generateRandomKey();
            PairedPeer otherPaired = makePeer("fake-other-peer", "May Chen Ngang", otherKey, 0);
            otherPaired.autoConnect = false;
            peerStore.addOrUpdate(otherPaired);
            FakeSessionPeer intruder;
            intruder.myId = "fake-other-peer";
            intruder.name = "May Chen Ngang";
            intruder.peerId = myIdentity.id;
            intruder.longTermKey = otherKey;
            intruder.connectTo(controller.listenPort());
            CHECK(waitUntil([&] { return intruder.established; }));
            ProtocolMessage intruderKey = key;
            intruderKey.intA = 0x5A;
            CHECK(intruder.session.sendMessage(intruderKey, &e));
            ProtocolMessage intruderEnd;
            intruderEnd.type = MessageType::ControlHandoff;
            intruderEnd.intA = static_cast<int>(ScreenSide::None);
            CHECK(intruder.session.sendMessage(intruderEnd, &e));
            CHECK(intruder.session.sendMessage(handoff, &e)); // đòi điều khiển trong lúc máy kia đang điều khiển
            CHECK(waitUntil([&] { return !intruder.received.isEmpty(); }));
            CHECK(!intruder.received.isEmpty() && intruder.received.first().type == MessageType::ControlHandoff &&
                  intruder.received.first().intA == static_cast<int>(ScreenSide::None)); // bị từ chối
            CHECK(injector.countKey(0x5A, true) == 0);
            CHECK(controller.currentRole() == ControlRole::BeingControlled);
            CHECK(controller.activePeerName() == "May Phien Gia");
            CHECK(injector.absoluteMoves.size() == 1);

            // D5. Thông điệp ghép đôi trên một phiên đã xác thực = vi phạm giao thức -> đóng phiên đó, và
            //     KHÔNG ghi đè được khóa của máy nào
            const QByteArray keyBefore = peerStore.find("fake-session-peer")->longTermKey;
            ProtocolMessage evilAccept;
            evilAccept.type = MessageType::PairAccept;
            evilAccept.textA = "Ke Xau";
            evilAccept.textB = "fake-session-peer"; // định ghi đè khóa của máy đang điều khiển
            evilAccept.longTermKey = CryptoSession::generateRandomKey();
            evilAccept.intA = ConnectProtocol::kVersion;
            const int pairedBefore = controller.pairedPeers().size();
            CHECK(intruder.session.sendMessage(evilAccept, &e));
            CHECK(waitUntil([&] { return intruder.closed; }));
            CHECK(!controller.isPeerConnected("fake-other-peer"));
            CHECK(peerStore.find("fake-session-peer")->longTermKey == keyBefore);
            CHECK(controller.pairedPeers().size() == pairedBefore);
            CHECK(controller.currentRole() == ControlRole::BeingControlled); // phiên của máy đang điều khiển không hề hấn gì

            // D6. Máy đang điều khiển rớt kết nối trong lúc còn "giữ" phím -> controller tự nhả phím đó và
            //     về Idle (không kẹt phím, không kẹt vai trò)
            peer.session.abortSession();
            CHECK(waitUntil([&] { return controller.currentRole() == ControlRole::Idle; }));
            CHECK(injector.countKey(0x41, false) == 1);
            CHECK(!controller.isPeerConnected("fake-session-peer"));
        }
        {
            // D7. Không gửi SessionConfirm (= kẻ chỉ phát lại được SessionHello, không có khóa để đi tiếp):
            //     KHÔNG BAO GIỜ được đăng ký phiên; mọi thông điệp gửi trước khi xác nhận làm đóng kết nối
            connectionEvents.clear();
            injector.absoluteMoves.clear();
            FakeSessionPeer halfOpen;
            halfOpen.myId = "fake-session-peer";
            halfOpen.peerId = myIdentity.id;
            halfOpen.longTermKey = fakeKey;
            halfOpen.sendConfirm = false;
            halfOpen.connectTo(controller.listenPort());
            CHECK(waitUntil([&] { return halfOpen.gotHelloAck; }));
            pump(150);
            CHECK(!controller.isPeerConnected("fake-session-peer"));
            CHECK(connectionEvents.isEmpty());
            ProtocolMessage handoff;
            handoff.type = MessageType::ControlHandoff;
            handoff.intA = static_cast<int>(ScreenSide::Left);
            QString e;
            CHECK(halfOpen.session.sendMessage(handoff, &e));
            CHECK(waitUntil([&] { return halfOpen.closed; }));
            CHECK(controller.currentRole() == ControlRole::Idle);
            CHECK(injector.absoluteMoves.isEmpty());
            CHECK(!controller.isPeerConnected("fake-session-peer"));
        }
        {
            // D8. Sai khóa dài hạn (máy lạ tự khai id của một máy đã ghép đôi) -> bị đóng ở bước đầu
            FakeSessionPeer impostor;
            impostor.myId = "fake-session-peer";
            impostor.peerId = myIdentity.id;
            impostor.longTermKey = CryptoSession::generateRandomKey();
            impostor.connectTo(controller.listenPort());
            CHECK(waitUntil([&] { return impostor.closed; }));
            CHECK(!impostor.gotHelloAck);
            CHECK(!controller.isPeerConnected("fake-session-peer"));
            // id không hề được ghép đôi -> bị đóng ngay sau preamble
            FakeSessionPeer stranger;
            stranger.myId = "khong-he-ghep-doi";
            stranger.peerId = myIdentity.id;
            stranger.longTermKey = fakeKey;
            stranger.connectTo(controller.listenPort());
            CHECK(waitUntil([&] { return stranger.closed; }));
            CHECK(!stranger.gotHelloAck);
        }
        {
            // D9. PHÁT LẠI nguyên văn một lần bắt tay THÀNH CÔNG đã bắt được trên đường truyền: dựng một
            //     "máy trung gian" chuyển tiếp + ghi lại mọi byte chiều máy giả -> controller, rồi gửi lại
            //     đúng chuỗi byte đó trên một kết nối mới.
            QTcpServer proxy;
            CHECK(proxy.listen(QHostAddress::LocalHost));
            QByteArray recordedClientToServer;
            QByteArray waitingForServerSide; // dữ liệu tới trước khi phía nối sang controller kịp nối xong
            QTcpSocket* proxyClientSide = nullptr;
            QTcpSocket proxyServerSide;
            QObject::connect(&proxyServerSide, &QTcpSocket::connected, &proxyServerSide, [&] {
                proxyServerSide.write(waitingForServerSide);
                waitingForServerSide.clear();
            });
            QObject::connect(&proxy, &QTcpServer::newConnection, [&] {
                proxyClientSide = proxy.nextPendingConnection();
                QObject::connect(proxyClientSide, &QTcpSocket::readyRead, proxyClientSide, [&] {
                    const QByteArray data = proxyClientSide->readAll();
                    recordedClientToServer += data;
                    if (proxyServerSide.state() == QAbstractSocket::ConnectedState)
                        proxyServerSide.write(data);
                    else
                        waitingForServerSide += data;
                });
                QObject::connect(&proxyServerSide, &QTcpSocket::readyRead, &proxyServerSide,
                                 [&] { proxyClientSide->write(proxyServerSide.readAll()); });
                proxyServerSide.connectToHost(QHostAddress::LocalHost, controller.listenPort());
            });

            connectionEvents.clear();
            {
                FakeSessionPeer legit;
                legit.myId = "fake-session-peer";
                legit.peerId = myIdentity.id;
                legit.longTermKey = fakeKey;
                legit.connectTo(proxy.serverPort());
                CHECK(waitUntil([&] { return legit.established; }));
                CHECK(waitUntil([&] { return controller.isPeerConnected("fake-session-peer"); }));
                legit.session.abortSession();
                proxyServerSide.abort();
            }
            CHECK(waitUntil([&] { return !controller.isPeerConnected("fake-session-peer"); }));
            CHECK(recordedClientToServer.size() > 60); // preamble + SessionHello + SessionConfirm
            const int connectedEventsBefore = connectionEvents.count(qMakePair(QString("fake-session-peer"), true));
            CHECK(connectedEventsBefore == 1);

            QTcpSocket replayer;
            replayer.connectToHost(QHostAddress::LocalHost, controller.listenPort());
            CHECK(waitUntil([&] { return replayer.state() == QAbstractSocket::ConnectedState; }));
            replayer.write(recordedClientToServer);
            replayer.flush();
            // SessionHello cũ vẫn giải mã được (khóa dài hạn) nên controller trả lời SessionHelloAck với
            // nonce MỚI; SessionConfirm cũ được mã hóa bằng khóa phiên của nonce CŨ -> không giải mã được
            // -> đóng. Không có phiên nào được đăng ký.
            CHECK(waitUntil([&] { return replayer.state() == QAbstractSocket::UnconnectedState; }));
            CHECK(!controller.isPeerConnected("fake-session-peer"));
            CHECK(connectionEvents.count(qMakePair(QString("fake-session-peer"), true)) == connectedEventsBefore);
            CHECK(controller.currentRole() == ControlRole::Idle);
        }
        {
            // D10. PHẢN XẠ: controller chủ động nối tới một "máy" chỉ dội ngược lại chính các gói của
            //      controller (bản 1: dội lại SessionHello là đủ để được đăng ký phiên mà không cần khóa)
            QTcpServer echoServer;
            CHECK(echoServer.listen(QHostAddress::LocalHost));
            QTcpSocket* echo = nullptr;
            QByteArray echoBuffer;
            int framesSeen = 0, framesReflected = 0;
            bool echoClosed = false;
            QObject::connect(&echoServer, &QTcpServer::newConnection, [&] {
                echo = echoServer.nextPendingConnection();
                QObject::connect(echo, &QTcpSocket::disconnected, echo, [&] { echoClosed = true; });
                QObject::connect(echo, &QTcpSocket::readyRead, echo, [&] {
                    echoBuffer += echo->readAll();
                    while (echoBuffer.size() >= 4)
                    {
                        const quint32 len = qFromBigEndian<quint32>(reinterpret_cast<const uchar*>(echoBuffer.constData()));
                        if (echoBuffer.size() < 4 + static_cast<int>(len))
                            break;
                        const QByteArray frame = echoBuffer.left(4 + static_cast<int>(len));
                        echoBuffer.remove(0, 4 + static_cast<int>(len));
                        ++framesSeen;
                        if (framesSeen >= 2) // bỏ khung preamble, dội lại đúng khung SessionHello mã hóa
                        {
                            echo->write(frame);
                            ++framesReflected;
                        }
                    }
                });
            });

            const QByteArray echoKey = CryptoSession::generateRandomKey();
            peerStore.addOrUpdate(makePeer("echo-peer", "May Doi Nguoc", echoKey, echoServer.serverPort()));
            connectionEvents.clear();
            controller.reconnectNow(); // nối theo địa chỉ + cổng đã lưu, KHÔNG cần gói quảng bá nào
            CHECK(waitUntil([&] { return framesReflected >= 1; }));
            CHECK(waitUntil([&] { return echoClosed; }));
            CHECK(!controller.isPeerConnected("echo-peer"));
            CHECK(connectionEvents.isEmpty());
            controller.forgetPeer("echo-peer");
            CHECK(peerStore.find("echo-peer") == nullptr);
        }
        {
            // D11. Kết nối đi tới một cổng không ai nghe: không rò phiên chờ, không mở dồn kết nối
            QTcpServer closedServer;
            CHECK(closedServer.listen(QHostAddress::LocalHost));
            const quint16 deadPort = closedServer.serverPort();
            closedServer.close();
            peerStore.addOrUpdate(makePeer("dead-peer", "May Da Tat", CryptoSession::generateRandomKey(), deadPort));
            controller.reconnectNow();
            controller.reconnectNow();
            controller.reconnectNow();
            CHECK(controller.pendingLinkCountForTesting() <= 1); // tối đa MỘT kết nối đi đang chờ cho mỗi máy
            CHECK(waitUntil([&] { return controller.pendingLinkCountForTesting() == 0; }, 5000));
            controller.forgetPeer("dead-peer");
        }

        {
            // D12. RỚT KẾT NỐI ĐỘT NGỘT khi controller đang ĐIỀU KHIỂN máy kia (máy kia biến mất, không
            //      kịp báo gì): hook PHẢI tắt và vai trò về Idle. Bản 1 chỉ đổi role về Idle - hook vẫn nuốt
            //      mọi chuột/phím và Ctrl+Alt+Home vô tác dụng, người dùng bị khóa input.
            FakeSessionPeer victim;
            victim.myId = "fake-session-peer";
            victim.peerId = myIdentity.id;
            victim.longTermKey = fakeKey;
            victim.connectTo(controller.listenPort());
            CHECK(waitUntil([&] { return victim.established && controller.isPeerConnected("fake-session-peer"); }));
            CHECK(!controller.requestControlForTesting("may-khong-ton-tai", ScreenSide::Right)); // không có phiên
            CHECK(!controller.isHookActive());

            const int absBefore = injector.absoluteMoves.size();
            CHECK(controller.requestControlForTesting("fake-session-peer", ScreenSide::Right));
            CHECK(controller.currentRole() == ControlRole::Controlling);
            CHECK(controller.isHookActive());
            CHECK(waitUntil([&] { return !victim.received.isEmpty(); }));
            CHECK(!victim.received.isEmpty() && victim.received.first().type == MessageType::ControlHandoff &&
                  victim.received.first().intA == static_cast<int>(ScreenSide::Left)); // đi vào từ phía đối diện

            victim.session.abortSession(); // rớt mạng giữa chừng
            CHECK(waitUntil([&] { return controller.currentRole() == ControlRole::Idle; }));
            CHECK(!controller.isHookActive());
            CHECK(!controller.isPeerConnected("fake-session-peer"));
            CHECK(injector.absoluteMoves.size() == absBefore + 1); // con trỏ được lùi khỏi biên (qua injector giả)
            if (InputHook* hook = controller.hookForTesting())
            {
                CHECK(hook->onRawMouseMove(60, 60, 50, 50) == false); // chuột/phím đã thật sự về lại máy này
                CHECK(hook->onRawKey(0x44, 32, true, false) == false);
                CHECK(hook->onRawKey(0x44, 32, false, false) == false);
            }

            // D13. Máy bị điều khiển TỪ CHỐI (trả ControlHandoff(None)): controller cũng tắt hook
            FakeSessionPeer refuser;
            refuser.myId = "fake-session-peer";
            refuser.peerId = myIdentity.id;
            refuser.longTermKey = fakeKey;
            refuser.connectTo(controller.listenPort());
            CHECK(waitUntil([&] { return refuser.established && controller.isPeerConnected("fake-session-peer"); }));
            CHECK(controller.requestControlForTesting("fake-session-peer", ScreenSide::Bottom));
            CHECK(controller.isHookActive());
            CHECK(waitUntil([&] { return !refuser.received.isEmpty(); }));
            ProtocolMessage refuse;
            refuse.type = MessageType::ControlHandoff;
            refuse.intA = static_cast<int>(ScreenSide::None);
            QString e;
            CHECK(refuser.session.sendMessage(refuse, &e));
            CHECK(waitUntil([&] { return controller.currentRole() == ControlRole::Idle; }));
            CHECK(!controller.isHookActive());
            CHECK(controller.isPeerConnected("fake-session-peer")); // từ chối điều khiển không làm rớt phiên
            refuser.session.abortSession();
            CHECK(waitUntil([&] { return !controller.isPeerConnected("fake-session-peer"); }));
        }

        // ---- E. Hạn bắt tay: kết nối đến không gửi gì bị đóng sau handshakeTimeoutMs ----
        {
            QTcpSocket silent;
            silent.connectToHost(QHostAddress::LocalHost, controller.listenPort());
            CHECK(waitUntil([&] { return silent.state() == QAbstractSocket::ConnectedState; }));
            CHECK(waitUntil([&] { return controller.pendingLinkCountForTesting() == 1; }));
            CHECK(waitUntil([&] { return silent.state() == QAbstractSocket::UnconnectedState; }, 4000));
            CHECK(controller.pendingLinkCountForTesting() == 0);
        }

        // ---- F. Trần số kết nối đến chưa bắt tay xong ----
        {
            std::vector<std::unique_ptr<QTcpSocket>> flood;
            for (int i = 0; i < 40; ++i)
            {
                flood.push_back(std::make_unique<QTcpSocket>());
                flood.back()->connectToHost(QHostAddress::LocalHost, controller.listenPort());
            }
            pump(400);
            CHECK(controller.pendingLinkCountForTesting() <= 16);
            CHECK(controller.pendingLinkCountForTesting() > 0);
            // Đang bị "ngập" vẫn bắt tay được với máy thật (kết nối cũ nhất bị nhường chỗ)
            FakeSessionPeer peer;
            peer.myId = "fake-session-peer";
            peer.peerId = myIdentity.id;
            peer.longTermKey = fakeKey;
            peer.connectTo(controller.listenPort());
            CHECK(waitUntil([&] { return peer.established; }));
            CHECK(controller.isPeerConnected("fake-session-peer"));
            flood.clear();
            CHECK(waitUntil([&] { return controller.pendingLinkCountForTesting() == 0; }, 4000));
            peer.session.abortSession();
            CHECK(waitUntil([&] { return !controller.isPeerConnected("fake-session-peer"); }));
        }

        // ---- G. Quản lý peer: đổi vị trí, tắt tự kết nối, quên máy ----
        controller.setPeerSide("fake-peer-id-aaa", ScreenSide::Right);
        {
            const auto peers = controller.pairedPeers();
            const auto it = std::find_if(peers.begin(), peers.end(),
                                         [](const PairedPeer& p) { return p.id == "fake-peer-id-aaa"; });
            CHECK(it != peers.end() && it->side == ScreenSide::Right);
        }
        {
            const auto peers = controller.pairedPeers();
            const auto it = std::find_if(peers.begin(), peers.end(),
                                         [](const PairedPeer& p) { return p.id == "fake-peer-id-bbb"; });
            CHECK(it != peers.end() && it->autoConnect == false);
        }

        const int beforeForget = controller.pairedPeers().size();
        controller.forgetPeer("fake-peer-id-aaa");
        CHECK(controller.pairedPeers().size() == beforeForget - 1);
        CHECK(controller.currentRole() == ControlRole::Idle);
        CHECK(!controller.isHookActive());
        CHECK(injector.buttons.isEmpty()); // suốt cả mục này không có nút chuột nào bị "tiêm"

        controller.stop();
        CHECK(!controller.isRunning());
        controller.stop(); // gọi lại khi đã dừng -> không crash

        // Chạy lại được sau khi dừng (đóng rồi mở lại cửa sổ Connect Together)
        CHECK(controller.start(&startErr));
        CHECK(controller.isRunning() && controller.listenPort() != 0);
        controller.stop();
    }

    // ---- HAI ConnectSessionController thật nối với nhau qua loopback (danh tính + kho peer riêng):
    // ghép đôi thật, bắt tay phiên hai chiều khi cả hai cùng mở kết nối chéo, trao quyền điều khiển được
    // bên nhận CHẤP NHẬN, chuyển tiếp input, Ctrl+Alt+Home, rớt kết nối khi đang điều khiển. ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        PeerStore storeA(tmp.filePath("peers_a.json"));
        PeerStore storeB(tmp.filePath("peers_b.json"));
        FakeInjector injectorA, injectorB;

        auto configure = [](ConnectSessionController& c, const QString& id, const QString& name, PeerStore* store,
                            FakeInjector* injector) {
            ConnectSessionController::TestConfig cfg;
            cfg.identity.id = id;
            cfg.identity.machineName = name;
            cfg.store = store;
            cfg.injector = injector;
            cfg.installGlobalHook = false;   // hook KHÔNG được cài -> setActive(true) chỉ là một cờ, không chặn gì
            cfg.enableDiscovery = false;     // không phát quảng bá ra LAN thật; kết nối lại theo địa chỉ đã lưu
            cfg.enableClipboardSync = false; // không đụng clipboard thật
            cfg.usePreferredPort = false;
            cfg.housekeepingIntervalMs = 100;
            c.configureForTesting(cfg);
        };

        // id của A nhỏ hơn id của B -> kết nối do A mở là kết nối "ưu tiên" khi hai máy mở chéo cùng lúc
        ConnectSessionController a, b;
        configure(a, "aaaa-may-a", "May A", &storeA, &injectorA);
        configure(b, "bbbb-may-b", "May B", &storeB, &injectorB);
        QString err;
        CHECK(a.start(&err));
        CHECK(b.start(&err));
        CHECK(a.listenPort() != 0 && b.listenPort() != 0 && a.listenPort() != b.listenPort());

        QList<QString> failedA, failedB;
        int succeededA = 0, succeededB = 0;
        QObject::connect(&a, &ConnectSessionController::pairingFailed, [&](QString r) { failedA << r; });
        QObject::connect(&b, &ConnectSessionController::pairingFailed, [&](QString r) { failedB << r; });
        QObject::connect(&a, &ConnectSessionController::pairingSucceeded, [&](QString, QString) { ++succeededA; });
        QObject::connect(&b, &ConnectSessionController::pairingSucceeded, [&](QString, QString) { ++succeededB; });

        // ---- Sai mã (đúng định dạng) giữa hai controller THẬT: bên nhập được báo lỗi, không ai thêm peer ----
        const QString code = a.beginPairingSession();
        const QString wrongCode = code == "314159265" ? "271828182" : "314159265";
        b.connectWithCode(QHostAddress::LocalHost, a.listenPort(), wrongCode);
        CHECK(waitUntil([&] { return !failedB.isEmpty(); }));
        CHECK(succeededA == 0 && succeededB == 0);
        CHECK(a.pairedPeers().isEmpty() && b.pairedPeers().isEmpty());
        CHECK(a.isPairingSessionOpen()); // một lần sai chưa hủy mã
        CHECK(waitUntil([&] { return b.pendingLinkCountForTesting() == 0 && a.pendingLinkCountForTesting() == 0; }));

        // ---- Ghép đôi thật bằng đúng mã ----
        failedB.clear();
        b.connectWithCode(QHostAddress::LocalHost, a.listenPort(), code);
        CHECK(waitUntil([&] { return succeededA == 1 && succeededB == 1; }));
        CHECK(failedA.isEmpty() && failedB.isEmpty());
        CHECK(!a.isPairingSessionOpen());
        CHECK(storeA.find("bbbb-may-b") != nullptr && storeB.find("aaaa-may-a") != nullptr);
        if (storeA.find("bbbb-may-b") && storeB.find("aaaa-may-a"))
        {
            CHECK(storeA.find("bbbb-may-b")->longTermKey == storeB.find("aaaa-may-a")->longTermKey);
            CHECK(storeA.find("bbbb-may-b")->machineName == "May B");
            CHECK(storeB.find("aaaa-may-a")->machineName == "May A");
            CHECK(storeA.find("bbbb-may-b")->lastPort == b.listenPort()); // cổng lắng nghe, không phải cổng nguồn
            CHECK(storeB.find("aaaa-may-a")->lastPort == a.listenPort());
        }

        // ---- Bắt tay phiên: ngay sau khi ghép đôi, CẢ HAI máy cùng tự mở kết nối tới nhau (không có gói
        // quảng bá nào - discovery đang tắt). Phải hội tụ về đúng MỘT kết nối, hai đầu cùng có phiên. ----
        CHECK(waitUntil([&] { return a.isPeerConnected("bbbb-may-b") && b.isPeerConnected("aaaa-may-a"); }, 6000));
        CHECK(waitUntil([&] { return a.pendingLinkCountForTesting() == 0 && b.pendingLinkCountForTesting() == 0; }, 6000));
        pump(700); // đủ lâu để mọi kết nối trùng bị dọn và không có gì tự rớt
        CHECK(a.isPeerConnected("bbbb-may-b") && b.isPeerConnected("aaaa-may-a"));
        CHECK(a.pendingLinkCountForTesting() == 0 && b.pendingLinkCountForTesting() == 0);
        CHECK(a.currentRole() == ControlRole::Idle && b.currentRole() == ControlRole::Idle);
        CHECK(!a.isHookActive() && !b.isHookActive());

        // ---- Trao quyền điều khiển A -> B: BÊN NHẬN PHẢI CHẤP NHẬN (bản 1: lệnh đi qua kết nối mà đầu B
        // chưa đăng ký nên B luôn bỏ qua - A bật hook nuốt input trong khi B không hề bị điều khiển) ----
        a.setPeerSide("bbbb-may-b", ScreenSide::Right);
        CHECK(a.requestControlForTesting("bbbb-may-b", ScreenSide::Right));
        CHECK(a.currentRole() == ControlRole::Controlling);
        CHECK(a.isHookActive());
        CHECK(a.activePeerName() == "May B");
        CHECK(waitUntil([&] { return b.currentRole() == ControlRole::BeingControlled; }));
        CHECK(b.activePeerName() == "May A");
        CHECK(injectorB.absoluteMoves.size() == 1); // con trỏ B được đặt vào điểm vào - qua injector GIẢ
        CHECK(!b.isHookActive());
        CHECK(!a.requestControlForTesting("bbbb-may-b", ScreenSide::Right)); // đang điều khiển rồi

        // ---- Chuyển tiếp input: sự kiện "phần cứng" của A (gọi thẳng onRaw* như callback hook) tới B ----
        InputHook* hookA = a.hookForTesting();
        CHECK(hookA != nullptr && !hookA->isInstalled()); // không có hook toàn cục thật nào được cài
        if (hookA)
        {
            CHECK(hookA->onRawKey(0x41, 30, true, false) == true);
            CHECK(hookA->onRawKey(0x41, 30, false, false) == true);
            CHECK(waitUntil([&] { return injectorB.countKey(0x41, true) == 1 && injectorB.countKey(0x41, false) == 1; }));
            CHECK(hookA->onRawMouseMove(110, 95, 100, 100) == true);
            CHECK(waitUntil([&] { return injectorB.relativeMoves.size() == 1; }));
            CHECK(!injectorB.relativeMoves.isEmpty() && injectorB.relativeMoves.first() == QPoint(10, -5));
            CHECK(hookA->onRawMouseButton(0, true) == true);
            CHECK(hookA->onRawMouseWheel(120, 0) == true);
            CHECK(waitUntil([&] { return injectorB.buttons.size() == 1 && injectorB.wheelEvents == 1; }));
            CHECK(injectorA.keys.isEmpty() && injectorA.relativeMoves.isEmpty()); // A không tự tiêm gì vào chính nó

            // ---- Ctrl+Alt+Home trên A: Ctrl và Alt đã được chuyển sang B dưới dạng "nhấn" - khi trả quyền
            // A phải gửi lệnh NHẢ cho chúng (và cho nút chuột trái còn đang giữ), nếu không B kẹt phím ----
            CHECK(hookA->onRawKey(VK_LCONTROL, 29, true, false) == true);
            CHECK(hookA->onRawKey(VK_LMENU, 56, true, false) == true);
            CHECK(waitUntil([&] { return injectorB.countKey(VK_LCONTROL, true) == 1 && injectorB.countKey(VK_LMENU, true) == 1; }));
            const int absMovesABefore = injectorA.absoluteMoves.size();
            CHECK(hookA->onRawKey(VK_HOME, 71, true, true) == true); // nuốt, không lọt xuống máy A
            CHECK(a.currentRole() == ControlRole::Idle);
            CHECK(!a.isHookActive());
            CHECK(injectorA.absoluteMoves.size() == absMovesABefore + 1); // con trỏ A được lùi vào trong biên
            CHECK(waitUntil([&] { return b.currentRole() == ControlRole::Idle; }));
            CHECK(injectorB.countKey(VK_LCONTROL, false) == 1);
            CHECK(injectorB.countKey(VK_LMENU, false) == 1);
            CHECK(injectorB.countKey(VK_HOME, true) == 0); // phím Home của tổ hợp không tới B
            CHECK(injectorB.buttons.size() == 2 && injectorB.buttons.last() == qMakePair(0, false));
            // Lần nhả Ctrl/Alt/Home vật lý xảy ra SAU khi đã trả quyền: bị nuốt cho khớp cặp, không gửi đi đâu
            const int keysBBefore = injectorB.keys.size();
            CHECK(hookA->onRawKey(VK_HOME, 71, false, true) == true);
            CHECK(hookA->onRawKey(VK_LMENU, 56, false, false) == true);
            CHECK(hookA->onRawKey(VK_LCONTROL, 29, false, false) == true);
            CHECK(hookA->onRawMouseButton(0, false) == true);
            pump(150);
            CHECK(injectorB.keys.size() == keysBBefore);
            // Đã về Idle: gõ phím trên A không còn đi đâu cả
            CHECK(hookA->onRawKey(0x42, 48, true, false) == false);
            CHECK(hookA->onRawKey(0x42, 48, false, false) == false);
            pump(100);
            CHECK(injectorB.keys.size() == keysBBefore);
        }
        CHECK(a.isPeerConnected("bbbb-may-b") && b.isPeerConnected("aaaa-may-a")); // trả quyền không làm rớt phiên

        // ---- Người ngồi ở máy BỊ điều khiển bấm Ctrl+Alt+Home: ngắt được ngay, máy điều khiển tắt hook ----
        CHECK(a.requestControlForTesting("bbbb-may-b", ScreenSide::Right));
        CHECK(waitUntil([&] { return b.currentRole() == ControlRole::BeingControlled; }));
        if (InputHook* hookB = b.hookForTesting())
        {
            hookB->onRawKey(VK_LCONTROL, 29, true, false);
            hookB->onRawKey(VK_LMENU, 56, true, false);
            hookB->onRawKey(VK_HOME, 71, true, true);
            CHECK(b.currentRole() == ControlRole::Idle);
            CHECK(waitUntil([&] { return a.currentRole() == ControlRole::Idle; }));
            CHECK(!a.isHookActive());
            hookB->onRawKey(VK_HOME, 71, false, true);
            hookB->onRawKey(VK_LMENU, 56, false, false);
            hookB->onRawKey(VK_LCONTROL, 29, false, false);
        }

        // ---- Hai máy cùng đòi điều khiển nhau một lúc: không ai bị kẹt ở Controlling với hook bật ----
        CHECK(a.requestControlForTesting("bbbb-may-b", ScreenSide::Right));
        b.setPeerSide("aaaa-may-a", ScreenSide::Left);
        CHECK(b.requestControlForTesting("aaaa-may-a", ScreenSide::Left)); // B chưa kịp nhận lệnh của A
        CHECK(waitUntil([&] { return a.currentRole() == ControlRole::Idle && b.currentRole() == ControlRole::Idle; }));
        CHECK(!a.isHookActive() && !b.isHookActive());

        // ---- Rớt kết nối khi đang điều khiển: hook PHẢI tắt, vai trò về Idle (bản 1: role về Idle nhưng
        // hook vẫn nuốt mọi chuột/phím, Ctrl+Alt+Home vô tác dụng - người dùng bị khóa input) ----
        CHECK(a.requestControlForTesting("bbbb-may-b", ScreenSide::Right));
        CHECK(a.isHookActive());
        CHECK(waitUntil([&] { return b.currentRole() == ControlRole::BeingControlled; }));
        if (hookA)
        {
            CHECK(hookA->onRawKey(VK_SHIFT, 42, true, false) == true); // B đang "giữ" Shift theo lệnh A
            CHECK(waitUntil([&] { return injectorB.countKey(VK_SHIFT, true) == 1; }));
        }
        const int absMovesA = injectorA.absoluteMoves.size();
        b.stop(); // máy B biến mất giữa chừng
        CHECK(injectorB.countKey(VK_SHIFT, false) == 1); // B tự nhả phím đang giữ hộ trước khi dừng
        CHECK(waitUntil([&] { return a.currentRole() == ControlRole::Idle; }));
        CHECK(!a.isHookActive());
        CHECK(!a.isPeerConnected("bbbb-may-b"));
        CHECK(injectorA.absoluteMoves.size() == absMovesA + 1); // con trỏ A được lùi khỏi biên
        if (hookA)
        {
            // Chuột/phím của A đã thật sự về lại A: không còn bị nuốt
            CHECK(hookA->onRawMouseMove(50, 50, 40, 40) == false);
            CHECK(hookA->onRawKey(0x43, 46, true, false) == false);
            CHECK(hookA->onRawKey(0x43, 46, false, false) == false);
            hookA->onRawKey(VK_SHIFT, 42, false, false);
        }
        CHECK(!a.requestControlForTesting("bbbb-may-b", ScreenSide::Right)); // không còn phiên -> không bật hook
        CHECK(!a.isHookActive());

        // ---- B chạy lại (cổng lắng nghe có thể đổi): hai máy tự nối lại theo địa chỉ đã lưu, không cần
        // quảng bá UDP; A học được cổng mới của B qua SessionHello ----
        CHECK(b.start(&err));
        CHECK(waitUntil([&] { return a.isPeerConnected("bbbb-may-b") && b.isPeerConnected("aaaa-may-a"); }, 8000));
        CHECK(waitUntil([&] { return storeA.find("bbbb-may-b") && storeA.find("bbbb-may-b")->lastPort == b.listenPort(); }, 3000));
        CHECK(a.requestControlForTesting("bbbb-may-b", ScreenSide::Right));
        CHECK(waitUntil([&] { return b.currentRole() == ControlRole::BeingControlled; }));

        // ---- "Quên" máy đang điều khiển: hook tắt, vai trò về Idle ở cả hai máy ----
        a.forgetPeer("bbbb-may-b");
        CHECK(a.currentRole() == ControlRole::Idle);
        CHECK(!a.isHookActive());
        CHECK(storeA.find("bbbb-may-b") == nullptr);
        CHECK(waitUntil([&] { return b.currentRole() == ControlRole::Idle; }));
        CHECK(waitUntil([&] { return !b.isPeerConnected("aaaa-may-a"); }));
        // B vẫn còn nhớ A và thử nối lại, nhưng A không còn nhận -> không bao giờ có phiên
        b.reconnectNow();
        pump(500);
        CHECK(!a.isPeerConnected("bbbb-may-b") && !b.isPeerConnected("aaaa-may-a"));

        a.stop();
        b.stop();
        CHECK(!a.isRunning() && !b.isRunning());
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
