// Test nền IKEv1 crypto (thuần, BCrypt): DH round-trip, HMAC/băm theo VECTOR CHUẨN, AES-CBC round-trip,
// và lớp bản tin ISAKMP (encode/decode, lồng SA/proposal/transform, thuộc tính, config).
// cmake --build build --target ike_crypto_check && build\ike_crypto_check.exe
#include "tools/vpn/direct/IkeCrypto.h"
#include "tools/vpn/direct/IkeKeys.h"
#include "tools/vpn/direct/IsakmpMessage.h"

#include <QByteArray>
#include <cstdio>

static int g_fail = 0, g_pass = 0;
#define CHECK(c)                                                            \
    do {                                                                    \
        if (c) ++g_pass;                                                    \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

int main()
{
    using namespace IkeCrypto;

    // ---- Băm: vector chuẩn ----
    CHECK(hash(Hash::Sha1, "abc").toHex() == "a9993e364706816aba3e25717850c26c9cd0d89d");
    CHECK(hash(Hash::Sha256, "abc").toHex() == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(hash(Hash::Md5, "abc").toHex() == "900150983cd24fb0d6963f7d28e17f72");

    // ---- HMAC: vector RFC 2202 / RFC 4231 ----
    // HMAC-SHA1(key=0x0b*20, "Hi There")
    CHECK(prf(Hash::Sha1, QByteArray(20, '\x0b'), "Hi There").toHex() == "b617318655057264e28bc0b6fb378c8ef146be00");
    // HMAC-SHA256(key=0x0b*20, "Hi There")
    CHECK(prf(Hash::Sha256, QByteArray(20, '\x0b'), "Hi There").toHex() ==
          "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7");
    // HMAC-MD5(key=0x0b*16, "Hi There")
    CHECK(prf(Hash::Md5, QByteArray(16, '\x0b'), "Hi There").toHex() == "9294727a3638bb1c13f48ef8158bfc9d");
    // HMAC-SHA1(key="Jefe", "what do ya want for nothing?")
    CHECK(prf(Hash::Sha1, "Jefe", "what do ya want for nothing?").toHex() == "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79");

    // ---- prf+ : đủ độ dài, nối các khối ----
    const QByteArray k = QByteArray::fromHex("0102030405060708090a0b0c0d0e0f10");
    const QByteArray seed = "seed-xyz";
    const QByteArray p = prfPlus(Hash::Sha1, k, seed, 50);
    CHECK(p.size() == 50);
    // T1 = prf(k, seed|01); đầu ra bắt đầu bằng T1.
    CHECK(p.left(20) == prf(Hash::Sha1, k, seed + QByteArray(1, '\x01')));

    // ---- AES-CBC: round-trip + vector NIST (AES-128-CBC) ----
    {
        const QByteArray key = QByteArray::fromHex("2b7e151628aed2a6abf7158809cf4f3c");
        const QByteArray iv = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f");
        const QByteArray pt = QByteArray::fromHex("6bc1bee22e409f96e93d7e117393172a");
        const QByteArray ct = aesCbcEncrypt(key, iv, pt);
        CHECK(ct.toHex() == "7649abac8119b246cee98e9b12e9197d");
        CHECK(aesCbcDecrypt(key, iv, ct) == pt);
    }
    {
        // Nhiều khối, AES-256.
        const QByteArray key = randomBytes(32), iv = randomBytes(16), pt = randomBytes(64);
        CHECK(aesCbcDecrypt(key, iv, aesCbcEncrypt(key, iv, pt)) == pt);
    }

    // ---- Diffie-Hellman: hai bên ra cùng bí mật chung (các nhóm) ----
    for (DhGroup g : {DhGroup::Modp1024, DhGroup::Modp1536, DhGroup::Modp2048})
    {
        Dh a, b;
        QString ea, eb;
        CHECK(a.generate(g, &ea) && b.generate(g, &eb));
        const int sz = g == DhGroup::Modp1024 ? 128 : (g == DhGroup::Modp1536 ? 192 : 256);
        CHECK(a.publicKey().size() == sz && b.publicKey().size() == sz);
        const QByteArray sa = a.computeShared(b.publicKey());
        const QByteArray sb = b.computeShared(a.publicKey());
        CHECK(!sa.isEmpty() && sa == sb && sa.size() == sz);
        std::printf("DH group %d: shared %d byte OK\n", static_cast<int>(g), sa.size());
        // Public key sai độ dài -> rỗng.
        CHECK(a.computeShared(QByteArray(10, 'x')).isEmpty());
    }

    // ---- ISAKMP: thuộc tính TV/TLV round-trip ----
    {
        using namespace Isakmp;
        QByteArray a = attribute(EncAlg, ENC_AES_CBC) + attribute(KeyLength, 256) + attributeVar(LifeDuration, QByteArray::fromHex("00015180"));
        int seen = 0;
        quint32 enc = 0, klen = 0;
        QByteArray dur;
        const bool ok = forEachAttribute(a, [&](quint16 t, bool var, quint32 val, const QByteArray& v) {
            ++seen;
            if (t == EncAlg && !var) enc = val;
            if (t == KeyLength && !var) klen = val;
            if (t == LifeDuration && var) dur = v;
        });
        CHECK(ok && seen == 3 && enc == ENC_AES_CBC && klen == 256 && dur.toHex() == "00015180");
    }

    // ---- ISAKMP: encode/decode bản tin nhiều payload (Aggressive mode msg1 giả) ----
    {
        using namespace Isakmp;
        Message m;
        m.initiatorCookie = QByteArray::fromHex("1122334455667788");
        m.responderCookie = QByteArray(8, '\0');
        m.exchange = Aggressive;
        m.messageId = 0;
        const QByteArray ke = IkeCrypto::randomBytes(128);
        const QByteArray nonce = IkeCrypto::randomBytes(20);
        const QByteArray id = idPayload(ID_USER_FQDN, 17, 500, QByteArray("user@corp"));
        m.payloads = {{SA, saPayload(proposal(1, 1, QByteArray(), transform(1, 1, attribute(GroupDesc, 14), true), 1, true))},
                      {KE, ke}, {NONCE, nonce}, {ID, id}};

        const QByteArray raw = m.encode();
        Message d;
        CHECK(Message::decode(raw, &d));
        CHECK(d.initiatorCookie == m.initiatorCookie && d.exchange == Aggressive && d.firstPayload == SA);
        CHECK(d.payloads.size() == 4);
        CHECK(d.payload(KE) == ke && d.payload(NONCE) == nonce && d.payload(ID) == id);
        // Lồng SA: DOI+Situation rồi tới proposal/transform; attrs transform phải khôi phục được.
        const QByteArray sa = d.payload(SA);
        CHECK(sa.size() == 24 + 4 && sa.left(8).toHex() == "0000000100000001"); // attrs = 1 attribute 4 byte
        const QByteArray attrs = sa.mid(24);
        quint32 grp = 0;
        forEachAttribute(attrs, [&](quint16 t, bool, quint32 val, const QByteArray&) { if (t == GroupDesc) grp = val; });
        CHECK(grp == 14);
    }

    // ---- ISAKMP: config payload (Mode-config/XAuth) round-trip ----
    {
        using namespace Isakmp;
        const QByteArray attrs = attributeVar(XAUTH_USER_NAME, QByteArray("alice")) +
                                 attributeVar(XAUTH_USER_PASSWORD, QByteArray("secret"));
        const QByteArray cp = configPayload(CFG_REPLY, 0x1234, attrs);
        quint8 ct = 0;
        quint16 ident = 0;
        const QByteArray got = configAttrs(cp, &ct, &ident);
        CHECK(ct == CFG_REPLY && ident == 0x1234 && got == attrs);
        QByteArray user, pass;
        forEachAttribute(got, [&](quint16 t, bool var, quint32, const QByteArray& v) {
            if (t == XAUTH_USER_NAME && var) user = v;
            if (t == XAUTH_USER_PASSWORD && var) pass = v;
        });
        CHECK(user == "alice" && pass == "secret");
    }

    // ---- IkeKeys: hai vai initiator/responder nhất quán (SKEYID, HASH_I/R, KEYMAT, IV, mã hóa phiên) ----
    {
        using namespace IkeKeys;
        const Hash H = Hash::Sha1;
        // Bí mật DH chung thật (đã test ở trên), cùng cookie/nonce cho hai bên.
        Dh da, db;
        QString e1, e2;
        CHECK(da.generate(DhGroup::Modp1024, &e1) && db.generate(DhGroup::Modp1024, &e2));
        const QByteArray gxi = da.publicKey(), gxr = db.publicKey();
        const QByteArray gxy_i = da.computeShared(gxr), gxy_r = db.computeShared(gxi);
        CHECK(gxy_i == gxy_r && !gxy_i.isEmpty());

        const QByteArray psk = "topsecret-psk";
        const QByteArray ni = IkeCrypto::randomBytes(20), nr = IkeCrypto::randomBytes(20);
        const QByteArray ckyI = IkeCrypto::randomBytes(8), ckyR = IkeCrypto::randomBytes(8);
        const QByteArray saiB = IkeCrypto::randomBytes(48), idiiB = IkeCrypto::randomBytes(12), idirB = IkeCrypto::randomBytes(8);

        const QByteArray sk = skeyidPsk(H, psk, ni, nr);
        const Skeyids s = deriveSkeyids(H, sk, gxy_i, ckyI, ckyR);
        CHECK(!s.d.isEmpty() && !s.a.isEmpty() && !s.e.isEmpty());

        // HASH_I do initiator dựng; responder kiểm bằng CÙNG công thức -> phải khớp.
        const QByteArray hi = hashI(H, sk, gxi, gxr, ckyI, ckyR, saiB, idiiB);
        const QByteArray hiCheck = hashI(H, sk, gxi, gxr, ckyI, ckyR, saiB, idiiB);
        CHECK(hi == hiCheck && hi.size() == 20);
        // HASH_R do responder dựng; initiator kiểm.
        const QByteArray hr = hashR(H, sk, gxr, gxi, ckyR, ckyI, saiB, idirB);
        CHECK(hr != hi && hr.size() == 20);

        // KEYMAT: khóa chiều ra (SPI máy chủ) và chiều vào (SPI ta) khác nhau, cùng công thức hai bên ra trùng.
        const quint32 spiI = 0xAABBCCDD, spiR = 0x11223344;
        const int need = 16 + 20; // AES-128 + HMAC-SHA1
        const QByteArray kmOut = keymat(H, s.d, 3, spiR, ni, nr, need); // ta GỬI -> SPI máy chủ
        const QByteArray kmIn = keymat(H, s.d, 3, spiI, ni, nr, need);  // ta NHẬN -> SPI ta
        CHECK(kmOut.size() == need && kmIn.size() == need && kmOut != kmIn);
        CHECK(keymat(H, s.d, 3, spiR, ni, nr, need) == kmOut); // tất định

        // Mở rộng khóa mã hóa + IV + mã hóa/giải mã bản tin phiên (round-trip).
        const QByteArray encKey = expandKey(H, s.e, 32); // AES-256
        CHECK(encKey.size() == 32);
        const QByteArray iv1 = phase1Iv(H, gxi, gxr, 16);
        CHECK(iv1.size() == 16);
        const QByteArray iv2 = phase2Iv(H, iv1, 0x01020304, 16);
        CHECK(iv2.size() == 16 && iv2 != iv1);
        const QByteArray msg = IkeCrypto::randomBytes(48);
        CHECK(IkeCrypto::aesCbcDecrypt(encKey, iv2, IkeCrypto::aesCbcEncrypt(encKey, iv2, msg)) == msg);

        // Quick Mode HASH(3) khác HASH(1).
        CHECK(qmHash3(H, s.a, 7, ni, nr) != qmHash1(H, s.a, 7, saiB));
    }

    // ---- ISAKMP: bản tin hỏng bị từ chối ----
    {
        using namespace Isakmp;
        Message d;
        CHECK(!Message::decode(QByteArray(10, '\0'), &d));                 // quá ngắn
        QByteArray bad(28, '\0');
        bad[24] = '\xFF'; // length field > thực tế
        CHECK(!Message::decode(bad, &d));
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
