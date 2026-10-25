// Test bắt tay IKEv1 TRỌN VẸN offline: IkeSession (client) đấu với một responder GIẢ trong test, dựng bằng
// chính IsakmpMessage/IkeCrypto/IkeKeys. Responder KIỂM HASH của client (HASH_I, XAuth, Quick Mode) để chứng
// minh client tính đúng dải byte; cuối cùng so khóa ESP hai bên dẫn xuất độc lập. Đây là kiểm chứng offline -
// quirk riêng của FortiGate thật (VID/kiểu ID) vẫn cần máy chủ thật.
// cmake --build build --target ike_session_check && build\ike_session_check.exe
#include "tools/vpn/direct/IkeCrypto.h"
#include "tools/vpn/direct/IkeKeys.h"
#include "tools/vpn/direct/IkeSession.h"
#include "tools/vpn/direct/IsakmpMessage.h"

#include <QCoreApplication>
#include <QList>
#include <cstdio>

using namespace Isakmp;
using IkeCrypto::Hash;

static int g_fail = 0, g_pass = 0;
#define CHECK(c)                                                            \
    do {                                                                    \
        if (c) ++g_pass;                                                    \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static quint32 rd32(const QByteArray& b, int off)
{
    return (static_cast<quint32>(static_cast<quint8>(b[off])) << 24) | (static_cast<quint32>(static_cast<quint8>(b[off + 1])) << 16) |
           (static_cast<quint32>(static_cast<quint8>(b[off + 2])) << 8) | static_cast<quint32>(static_cast<quint8>(b[off + 3]));
}
static QByteArray be32b(quint32 v)
{
    QByteArray b(4, '\0');
    b[0] = char((v >> 24) & 0xFF); b[1] = char((v >> 16) & 0xFF); b[2] = char((v >> 8) & 0xFF); b[3] = char(v & 0xFF);
    return b;
}
// Nối payload (có generic header) - khớp IkeSession::chain.
static QByteArray chain(const QList<QPair<quint8, QByteArray>>& ps)
{
    QByteArray b;
    for (int i = 0; i < ps.size(); ++i)
    {
        const quint8 next = (i + 1 < ps.size()) ? ps[i + 1].first : quint8(None);
        const QByteArray& d = ps[i].second;
        b.append(char(next)); b.append(char(0)); b.append(char((4 + d.size()) >> 8)); b.append(char((4 + d.size()) & 0xFF)); b.append(d);
    }
    return b;
}

// Dựng SA Phase 1 chỉ gồm MỘT transform (vai gateway chọn 1 trong các transform client chào).
static QByteArray p1ChosenSa(quint16 enc, quint16 keyBits, quint16 hash, quint16 grp, quint16 auth)
{
    QByteArray a = attribute(EncAlg, enc) + attribute(KeyLength, keyBits) + attribute(HashAlg, hash) +
                   attribute(AuthMethod, auth) + attribute(GroupDesc, grp) + attribute(LifeType, 1) +
                   attributeVar(LifeDuration, be32b(86400));
    QByteArray tf = transform(1, 1 /*KEY_IKE*/, a, true);
    QByteArray pr = proposal(1, 1 /*ISAKMP*/, QByteArray(), tf, 1, true);
    return saPayload(pr);
}

// Responder giả: vai gateway FortiGate tối giản nhưng ĐÚNG công thức IKEv1.
struct FakeGateway
{
    Hash H = Hash::Sha256;
    int encKeyBits = 256;
    IkeCrypto::DhGroup group = IkeCrypto::DhGroup::Ecp384; // như FortiGate thật (DHGroup=512 = grp20)
    QByteArray psk = "unit-test-psk";
    QString user = "alice", pass = "s3cret";
    quint32 gwIp = 0x0a000001; // 10.0.0.1
    quint32 assignIp = 0x0a140005, assignMask = 0xFFFFFF00, dns = 0x0a140001;

    IkeCrypto::Dh dh;
    QByteArray ckyI, ckyR, gxi, gxr, gxy, ni, nr, saiB, idiiB, idirB, encKey, phase1Iv;
    IkeKeys::Skeyids sk;
    QMap<quint32, QByteArray> iv;
    bool phase1 = false, xauthReplied = false, modecfgDone = false, quickDone = false;
    quint32 xauthMid = 0x55555555, spiServer = 0x0bad0bad, spiClient = 0, qmMid = 0;
    QByteArray qmNi, qmNr;

    QByteArray& ivFor(quint32 mid)
    {
        if (!iv.contains(mid)) iv[mid] = (mid == 0) ? phase1Iv : IkeKeys::phase2Iv(H, phase1Iv, mid, 16);
        return iv[mid];
    }
    QByteArray enc(Message& m)
    {
        m.initiatorCookie = ckyI; m.responderCookie = ckyR; m.flags |= 1;
        QByteArray clear = m.encode(), hdr = clear.left(28), pl = clear.mid(28);
        if (pl.size() % 16) pl.append(QByteArray(16 - pl.size() % 16, '\0'));
        QByteArray& v = ivFor(m.messageId);
        QByteArray ct = IkeCrypto::aesCbcEncrypt(encKey, v, pl);
        v = ct.right(16);
        quint32 total = 28 + ct.size();
        hdr[24] = char(total >> 24); hdr[25] = char(total >> 16); hdr[26] = char(total >> 8); hdr[27] = char(total);
        return hdr + ct;
    }
    bool dec(const QByteArray& raw, Message* out)
    {
        quint32 mid = rd32(raw, 20);
        QByteArray ct = raw.mid(28);
        QByteArray& v = ivFor(mid);
        QByteArray pt = IkeCrypto::aesCbcDecrypt(encKey, v, ct);
        v = ct.right(16);
        return Message::decode(raw.left(28) + pt, out);
    }

    QList<QByteArray> feed(const QByteArray& raw)
    {
        QList<QByteArray> out;
        const quint8 ex = quint8(raw[18]);
        if (ex == Aggressive && !phase1 && ckyR.isEmpty())
        {
            // msg1
            Message m; CHECK(Message::decode(raw, &m));
            ckyI = m.initiatorCookie; ckyR = IkeCrypto::randomBytes(8);
            saiB = m.payload(SA); gxi = m.payload(KE); ni = m.payload(NONCE); idiiB = m.payload(ID);
            QString e; CHECK(dh.generate(group, &e));
            gxr = dh.publicKey(); nr = IkeCrypto::randomBytes(20);
            gxy = dh.computeShared(gxi); CHECK(!gxy.isEmpty());
            QByteArray skeyid = IkeKeys::skeyidPsk(H, psk, ni, nr);
            sk = IkeKeys::deriveSkeyids(H, skeyid, gxy, ckyI, ckyR);
            encKey = IkeKeys::expandKey(H, sk.e, encKeyBits / 8);
            phase1Iv = IkeKeys::phase1Iv(H, gxi, gxr, 16);
            idirB = idPayload(ID_IPV4_ADDR, 0, 0, be32b(gwIp));
            // HASH_R dùng SAi_b = TOÀN BỘ đề xuất client (saiB). msg2 chỉ echo 1 transform đã chọn (grp20).
            QByteArray hr = IkeKeys::hashR(H, skeyid, gxr, gxi, ckyR, ckyI, saiB, idirB);
            const quint16 gnum = static_cast<quint16>(group);
            QByteArray chosenSa = p1ChosenSa(ENC_AES_CBC, static_cast<quint16>(encKeyBits),
                                             H == Hash::Sha256 ? HASH_SHA256 : HASH_SHA, gnum, AUTH_XAUTH_INIT_PSK);
            Message r; r.exchange = Aggressive; r.messageId = 0;
            r.initiatorCookie = ckyI; r.responderCookie = ckyR;
            r.payloads = {{SA, chosenSa}, {KE, gxr}, {NONCE, nr}, {ID, idirB}, {HASH, hr},
                          {VID, QByteArray::fromHex("4a131c81070358455c5728f20e95452f")}};
            out.append(r.encode());
            return out;
        }
        if (ex == Aggressive && !phase1)
        {
            // msg3: verify HASH_I
            Message m; CHECK(Message::decode(raw, &m));
            QByteArray hi = m.payload(HASH);
            QByteArray exp = IkeKeys::hashI(H, sk.skeyid, gxi, gxr, ckyI, ckyR, saiB, idiiB);
            CHECK(hi == exp);
            phase1 = true;
            // Khởi tạo XAuth: hỏi user/pass.
            QByteArray attrs = attribute(XAUTH_TYPE, 0) + attributeVar(XAUTH_USER_NAME, QByteArray()) + attributeVar(XAUTH_USER_PASSWORD, QByteArray());
            QByteArray cp = configPayload(CFG_REQUEST, 1, attrs);
            QByteArray hash = IkeKeys::genericHash(H, sk.a, xauthMid, chain({{CP, cp}}));
            Message r; r.exchange = Transaction; r.messageId = xauthMid; r.payloads = {{HASH, hash}, {CP, cp}};
            out.append(enc(r));
            return out;
        }
        if (ex == Transaction)
        {
            Message m; CHECK(dec(raw, &m));
            QByteArray cp = m.payload(CP);
            quint8 ct = 0; quint16 id = 0;
            QByteArray attrs = configAttrs(cp, &ct, &id);
            // Kiểm HASH của client.
            QByteArray exp = IkeKeys::genericHash(H, sk.a, m.messageId, chain({{CP, cp}}));
            CHECK(m.payload(HASH) == exp);
            if (ct == CFG_REPLY)
            {
                QByteArray gotUser, gotPass;
                forEachAttribute(attrs, [&](quint16 t, bool var, quint32, const QByteArray& v) {
                    if (t == XAUTH_USER_NAME && var) gotUser = v;
                    if (t == XAUTH_USER_PASSWORD && var) gotPass = v;
                });
                CHECK(gotUser == user.toUtf8() && gotPass == pass.toUtf8());
                xauthReplied = true;
                // Gửi SET status=1.
                QByteArray a = attribute(XAUTH_STATUS, 1);
                QByteArray scp = configPayload(CFG_SET, 2, a);
                QByteArray hash = IkeKeys::genericHash(H, sk.a, xauthMid, chain({{CP, scp}}));
                Message r; r.exchange = Transaction; r.messageId = xauthMid; r.payloads = {{HASH, hash}, {CP, scp}};
                out.append(enc(r));
                return out;
            }
            if (ct == CFG_ACK)
            {
                return out; // ACK của SET - không trả gì, chờ mode-config request
            }
            if (ct == CFG_REQUEST)
            {
                // Mode-config: cấp IP/DNS/mask.
                QByteArray a;
                a += attributeVar(INTERNAL_IP4_ADDRESS, be32b(assignIp));
                a += attributeVar(INTERNAL_IP4_NETMASK, be32b(assignMask));
                a += attributeVar(INTERNAL_IP4_DNS, be32b(dns));
                // Chia tuyến như FortiGate: INTERNAL_IP4_SUBNET (172.16.0.0/16) + UNITY_SPLIT_INCLUDE (mục 14 byte: địa chỉ,
                // mặt nạ, giao thức, cổng nguồn, cổng đích) liệt kê lại 172.16.0.0/16 và thêm 172.17.0.0/16.
                a += attributeVar(INTERNAL_IP4_SUBNET, be32b(0xAC100000u) + be32b(0xFFFF0000u));
                QByteArray split;
                for (quint32 net : {0xAC100000u, 0xAC110000u})
                    split += be32b(net) + be32b(0xFFFF0000u) + QByteArray(6, '\0');
                a += attributeVar(UNITY_SPLIT_INCLUDE, split);
                QByteArray rcp = configPayload(CFG_REPLY, id, a);
                QByteArray hash = IkeKeys::genericHash(H, sk.a, m.messageId, chain({{CP, rcp}}));
                Message r; r.exchange = Transaction; r.messageId = m.messageId; r.payloads = {{HASH, hash}, {CP, rcp}};
                modecfgDone = true;
                out.append(enc(r));
                return out;
            }
        }
        if (ex == QuickMode)
        {
            Message m; CHECK(dec(raw, &m));
            QByteArray sa = m.payload(SA);
            if (!sa.isEmpty())
            {
                // QM1: verify HASH1, đọc SPI client, trả QM2.
                qmMid = m.messageId; qmNi = m.payload(NONCE);
                QList<QPair<quint8, QByteArray>> after;
                bool afterHash = false;
                for (auto& p : m.payloads) { if (p.first == HASH) { afterHash = true; continue; } if (afterHash) after.append(p); }
                QByteArray exp = IkeKeys::qmHash1(H, sk.a, qmMid, chain(after));
                CHECK(m.payload(HASH) == exp);
                // SPI client: sa[16..19]
                spiClient = rd32(sa, 16);
                qmNr = IkeCrypto::randomBytes(20);
                QByteArray espAttrs = attribute(1, 1) + attributeVar(2, be32b(3600)) + attribute(4, 1) +
                                      attribute(5, H == Hash::Sha256 ? 5 : 2) + attribute(6, encKeyBits);
                QByteArray tf = transform(1, 12, espAttrs, true);
                QByteArray pr = proposal(1, 3, be32b(spiServer), tf, 1, true);
                QByteArray espSa = saPayload(pr);
                QByteArray idci = idPayload(ID_IPV4_ADDR, 0, 0, be32b(assignIp));
                QByteArray idcr = idPayload(ID_IPV4_SUBNET, 0, 0, be32b(0) + be32b(0));
                QList<QPair<quint8, QByteArray>> aft = {{SA, espSa}, {NONCE, qmNr}, {ID, idci}, {ID, idcr}};
                QByteArray hash = IkeKeys::qmHash2(H, sk.a, qmMid, qmNi, chain(aft));
                Message r; r.exchange = QuickMode; r.messageId = qmMid; r.payloads = {{HASH, hash}}; r.payloads += aft;
                out.append(enc(r));
                return out;
            }
            else
            {
                // QM3: verify HASH3.
                QByteArray exp = IkeKeys::qmHash3(H, sk.a, qmMid, qmNi, qmNr);
                CHECK(m.payload(HASH) == exp);
                quickDone = true;
                return out;
            }
        }
        return out;
    }
};

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    FakeGateway gw;
    gw.H = Hash::Sha256;
    gw.encKeyBits = 256;
    gw.group = IkeCrypto::DhGroup::Ecp384;

    IkeSession s;
    IkeSession::Config c;
    c.gatewayIp = gw.gwIp;
    c.psk = gw.psk;
    c.xauthUser = gw.user;
    c.xauthPass = gw.pass;
    c.localIdType = ID_IPV4_ADDR;
    c.dhGroup = IkeCrypto::DhGroup::Ecp384; // nhóm 20 như FortiGate thật
    c.encKeyBits = 256;                     // sẽ bị ghi đè theo transform gateway chọn (ở đây cũng 256)
    c.hash = Hash::Sha256;
    c.natT = true;
    s.configure(c);

    QList<QByteArray> toGw;
    bool estab = false;
    IkeSession::Result result;
    QString failReason;
    QObject::connect(&s, &IkeSession::sendDatagram, [&](QByteArray dg, bool) { toGw.append(dg); });
    QObject::connect(&s, &IkeSession::established, [&](IkeSession::Result r) { estab = true; result = r; });
    QObject::connect(&s, &IkeSession::failed, [&](QString r) { failReason = r; });

    s.start();
    // Vòng lặp ping-pong đồng bộ cho tới khi lặng hoặc hết lượt.
    int guard = 0;
    while (!toGw.isEmpty() && guard++ < 50)
    {
        QByteArray dg = toGw.takeFirst();
        for (const QByteArray& resp : gw.feed(dg))
            s.onDatagram(resp); // có thể nối thêm vào toGw
    }

    CHECK(failReason.isEmpty());
    CHECK(gw.phase1 && gw.xauthReplied && gw.modecfgDone && gw.quickDone);
    CHECK(estab);
    CHECK(result.assignedIp == gw.assignIp);
    CHECK(result.dns.size() == 1 && result.dns[0] == gw.dns);
    // Chia tuyến: 172.16.0.0/16 xuất hiện ở hai thuộc tính nhưng chỉ giữ MỘT; thêm 172.17.0.0/16 từ split-include.
    CHECK(result.subnetsBase.size() == 2 && result.subnetsMask.size() == 2);
    CHECK(result.subnetsBase.value(0) == 0xAC100000u && result.subnetsMask.value(0) == 0xFFFF0000u);
    CHECK(result.subnetsBase.value(1) == 0xAC110000u && result.subnetsMask.value(1) == 0xFFFF0000u);
    CHECK(result.esp.present && result.esp.spiOut == gw.spiServer && result.esp.spiIn == gw.spiClient);

    // Khóa ESP: client dẫn xuất phải khớp keymat tính độc lập từ các giá trị đã trao đổi.
    const int encLen = 32, hmacLen = 32; // aes-256 + sha256
    QByteArray kmOut = IkeKeys::keymat(gw.H, gw.sk.d, 3, gw.spiServer, gw.qmNi, gw.qmNr, encLen + hmacLen);
    QByteArray kmIn = IkeKeys::keymat(gw.H, gw.sk.d, 3, gw.spiClient, gw.qmNi, gw.qmNr, encLen + hmacLen);
    CHECK(result.esp.encKeyOut == kmOut.left(encLen) && result.esp.hmacKeyOut == kmOut.mid(encLen, hmacLen));
    CHECK(result.esp.encKeyIn == kmIn.left(encLen) && result.esp.hmacKeyIn == kmIn.mid(encLen, hmacLen));

    std::printf("IKE handshake offline: estab=%d assignedIp=%08x spiOut=%08x spiIn=%08x\n",
                estab ? 1 : 0, result.assignedIp, result.esp.spiOut, result.esp.spiIn);
    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
