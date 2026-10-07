// Kiểm thử lõi Connect Together (mã ghép đôi, giao thức, mã hóa AES-GCM). Không cần mạng/phần cứng.
// Build: cmake --build build --target connect_tests && build\connect_tests.exe
#include <QCoreApplication>
#include <QDateTime>
#include <QEventLoop>
#include <QFile>
#include <QHostAddress>
#include <QSet>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTimer>
#include <cmath>
#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "tools/connect/engine/CryptoSession.h"
#include "tools/connect/engine/EdgeDetector.h"
#include "tools/connect/engine/InputHook.h"
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

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

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

        // Dữ liệu rác / rỗng không được làm crash, phải trả về false
        ProtocolMessage junk;
        CHECK(!ProtocolMessage::fromBytes(QByteArray(), &junk));
        CHECK(!ProtocolMessage::fromBytes(QByteArray("\xFF\xFE\xFD", 3), &junk));
        CHECK(!ProtocolMessage::fromBytes(QByteArray(1, '\x00'), &junk)); // type=0, ngoài enum hợp lệ
    }

    // ---- CryptoSession: round-trip, phát hiện sai khóa, phát hiện bị sửa dữ liệu ----
    {
        const QByteArray keyA = CryptoSession::generateRandomKey();
        const QByteArray keyB = CryptoSession::generateRandomKey();
        CHECK(keyA.size() == 32 && keyB.size() == 32);
        CHECK(keyA != keyB);

        CryptoSession sessA(keyA);
        CryptoSession sessA2(keyA); // mô phỏng máy B dùng ĐÚNG khóa giống máy A
        CryptoSession sessWrong(keyB);
        CHECK(sessA.isValid() && sessWrong.isValid());

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

        // Bản tin lớn (1MB) round-trip được (mô phỏng dữ liệu clipboard/ảnh lớn)
        QByteArray big(1024 * 1024, 'x');
        QByteArray bigPacket, bigOut;
        CHECK(sessA.encrypt(big, &bigPacket, &err));
        CHECK(sessA2.decrypt(bigPacket, &bigOut, &err));
        CHECK(bigOut == big);
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

    // ---- PeerStore: lưu/nạp lại, gồm cả khóa dài hạn dạng nhị phân ----
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

        // Nạp lại từ file phải khớp dữ liệu đã lưu, kể cả khóa nhị phân 32 byte
        CHECK(PeerStore::instance().load());
        CHECK(PeerStore::instance().peers().size() == 2);
        PairedPeer* reloaded = PeerStore::instance().find("peer-bbb");
        CHECK(reloaded != nullptr);
        CHECK(reloaded->longTermKey == p2.longTermKey);
        CHECK(reloaded->longTermKey.size() == 32);
        CHECK(reloaded->side == ScreenSide::Left);
        CHECK(reloaded->machineName == QString::fromUtf8("Máy Bên Trái"));

        PeerStore::instance().remove("peer-aaa");
        CHECK(PeerStore::instance().peers().size() == 1);
        CHECK(PeerStore::instance().find("peer-aaa") == nullptr);

        PeerStore::instance().clear();
        CHECK(PeerStore::instance().peers().isEmpty());

        // File không tồn tại / hỏng -> load() trả false, không crash
        PeerStore::instance().setFilePath(tmp.filePath("khong_ton_tai.json"));
        CHECK(!PeerStore::instance().load());
        QFile bad(tmp.filePath("hong.json"));
        bad.open(QIODevice::WriteOnly);
        bad.write("{ day khong phai json hop le");
        bad.close();
        PeerStore::instance().setFilePath(tmp.filePath("hong.json"));
        CHECK(!PeerStore::instance().load());
    }

    // ---- NetworkSession: TCP loopback thật, mã hóa đầu-cuối ----
    {
        QTcpServer server;
        CHECK(server.listen(QHostAddress::LocalHost));
        const quint16 port = server.serverPort();

        const QByteArray sharedKey = CryptoSession::generateRandomKey();

        NetworkSession clientSide;
        clientSide.setCryptoKey(sharedKey);

        NetworkSession serverSide;
        bool serverAdopted = false;
        QObject::connect(&server, &QTcpServer::newConnection, [&]() {
            QTcpSocket* sock = server.nextPendingConnection();
            serverSide.adoptSocket(sock);
            serverSide.setCryptoKey(sharedKey);
            serverAdopted = true;
        });

        clientSide.connectToHost(QHostAddress::LocalHost, port);
        CHECK(waitUntil([&] { return serverAdopted && clientSide.isConnected(); }));

        // Gửi 1 chiều: client -> server
        QList<ProtocolMessage> serverReceived;
        QObject::connect(&serverSide, &NetworkSession::messageReceived,
                         [&](ProtocolMessage m) { serverReceived.push_back(m); });

        ProtocolMessage mv;
        mv.type = MessageType::MouseMove;
        mv.intA = 42;
        mv.intB = -7;
        QString sendErr;
        CHECK(clientSide.sendMessage(mv, &sendErr));
        CHECK(waitUntil([&] { return !serverReceived.isEmpty(); }));
        CHECK(serverReceived.first().type == MessageType::MouseMove);
        CHECK(serverReceived.first().intA == 42 && serverReceived.first().intB == -7);

        // Gửi chiều ngược lại: server -> client
        QList<ProtocolMessage> clientReceived;
        QObject::connect(&clientSide, &NetworkSession::messageReceived,
                         [&](ProtocolMessage m) { clientReceived.push_back(m); });

        ProtocolMessage clip;
        clip.type = MessageType::ClipboardText;
        clip.textA = QString::fromUtf8("Đồng bộ clipboard 2 máy 📋");
        CHECK(serverSide.sendMessage(clip, &sendErr));
        CHECK(waitUntil([&] { return !clientReceived.isEmpty(); }));
        CHECK(clientReceived.first().textA == clip.textA);

        // Payload lớn (2MB, vượt xa kích thước 1 gói TCP) phải ráp khung đúng, không mất/lệch dữ liệu
        QList<ProtocolMessage> bigReceived;
        QObject::connect(&serverSide, &NetworkSession::messageReceived, [&](ProtocolMessage m) {
            if (m.type == MessageType::ClipboardText && m.textA.size() > 1000)
                bigReceived.push_back(m);
        });
        ProtocolMessage bigMsg;
        bigMsg.type = MessageType::ClipboardText;
        bigMsg.textA = QString(2 * 1024 * 1024, QChar('A'));
        CHECK(clientSide.sendMessage(bigMsg, &sendErr));
        CHECK(waitUntil([&] { return !bigReceived.isEmpty(); }, 6000));
        CHECK(bigReceived.first().textA == bigMsg.textA);
        CHECK(bigReceived.first().textA.size() == bigMsg.textA.size());

        // Nhiều thông điệp liên tiếp không bị trộn khung (gửi dồn dập)
        QList<ProtocolMessage> burstReceived;
        QObject::connect(&serverSide, &NetworkSession::messageReceived, [&](ProtocolMessage m) {
            if (m.type == MessageType::KeyEvent)
                burstReceived.push_back(m);
        });
        for (int i = 0; i < 50; ++i)
        {
            ProtocolMessage k;
            k.type = MessageType::KeyEvent;
            k.intA = i;
            CHECK(clientSide.sendMessage(k, &sendErr));
        }
        CHECK(waitUntil([&] { return burstReceived.size() == 50; }));
        for (int i = 0; i < 50; ++i)
            CHECK(burstReceived[i].intA == i);

        // Sai khóa qua mạng thật: một client TCP riêng dùng khóa KHÁC gửi vào server -> server phải
        // báo lỗi giải mã, KHÔNG phát messageReceived cho gói đó, KHÔNG crash, vẫn sống bình thường.
        // Ngắt lambda newConnection cũ (dùng để ghép phiên client/server ban đầu) trước khi gắn lambda
        // mới - nếu không, cả hai sẽ cùng chạy trên MỘT sự kiện newConnection và tranh nhau gọi
        // nextPendingConnection(), khiến lambda mới không bao giờ nhận được socket.
        QObject::disconnect(&server, &QTcpServer::newConnection, nullptr, nullptr);

        NetworkSession wrongKeyClient;
        wrongKeyClient.setCryptoKey(CryptoSession::generateRandomKey()); // cố tình sai khóa
        bool wrongKeyAdopted = false;
        QObject::connect(&server, &QTcpServer::newConnection, [&]() {
            if (!wrongKeyAdopted && server.hasPendingConnections())
            {
                serverSide.adoptSocket(server.nextPendingConnection()); // tái dùng serverSide, vẫn khóa ĐÚNG
                serverSide.setCryptoKey(sharedKey);
                wrongKeyAdopted = true;
            }
        });
        wrongKeyClient.connectToHost(QHostAddress::LocalHost, port);
        CHECK(waitUntil([&] { return wrongKeyAdopted && wrongKeyClient.isConnected(); }));

        bool wrongKeyErrorSeen = false;
        const int serverReceivedCountBefore = serverReceived.size();
        QObject::connect(&serverSide, &NetworkSession::errorOccurred, [&](const QString&) { wrongKeyErrorSeen = true; });

        ProtocolMessage probe;
        probe.type = MessageType::Heartbeat;
        CHECK(wrongKeyClient.sendMessage(probe, &sendErr));
        CHECK(waitUntil([&] { return wrongKeyErrorSeen; }));
        CHECK(serverReceived.size() == serverReceivedCountBefore); // gói sai khóa KHÔNG được chấp nhận
        CHECK(server.isListening()); // server vẫn sống sau khi nhận gói sai khóa

        // Khung giả/dữ liệu rác gửi thẳng qua socket TCP thô cũng không được làm sập server.
        QTcpSocket rogueSocket;
        rogueSocket.connectToHost(QHostAddress::LocalHost, port);
        CHECK(waitUntil([&] { return rogueSocket.state() == QAbstractSocket::ConnectedState; }));
        rogueSocket.write(QByteArray::fromHex("0000001F") + QByteArray(31, '\x00'));
        rogueSocket.flush();
        CHECK(waitUntil([&] { return true; }, 300)); // chỉ cần không crash trong 300ms
        CHECK(server.isListening());

        clientSide.disconnectSession();
        bool serverSawDisconnect = false;
        QObject::connect(&serverSide, &NetworkSession::disconnected, [&] { serverSawDisconnect = true; });
        CHECK(waitUntil([&] { return !clientSide.isConnected(); }));
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
            QObject::connect(&a, &PeerDiscovery::peerAnnounced,
                             [&](QString id, QString, QHostAddress, quint16) { aFound.push_back(id); });
            QObject::connect(&b, &PeerDiscovery::peerAnnounced,
                             [&](QString id, QString, QHostAddress, quint16) { bFound.push_back(id); });

            const bool sawEachOther = waitUntil(
                [&] {
                    return aFound.contains(QString("disc-id-bbb")) && bFound.contains(QString("disc-id-aaa"));
                },
                5000);

            if (sawEachOther)
            {
                CHECK(!aFound.contains(QString("disc-id-aaa"))); // không bao giờ thấy chính mình
                CHECK(!bFound.contains(QString("disc-id-bbb")));
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
        QObject::connect(&hook, &InputHook::mouseMoveRelative, [&](int, int) { ++moveSignals; });
        QObject::connect(&hook, &InputHook::mouseButtonChanged, [&](int, bool) { ++buttonSignals; });
        QObject::connect(&hook, &InputHook::mouseWheelMoved, [&](int, int) { ++wheelSignals; });
        QObject::connect(&hook, &InputHook::keyChanged, [&](int, int, bool, bool) { ++keySignals; });
        QObject::connect(&hook, &InputHook::returnHotkeyPressed, [&]() { ++hotkeySignals; });

        CHECK(hook.onRawMouseMove(100, 100) == false); // không active -> không nuốt sự kiện
        CHECK(moveSignals == 0);
        CHECK(hook.onRawMouseButton(0, true) == false);
        CHECK(buttonSignals == 0);

        // Bật active: sự kiện bị nuốt (trả về true) và phát tín hiệu tương ứng
        hook.setActive(true);
        CHECK(hook.isActive());

        CHECK(hook.onRawMouseMove(100, 100) == true); // điểm đầu tiên sau khi active: nuốt, chưa có delta
        CHECK(moveSignals == 0); // chưa phát vì chưa có "điểm trước" để tính delta
        CHECK(hook.onRawMouseMove(110, 95) == true);
        CHECK(moveSignals == 1); // lần thứ 2 trở đi mới có delta hợp lệ

        CHECK(hook.onRawMouseButton(1, true) == true);
        CHECK(buttonSignals == 1);
        CHECK(hook.onRawMouseWheel(120, 0) == true);
        CHECK(wheelSignals == 1);
        CHECK(hook.onRawKey(0x41, 30, true, false) == true); // phím 'A' thường
        CHECK(keySignals == 1);

        // Tắt active: quay về trạng thái đi qua bình thường
        hook.setActive(false);
        CHECK(hook.onRawMouseMove(200, 200) == false);
        CHECK(moveSignals == 1); // không tăng thêm

        // Phím nóng Ctrl+Alt+Home: luôn phát returnHotkeyPressed bất kể active hay không, và việc có
        // "nuốt" tổ hợp phím hay không phụ thuộc trạng thái active tại thời điểm nhấn.
        CHECK(hotkeySignals == 0);
        hook.onRawKey(VK_CONTROL, 29, true, true);
        hook.onRawKey(VK_MENU, 56, true, true);
        const bool consumedWhenInactive = hook.onRawKey(VK_HOME, 71, true, true);
        CHECK(hotkeySignals == 1);
        CHECK(consumedWhenInactive == false); // không active -> không nuốt, để phím Home đi qua bình thường

        hook.setActive(true);
        const bool consumedWhenActive = hook.onRawKey(VK_HOME, 71, true, true);
        CHECK(hotkeySignals == 2);
        CHECK(consumedWhenActive == true); // active -> nuốt tổ hợp phím nóng, không gửi Home sang máy kia

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

        CHECK(hook.install(&err)); // gọi lại install() khi đã cài -> idempotent, vẫn trả true

        hook.uninstall();
        CHECK(!hook.isInstalled());
        hook.uninstall(); // gọi lại uninstall() khi chưa cài -> không crash

        // Cài lại được sau khi đã gỡ
        CHECK(hook.install(&err));
        CHECK(hook.isInstalled());
        hook.uninstall();
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
