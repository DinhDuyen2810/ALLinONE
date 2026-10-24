// CÔNG CỤ KIỂM TRA TAY (không chạy như test tự động - quy tắc 5): gửi IKEv1 Aggressive msg1 THẬT tới một
// gateway FortiGate IPsec để xem (a) UDP 500/4500 có thông không, (b) gateway có trả lời không, (c) nhóm DH
// nào được chấp nhận. KHÔNG đăng nhập, KHÔNG gửi PSK/tài khoản - chỉ chào Phase 1 rồi in phản hồi (hex + phân
// tích). Dùng: ike_real_probe <ip> [port]   (mặc định 119.17.206.59 500).
//   cmake --build build --target ike_real_probe && build\ike_real_probe.exe 119.17.206.59
#include "tools/vpn/direct/IkeCrypto.h"
#include "tools/vpn/direct/IsakmpMessage.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QUdpSocket>
#include <cstdio>

using namespace Isakmp;

static QByteArray be32b(quint32 v)
{
    QByteArray b(4, '\0');
    b[0] = char((v >> 24) & 0xFF); b[1] = char((v >> 16) & 0xFF); b[2] = char((v >> 8) & 0xFF); b[3] = char(v & 0xFF);
    return b;
}

// Một transform Phase 1 (enc/keylen/hash/group/auth).
static QByteArray p1Transform(quint8 num, quint16 enc, quint16 keyBits, quint16 hash, quint16 grp, quint16 auth, bool last)
{
    QByteArray a;
    a += attribute(EncAlg, enc);
    a += attribute(KeyLength, keyBits);
    a += attribute(HashAlg, hash);
    a += attribute(AuthMethod, auth);
    a += attribute(GroupDesc, grp);
    a += attribute(LifeType, 1);
    a += attributeVar(LifeDuration, be32b(86400));
    return transform(num, 1 /*KEY_IKE*/, a, last);
}

// Chào nhiều transform (enc/hash/auth) nhưng CÙNG một nhóm DH (Aggressive gửi 1 KE nên khóa 1 nhóm).
static QByteArray buildMsg1(quint16 grp, const QByteArray& ke, const QByteArray& cookie, const QByteArray& nonce)
{
    QByteArray tfs;
    quint8 n = 1;
    const quint16 auths[] = {AUTH_XAUTH_INIT_PSK, AUTH_PSK};
    const quint16 encs[] = {ENC_AES_CBC};
    const quint16 keys[] = {256, 128};
    const quint16 hashes[] = {HASH_SHA256, HASH_SHA};
    QByteArray list;
    struct T { quint16 enc, key, hash, auth; };
    QList<T> all;
    for (quint16 au : auths) for (quint16 e : encs) for (quint16 k : keys) for (quint16 h : hashes) all.append({e, k, h, au});
    for (int i = 0; i < all.size(); ++i)
        list += p1Transform(static_cast<quint8>(i + 1), all[i].enc, all[i].key, all[i].hash, grp, all[i].auth, i + 1 == all.size());
    const QByteArray pr = proposal(1, 1 /*ISAKMP*/, QByteArray(), list, all.size(), true);
    const QByteArray sa = saPayload(pr);
    const QByteArray id = idPayload(ID_IPV4_ADDR, 0, 0, be32b(0));
    const QByteArray natVid = QByteArray::fromHex("4a131c81070358455c5728f20e95452f");

    Message m;
    m.initiatorCookie = cookie;
    m.responderCookie = QByteArray(8, '\0');
    m.exchange = Aggressive;
    m.messageId = 0;
    m.payloads = {{SA, sa}, {KE, ke}, {NONCE, nonce}, {ID, id}, {VID, natVid}};
    return m.encode();
}

static void dumpResponse(const QByteArray& r)
{
    std::printf("    <= NHAN %d byte\n", r.size());
    if (r.size() >= 28)
    {
        Message m;
        if (Message::decode(r, &m))
        {
            std::printf("       exchange=%d payloads=%d:", m.exchange, m.payloads.size());
            for (auto& p : m.payloads) std::printf(" %d", p.first);
            std::printf("\n");
            const QByteArray n = m.payload(NOTIFY);
            if (n.size() >= 8)
            {
                const quint16 type = static_cast<quint16>((static_cast<quint8>(n[6]) << 8) | static_cast<quint8>(n[7]));
                std::printf("       NOTIFY type=%d (14=NO-PROPOSAL-CHOSEN, 7=INVALID-PAYLOAD, 24=AUTH-FAILED)\n", type);
            }
            if (!m.payload(KE).isEmpty())
                std::printf("       CO KE+HASH => gateway CHAP NHAN nhom nay (Phase 1 di tiep duoc)\n");
            // Phân tích transform gateway CHỌN trong SA.
            const QByteArray sa = m.payload(SA);
            if (sa.size() >= 16)
            {
                const int spiSize = static_cast<quint8>(sa[8 + 4 + 2]);
                const int off = 8 + 4 + 4 + spiSize;
                if (off + 8 <= sa.size())
                {
                    const int tfLen = (static_cast<quint8>(sa[off + 2]) << 8) | static_cast<quint8>(sa[off + 3]);
                    const QByteArray attrs = sa.mid(off + 8, qMin(off + tfLen, sa.size()) - (off + 8));
                    quint32 enc = 0, klen = 0, hsh = 0, auth = 0, grp = 0;
                    forEachAttribute(attrs, [&](quint16 t, bool var, quint32 v, const QByteArray&) {
                        if (var) return;
                        if (t == EncAlg) enc = v; else if (t == KeyLength) klen = v; else if (t == HashAlg) hsh = v;
                        else if (t == AuthMethod) auth = v; else if (t == GroupDesc) grp = v;
                    });
                    std::printf("       GATEWAY CHON: enc=%u(7=AES) keylen=%u hash=%u(2=SHA1,4=SHA256) auth=%u(65001=XAUTHPSK,1=PSK) grp=%u\n",
                                enc, klen, hsh, auth, grp);
                }
            }
            // Có NAT-D => đang sau NAT => NAT-T (cổng 4500) sẽ được dùng.
            int natd = 0;
            for (auto& p : m.payloads) if (p.first == NATD) ++natd;
            if (natd) std::printf("       co %d NAT-D => sau NAT => dung NAT-T (UDP 4500)\n", natd);
        }
        else
            std::printf("       (khong decode duoc ISAKMP)\n");
    }
    std::printf("       hex[0..48]: %s\n", r.left(48).toHex(' ').constData());
}

// Gửi msg1 cho một nhóm rồi chờ phản hồi tối đa waitMs.
static bool probeGroup(QUdpSocket& sock, const QHostAddress& ip, quint16 port, bool natT, IkeCrypto::DhGroup g,
                       const char* gname, int waitMs)
{
    IkeCrypto::Dh dh;
    QString err;
    if (!dh.generate(g, &err)) { std::printf("  [%s] loi tao DH: %s\n", gname, err.toUtf8().constData()); return false; }
    const QByteArray cookie = IkeCrypto::randomBytes(8);
    const QByteArray nonce = IkeCrypto::randomBytes(20);
    QByteArray dg = buildMsg1(static_cast<quint16>(g), dh.publicKey(), cookie, nonce);
    if (natT) dg.prepend(QByteArray(4, '\0'));
    std::printf("  [%s cong %d%s] gui msg1 %d byte...\n", gname, port, natT ? " NAT-T" : "", dg.size());
    sock.writeDatagram(dg, ip, port);

    QElapsedTimer t; t.start();
    bool got = false;
    while (t.elapsed() < waitMs)
    {
        if (sock.waitForReadyRead(300))
        {
            while (sock.hasPendingDatagrams())
            {
                QByteArray r; r.resize(static_cast<int>(sock.pendingDatagramSize()));
                QHostAddress from; quint16 fp = 0;
                sock.readDatagram(r.data(), r.size(), &from, &fp);
                // Bóc dấu non-ESP nếu có.
                if (r.size() >= 4 && static_cast<quint8>(r[0]) == 0 && static_cast<quint8>(r[1]) == 0 &&
                    static_cast<quint8>(r[2]) == 0 && static_cast<quint8>(r[3]) == 0 && natT)
                    r = r.mid(4);
                // Khớp cookie initiator (8 byte đầu) để chắc là trả lời của ta.
                if (r.size() >= 8 && r.left(8) == cookie)
                {
                    dumpResponse(r);
                    got = true;
                }
                else
                    std::printf("    <= nhan %d byte (cookie khong khop - bo qua)\n", r.size());
            }
            if (got) break;
        }
    }
    if (!got) std::printf("    (khong co phan hoi trong %d ms)\n", waitMs);
    return got;
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QString ipStr = argc >= 2 ? QString::fromLatin1(argv[1]) : QStringLiteral("119.17.206.59");
    const quint16 port = argc >= 3 ? static_cast<quint16>(QString::fromLatin1(argv[2]).toInt()) : 500;
    QHostAddress ip(ipStr);
    std::printf("== Do IKEv1 Aggressive toi %s:%d (KHONG dang nhap) ==\n", ipStr.toUtf8().constData(), port);

    QUdpSocket sock;
    if (!sock.bind(QHostAddress(QHostAddress::AnyIPv4), 0)) { std::printf("khong bind duoc UDP\n"); return 1; }

    struct G { IkeCrypto::DhGroup g; const char* name; };
    const G groups[] = {{IkeCrypto::DhGroup::Ecp384, "ECP-384/grp20 (DHGroup=512)"},
                        {IkeCrypto::DhGroup::Ecp256, "ECP-256/grp19"},
                        {IkeCrypto::DhGroup::Ecp521, "ECP-521/grp21"},
                        {IkeCrypto::DhGroup::Modp2048, "MODP-2048/grp14"},
                        {IkeCrypto::DhGroup::Modp1536, "MODP-1536/grp5"},
                        {IkeCrypto::DhGroup::Modp1024, "MODP-1024/grp2"}};

    int responded = 0;
    for (const G& g : groups)
        if (probeGroup(sock, ip, port, port == 4500, g.g, g.name, 4000)) ++responded;

    std::printf("\n== KET LUAN ==\n");
    if (responded == 0)
        std::printf("Khong nhom MODP nao duoc tra loi. => Hoac UDP %d bi chan, hoac gateway dung nhom ECDH (19/20/21)\n"
                    "   nhu DHGroup=512 (grp20/ECP384) goi y. Buoc tiep: them ECDH, hoac mo rong Advanced Settings\n"
                    "   trong FortiClient de xem dung nhom DH/ma hoa.\n", port);
    else
        std::printf("Co %d nhom duoc tra loi - dung nhom do trong IkeSession::Config.\n", responded);
    return 0;
}
