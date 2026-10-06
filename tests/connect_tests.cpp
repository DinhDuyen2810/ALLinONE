// Kiểm thử lõi Connect Together (mã ghép đôi, giao thức, mã hóa AES-GCM). Không cần mạng/phần cứng.
// Build: cmake --build build --target connect_tests && build\connect_tests.exe
#include <QCoreApplication>
#include <cstdio>

#include "tools/connect/engine/CryptoSession.h"
#include "tools/connect/engine/PairingCode.h"
#include "tools/connect/engine/ProtocolMessage.h"

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

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
