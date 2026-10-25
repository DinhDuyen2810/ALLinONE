#include "IkeSession.h"

#include "DirectVpnTypes.h"

#include <QCryptographicHash>

namespace
{
using namespace Isakmp;

quint16 rd16(const QByteArray& b, int off) { return static_cast<quint16>((static_cast<quint8>(b[off]) << 8) | static_cast<quint8>(b[off + 1])); }
quint32 rd32(const QByteArray& b, int off)
{
    return (static_cast<quint32>(static_cast<quint8>(b[off])) << 24) | (static_cast<quint32>(static_cast<quint8>(b[off + 1])) << 16) |
           (static_cast<quint32>(static_cast<quint8>(b[off + 2])) << 8) | static_cast<quint32>(static_cast<quint8>(b[off + 3]));
}
QByteArray be32b(quint32 v)
{
    QByteArray b(4, '\0');
    b[0] = static_cast<char>((v >> 24) & 0xFF); b[1] = static_cast<char>((v >> 16) & 0xFF);
    b[2] = static_cast<char>((v >> 8) & 0xFF);  b[3] = static_cast<char>(v & 0xFF);
    return b;
}
QByteArray be16b(quint16 v)
{
    QByteArray b(2, '\0');
    b[0] = static_cast<char>((v >> 8) & 0xFF); b[1] = static_cast<char>(v & 0xFF);
    return b;
}
quint8 exchangeOf(const QByteArray& raw) { return raw.size() > 18 ? static_cast<quint8>(raw[18]) : 0; }
quint8 flagsOf(const QByteArray& raw) { return raw.size() > 19 ? static_cast<quint8>(raw[19]) : 0; }
quint32 msgIdOf(const QByteArray& raw) { return raw.size() >= 24 ? rd32(raw, 20) : 0; }

// VID NAT-T RFC 3947 = MD5("RFC 3947").
const QByteArray kNatTVid = QByteArray::fromHex("4a131c81070358455c5728f20e95452f");

// Vendor ID kiểu FortiClient. FortiClient (ipsec.exe) tính VID = MD5(chuỗi) - bảng chuỗi đọc từ chính tệp cài đặt:
// "draft-ietf-ipsec-nat-t-ike-0x", "RFC 3947", "draft-ietf-ipsra-isakmp-xauth-06.txt" (chỉ 8 byte đầu), "CISCO-UNITY"
// (14 byte đầu + 0100), "FRAGMENTATION", DPD (hằng số RFC 3706), và VID riêng Fortinet: "forticlient connect license" (FCC),
// "Fortinet Endpoint Control" (FEC), "FortiClient v4", "Forticlient EAP Extension". FortiGate dùng các VID này để nhận ra
// FortiClient; gateway trả lời VID "FORTIGATE" (12 byte đầu MD5 + 4 byte).
QByteArray md5Vid(const char* s) { return QCryptographicHash::hash(QByteArray(s), QCryptographicHash::Md5); }

QList<QByteArray> fortiClientVids(quint32 mask)
{
    QList<QByteArray> v;
    if (mask & 0x001) v << md5Vid("draft-ietf-ipsec-nat-t-ike-03") << md5Vid("draft-ietf-ipsec-nat-t-ike-02\n");
    if (mask & 0x002) v << md5Vid("draft-ietf-ipsra-isakmp-xauth-06.txt").left(8);
    if (mask & 0x004) v << QByteArray::fromHex("afcad71368a1f1c96b8696fc77570100"); // DPD (RFC 3706)
    if (mask & 0x008) v << md5Vid("CISCO-UNITY").left(14) + QByteArray::fromHex("0100");
    if (mask & 0x010) v << md5Vid("FRAGMENTATION");
    if (mask & 0x020) v << md5Vid("forticlient connect license"); // FCC
    if (mask & 0x040) v << md5Vid("Fortinet Endpoint Control");    // FEC
    if (mask & 0x080) v << md5Vid("FortiClient v4");
    if (mask & 0x100) v << md5Vid("Forticlient EAP Extension");
    return v;
}

int hmacKeyBytes(IkeCrypto::Hash h)
{
    switch (h) { case IkeCrypto::Hash::Sha1: return 20; case IkeCrypto::Hash::Sha256: return 32; case IkeCrypto::Hash::Md5: return 16; }
    return 20;
}
} // namespace

IkeSession::IkeSession(QObject* parent) : QObject(parent) {}
IkeSession::~IkeSession() = default;

int IkeSession::hmacKeyLen() const { return hmacKeyBytes(m_cfg.hash); }

QByteArray IkeSession::chain(const QList<QPair<quint8, QByteArray>>& payloads)
{
    QByteArray body;
    for (int i = 0; i < payloads.size(); ++i)
    {
        const quint8 next = (i + 1 < payloads.size()) ? payloads[i + 1].first : static_cast<quint8>(Isakmp::None);
        const QByteArray& d = payloads[i].second;
        body.append(static_cast<char>(next));
        body.append(static_cast<char>(0));
        body.append(static_cast<char>((4 + d.size()) >> 8));
        body.append(static_cast<char>((4 + d.size()) & 0xFF));
        body.append(d);
    }
    return body;
}

quint32 IkeSession::newMsgId()
{
    // message-ID ngẫu nhiên khác 0 cho trao đổi Phase 2 do ta khởi tạo.
    QByteArray r = IkeCrypto::randomBytes(4);
    quint32 v = rd32(r, 0);
    if (v == 0) v = 1;
    return v;
}

QByteArray& IkeSession::ivFor(quint32 msgid)
{
    if (!m_iv.contains(msgid))
        m_iv[msgid] = (msgid == 0) ? m_phase1Iv : IkeKeys::phase2Iv(m_cfg.hash, m_phase1Iv, msgid, blockSize());
    return m_iv[msgid];
}

QByteArray IkeSession::encryptMessage(Isakmp::Message& m)
{
    m.flags |= 0x01; // Encryption
    QByteArray clear = m.encode();
    QByteArray header = clear.left(28);
    QByteArray payload = clear.mid(28);
    const int rem = payload.size() % blockSize();
    if (rem != 0)
        payload.append(QByteArray(blockSize() - rem, '\0'));
    QByteArray& iv = ivFor(m.messageId);
    const QByteArray ct = IkeCrypto::aesCbcEncrypt(m_encKey, iv, payload);
    if (ct.size() >= blockSize())
        iv = ct.right(blockSize());
    const quint32 total = static_cast<quint32>(28 + ct.size());
    header[24] = static_cast<char>((total >> 24) & 0xFF); header[25] = static_cast<char>((total >> 16) & 0xFF);
    header[26] = static_cast<char>((total >> 8) & 0xFF);  header[27] = static_cast<char>(total & 0xFF);
    return header + ct;
}

bool IkeSession::decryptMessage(const QByteArray& raw, Isakmp::Message* out)
{
    if (raw.size() < 28 + blockSize())
        return false;
    const quint32 msgid = msgIdOf(raw);
    const QByteArray ct = raw.mid(28);
    if (ct.size() % blockSize() != 0)
        return false;
    QByteArray& iv = ivFor(msgid);
    const QByteArray pt = IkeCrypto::aesCbcDecrypt(m_encKey, iv, ct);
    if (pt.isEmpty())
        return false;
    // Chỉ đẩy IV khi bản tin GIẢI MÃ ĐÚNG - tránh một gói phát lại/lạc làm lệch chuỗi IV của message-ID này.
    if (!Isakmp::Message::decode(raw.left(28) + pt, out))
        return false;
    iv = ct.right(blockSize());
    return true;
}

void IkeSession::send(Isakmp::Message& m, bool encrypted)
{
    m.initiatorCookie = m_ckyI;
    m.responderCookie = m_ckyR;
    const QByteArray dg = encrypted ? encryptMessage(m) : m.encode();
    m_lastSent = dg;
    m_lastWasNatT = m_natActive;
    emit sendDatagram(dg, m_natActive);
}

void IkeSession::fail(const QString& why)
{
    if (m_state == State::Failed)
        return;
    m_state = State::Failed;
    emit log("IKE lỗi: " + why);
    emit failed(why);
}

void IkeSession::start()
{
    m_ckyI = IkeCrypto::randomBytes(8);
    m_ckyR = QByteArray(8, '\0');
    sendAggr1();
}

void IkeSession::stop() { m_state = State::Failed; }

QByteArray IkeSession::buildPhase1Sa()
{
    // Aggressive gửi 1 KE nên nhóm DH cố định = m_cfg.dhGroup; nhưng chào NHIỀU transform (enc/hash/auth) để
    // khớp cấu hình gateway. Transform gateway chọn được đọc lại trong msg2 (onAggr2) để dẫn xuất khóa cho đúng.
    const quint16 grp = static_cast<quint16>(m_cfg.dhGroup);
    struct T { quint16 enc, key, hash, auth; };
    QList<T> all;
    const quint16 auths[] = {AUTH_XAUTH_INIT_PSK, AUTH_PSK};
    const quint16 keys[] = {256, 128};
    const quint16 hashes[] = {HASH_SHA256, HASH_SHA};
    for (quint16 au : auths)
        for (quint16 k : keys)
            for (quint16 h : hashes)
                all.append({ENC_AES_CBC, k, h, au});

    QByteArray tfs;
    for (int i = 0; i < all.size(); ++i)
    {
        QByteArray attrs;
        attrs += attribute(EncAlg, all[i].enc);
        attrs += attribute(KeyLength, all[i].key);
        attrs += attribute(HashAlg, all[i].hash);
        attrs += attribute(AuthMethod, all[i].auth);
        attrs += attribute(GroupDesc, grp);
        attrs += attribute(LifeType, 1); // seconds
        attrs += attributeVar(LifeDuration, be32b(86400));
        tfs += transform(static_cast<quint8>(i + 1), 1 /*KEY_IKE*/, attrs, i + 1 == all.size());
    }
    const QByteArray pr = proposal(1, 1 /*ISAKMP*/, QByteArray(), tfs, all.size(), true);
    return saPayload(pr);
}

// Đọc transform Phase 1 gateway CHỌN trong SA của msg2 -> cập nhật hash + độ dài khóa để dẫn xuất đúng.
// SA = DOI(4)+Situation(4) + proposal[genhdr(4)+fixed(4)+spi] + transform[genhdr(4)+fixed(4)+attrs].
void IkeSession::applyChosenTransform(const QByteArray& sa)
{
    if (sa.size() < 16)
        return;
    const int spiSize = static_cast<quint8>(sa[8 + 4 + 2]);
    int off = 8 + 4 + 4 + spiSize; // tới transform đầu
    if (off + 8 > sa.size())
        return;
    const quint16 tfLen = static_cast<quint16>((static_cast<quint8>(sa[off + 2]) << 8) | static_cast<quint8>(sa[off + 3]));
    const int attrStart = off + 8;
    const int attrEnd = qMin(off + tfLen, sa.size());
    if (attrStart >= attrEnd)
        return;
    const QByteArray attrs = sa.mid(attrStart, attrEnd - attrStart);
    forEachAttribute(attrs, [&](quint16 t, bool var, quint32 val, const QByteArray&) {
        if (var)
            return;
        if (t == KeyLength)
            m_cfg.encKeyBits = static_cast<int>(val);
        else if (t == HashAlg)
            m_cfg.hash = (val == HASH_SHA256) ? IkeCrypto::Hash::Sha256
                        : (val == HASH_MD5 ? IkeCrypto::Hash::Md5 : IkeCrypto::Hash::Sha1);
    });
    emit log(QString("IKE: gateway chọn AES-%1 + %2").arg(m_cfg.encKeyBits)
                 .arg(m_cfg.hash == IkeCrypto::Hash::Sha256 ? "SHA256" : (m_cfg.hash == IkeCrypto::Hash::Md5 ? "MD5" : "SHA1")));
}

void IkeSession::sendAggr1()
{
    m_dh = std::make_unique<IkeCrypto::Dh>();
    QString err;
    if (!m_dh->generate(m_cfg.dhGroup, &err))
    {
        fail("không tạo được khóa DH: " + err);
        return;
    }
    m_gxi = m_dh->publicKey();
    m_ni = IkeCrypto::randomBytes(20);
    m_saiB = buildPhase1Sa();

    QByteArray idData;
    quint8 idType = m_cfg.localIdType;
    if (idType == ID_IPV4_ADDR)
        idData = m_cfg.localId.size() == 4 ? m_cfg.localId : QByteArray(4, '\0');
    else
        idData = m_cfg.localId;
    m_idiiB = idPayload(idType, 0, 0, idData);

    Isakmp::Message m;
    m.exchange = Aggressive;
    m.messageId = 0;
    m.payloads = {{SA, m_saiB}, {KE, m_gxi}, {NONCE, m_ni}, {ID, m_idiiB}, {VID, kNatTVid}};
    for (const QByteArray& vid : fortiClientVids(m_cfg.vidMask))
        m.payloads.append({VID, vid});
    send(m, false);
    m_state = State::Aggr1Sent;
    emit log(QString("IKE: gửi Aggressive msg1 (DH group %1, %2 payload)").arg(static_cast<int>(m_cfg.dhGroup)).arg(m.payloads.size()));
}

void IkeSession::onAggr2(const Isakmp::Message& m)
{
    m_ckyR = m.responderCookie;
    const QByteArray sa = m.payload(SA);
    m_gxr = m.payload(KE);
    m_nr = m.payload(NONCE);
    m_idirB = m.payload(ID);
    const QByteArray hashR = m.payload(HASH);
    if (m_gxr.isEmpty() || m_nr.isEmpty() || m_idirB.isEmpty() || hashR.isEmpty())
    {
        fail("msg2 thiếu payload (KE/Nonce/ID/HASH)");
        return;
    }
    // Phát hiện NAT-T: responder có gửi VID NAT-T không (có NAT-D payload nghĩa là đã bật).
    for (const auto& p : m.payloads)
        if (p.first == VID && p.second == kNatTVid) m_natDetected = true;
    if (!m.payload(NATD).isEmpty()) m_natDetected = true;

    applyChosenTransform(sa); // cập nhật m_cfg.hash + encKeyBits theo transform gateway chọn TRƯỚC khi dẫn xuất khóa

    m_gxy = m_dh->computeShared(m_gxr);
    if (m_gxy.isEmpty())
    {
        fail("tính bí mật DH thất bại (nhóm không khớp?)");
        return;
    }
    const QByteArray skeyid = IkeKeys::skeyidPsk(m_cfg.hash, m_cfg.psk, m_ni, m_nr);
    m_sk = IkeKeys::deriveSkeyids(m_cfg.hash, skeyid, m_gxy, m_ckyI, m_ckyR);
    m_encKey = IkeKeys::expandKey(m_cfg.hash, m_sk.e, encKeyLen());
    m_phase1Iv = IkeKeys::phase1Iv(m_cfg.hash, m_gxi, m_gxr, blockSize());

    const QByteArray expect = IkeKeys::hashR(m_cfg.hash, skeyid, m_gxr, m_gxi, m_ckyR, m_ckyI, m_saiB, m_idirB);
    if (expect != hashR)
    {
        // Không trùng: nhiều khi do kiểu ID/PSK sai - báo rõ để dò, nhưng vẫn dừng (không an toàn khi đi tiếp).
        fail("HASH_R không khớp (PSK sai, hoặc kiểu ID/thuật toán khác). Dừng.");
        return;
    }
    emit log("IKE: Phase 1 xong (HASH_R khớp). Chuyển msg3.");
    sendAggr3();
}

void IkeSession::sendAggr3()
{
    // NAT-T initiator-driven: float sang 4500 ngay từ msg3 (gateway mirror theo cổng ta dùng). Gửi kèm NAT-D.
    if (m_natDetected && m_cfg.natT)
    {
        m_natActive = true;
        emit switchToNatT();
    }
    const QByteArray skeyid = m_sk.skeyid;
    const QByteArray hashI = IkeKeys::hashI(m_cfg.hash, skeyid, m_gxi, m_gxr, m_ckyI, m_ckyR, m_saiB, m_idiiB);
    Isakmp::Message m;
    m.exchange = Aggressive;
    m.messageId = 0;
    m.payloads = {{HASH, hashI}};
    // NAT-D (RFC 3947): báo địa chỉ ta cho gateway để nó phát hiện TA sau NAT -> bật đóng gói UDP cho ESP.
    // Thiếu NAT-D thì FortiGate mặc định ESP thô (IP proto 50), không nhận ESP bọc UDP-4500 của ta (đường hầm "treo").
    if (m_natDetected && m_cfg.natT)
    {
        auto natd = [&](quint32 ip, quint16 port) {
            return IkeCrypto::hash(m_cfg.hash, m_ckyI + m_ckyR + be32b(ip) + be16b(port));
        };
        m.payloads.append({NATD, natd(m_cfg.gatewayIp, 500)}); // địa chỉ gateway (remote)
        // forceNatT: cố ý khai địa chỉ ta LỆCH (xor 1) để gateway luôn thấy NAT-D không khớp -> luôn dùng ESP bọc UDP-4500.
        // Cần khi máy có IP công cộng trực tiếp: socket UDP không gửi/nhận được ESP thô (IP proto 50).
        m.payloads.append({NATD, natd(m_cfg.forceNatT ? (m_localIp ^ 1u) : m_localIp, m_localPort)}); // địa chỉ ta (local)
    }
    // FortiClient gửi msg3 MÃ HÓA (cờ 0x01) và kèm Notify INITIAL-CONTACT (bắt gói thật: 172 byte = HASH + 2 NAT-D + Notify).
    if (m_cfg.initialContact)
        m.payloads.append({NOTIFY, notifyPayload(1 /*ISAKMP*/, m_ckyI + m_ckyR, 24578 /*INITIAL-CONTACT*/, QByteArray())});
    send(m, m_cfg.encMsg3); // mặc định KHÔNG mã hóa (đã float -> gửi trên 4500 có dấu)
    if (m_cfg.encMsg3)
        m_phase1Iv = ivFor(0); // chuỗi IV Phase 2 nối tiếp từ KHỐI MÃ CUỐI của msg3 (cả hai phía làm vậy)
    m_state = State::Phase1Done;
    emit log(QString("IKE: gửi msg3 (HASH_I%1). Chờ XAuth.").arg(m.payloads.size() > 1 ? " + NAT-D" : ""));
}

void IkeSession::onTransaction(const Isakmp::Message& m)
{
    const QByteArray cp = m.payload(CP);
    if (cp.isEmpty())
    {
        emit log("IKE: Transaction không có payload CP - bỏ qua.");
        return;
    }
    quint8 cfgType = 0;
    quint16 ident = 0;
    const QByteArray attrs = Isakmp::configAttrs(cp, &cfgType, &ident);

    bool hasXauthCred = false, hasStatus = false, hasIp = false;
    quint32 status = 0;
    Isakmp::forEachAttribute(attrs, [&](quint16 t, bool var, quint32 val, const QByteArray& v) {
        if (t == XAUTH_USER_NAME || t == XAUTH_USER_PASSWORD || t == XAUTH_MESSAGE || t == XAUTH_TYPE) hasXauthCred = true;
        if (t == XAUTH_STATUS && !var) { hasStatus = true; status = val; }
        if (t == INTERNAL_IP4_ADDRESS) hasIp = true;
        (void)v;
    });

    if (cfgType == CFG_REQUEST && hasXauthCred && !hasIp)
    {
        // XAuth: máy chủ hỏi user/pass -> trả lời (echo message-ID của máy chủ).
        QByteArray reply;
        reply += attribute(XAUTH_TYPE, 0); // Generic
        reply += attributeVar(XAUTH_USER_NAME, m_cfg.xauthUser.toUtf8());
        reply += attributeVar(XAUTH_USER_PASSWORD, m_cfg.xauthPass.toUtf8());
        const QByteArray cfg = configPayload(CFG_REPLY, ident, reply);
        const QByteArray after = chain({{CP, cfg}});
        const QByteArray hash = IkeKeys::genericHash(m_cfg.hash, m_sk.a, m.messageId, after);
        Isakmp::Message r;
        r.exchange = Transaction;
        r.messageId = m.messageId;
        r.payloads = {{HASH, hash}, {CP, cfg}};
        send(r, true);
        m_state = State::XauthWait;
        emit log("IKE: trả lời XAuth (user/pass).");
        return;
    }
    if (hasStatus)
    {
        // XAuth SET status -> ACK, rồi xin cấu hình (Mode-config).
        QByteArray ack = attribute(XAUTH_STATUS, status);
        const QByteArray cfg = configPayload(CFG_ACK, ident, ack);
        const QByteArray after = chain({{CP, cfg}});
        const QByteArray hash = IkeKeys::genericHash(m_cfg.hash, m_sk.a, m.messageId, after);
        Isakmp::Message r;
        r.exchange = Transaction;
        r.messageId = m.messageId;
        r.payloads = {{HASH, hash}, {CP, cfg}};
        send(r, true);
        if (status != 1)
        {
            fail("XAuth bị từ chối (sai tài khoản?).");
            return;
        }
        emit log("IKE: XAuth OK. Xin cấu hình (Mode-config).");
        sendModeCfgRequest();
        return;
    }
    if ((cfgType == CFG_REPLY || cfgType == CFG_SET) && hasIp)
    {
        // Mode-config: nhận IP/DNS/subnet.
        QStringList seen; // loại(độ dài) từng thuộc tính nhận được - chẩn đoán (không in giá trị)
        Isakmp::forEachAttribute(attrs, [&](quint16 t, bool var, quint32 val, const QByteArray& v) {
            (void)val;
            seen << QString("%1(%2)").arg(t).arg(var ? v.size() : 0);
            if (!var) return;
            if (t == INTERNAL_IP4_ADDRESS && v.size() == 4) m_result.assignedIp = rd32(v, 0);
            else if (t == INTERNAL_IP4_NETMASK && v.size() == 4) m_result.assignedMask = rd32(v, 0);
            else if (t == INTERNAL_IP4_DNS && v.size() == 4) m_result.dns.append(rd32(v, 0));
            else if (t == INTERNAL_IP4_SUBNET && v.size() == 8) { m_result.subnetsBase.append(rd32(v, 0)); m_result.subnetsMask.append(rd32(v, 4)); }
            else if (t == UNITY_SPLIT_INCLUDE)
            {
                // Cisco Unity split-include: mỗi mục 14 byte = địa chỉ(4) mặt nạ(4) giao thức(2) cổng nguồn(2) cổng đích(2).
                for (int i = 0; i + 14 <= v.size(); i += 14)
                {
                    m_result.subnetsBase.append(rd32(v, i));
                    m_result.subnetsMask.append(rd32(v, i + 4));
                }
            }
        });
        emit log(QString("IKE: Mode-config thuộc tính nhận được: %1").arg(seen.join(' ')));
        // INTERNAL_IP4_SUBNET và UNITY_SPLIT_INCLUDE thường liệt kê cùng các mạng -> bỏ trùng (giữ thứ tự).
        for (int i = 0; i < m_result.subnetsBase.size(); ++i)
            for (int j = m_result.subnetsBase.size() - 1; j > i; --j)
                if (m_result.subnetsBase[j] == m_result.subnetsBase[i] && m_result.subnetsMask[j] == m_result.subnetsMask[i])
                {
                    m_result.subnetsBase.removeAt(j);
                    m_result.subnetsMask.removeAt(j);
                }
        if (cfgType == CFG_SET)
        {
            // ACK lại CFG_SET.
            const QByteArray cfg = configPayload(CFG_ACK, ident, attribute(INTERNAL_IP4_ADDRESS, 0));
            const QByteArray after = chain({{CP, cfg}});
            const QByteArray hash = IkeKeys::genericHash(m_cfg.hash, m_sk.a, m.messageId, after);
            Isakmp::Message r; r.exchange = Transaction; r.messageId = m.messageId; r.payloads = {{HASH, hash}, {CP, cfg}};
            send(r, true);
        }
        emit log(QString("IKE: Mode-config cấp IP %1, %2 DNS, %3 subnet.")
                     .arg(DirectVpn::ipv4ToString(m_result.assignedIp)).arg(m_result.dns.size()).arg(m_result.subnetsBase.size()));
        sendQuick1();
        return;
    }
    emit log(QString("IKE: Transaction cfgType=%1 chưa xử lý.").arg(cfgType));
}

void IkeSession::sendModeCfgRequest()
{
    QByteArray attrs;
    attrs += attributeVar(INTERNAL_IP4_ADDRESS, QByteArray());
    attrs += attributeVar(INTERNAL_IP4_NETMASK, QByteArray());
    attrs += attributeVar(INTERNAL_IP4_DNS, QByteArray());
    attrs += attributeVar(INTERNAL_IP4_NBNS, QByteArray());
    if (m_cfg.cfgFortiClient)
    {
        // Bộ thuộc tính FortiClient xin trong Mode-config (đọc từ chuỗi "mode-cfg request ..." của ipsec.exe):
        // APPLICATION_VERSION, IP4 subnet, IPv6 address/DNS/NBNS/subnet, UNITY_DEF_DOMAIN, UNITY_SPLIT_INCLUDE, UNITY_PFS.
        attrs += attributeVar(7, QByteArray("FortiClient 7.4.3"));  // APPLICATION_VERSION
        attrs += attributeVar(8, QByteArray());                    // INTERNAL_IP6_ADDRESS
        attrs += attributeVar(10, QByteArray());                   // INTERNAL_IP6_DNS
        attrs += attributeVar(11, QByteArray());                   // INTERNAL_IP6_NBNS
        attrs += attributeVar(INTERNAL_IP4_SUBNET, QByteArray());  // 13
        attrs += attributeVar(15, QByteArray());                   // INTERNAL_IP6_SUBNET
        attrs += attributeVar(UNITY_DEF_DOMAIN, QByteArray());
        attrs += attributeVar(UNITY_SPLIT_INCLUDE, QByteArray());
        attrs += attributeVar(UNITY_PFS, QByteArray());
    }
    const QByteArray cfg = configPayload(CFG_REQUEST, 1, attrs);
    const quint32 mid = newMsgId();
    const QByteArray after = chain({{CP, cfg}});
    const QByteArray hash = IkeKeys::genericHash(m_cfg.hash, m_sk.a, mid, after);
    Isakmp::Message r; r.exchange = Transaction; r.messageId = mid; r.payloads = {{HASH, hash}, {CP, cfg}};
    send(r, true);
    m_state = State::ModeCfgWait;
}

void IkeSession::sendQuick1()
{
    m_qmMsgId = newMsgId();
    QByteArray r = IkeCrypto::randomBytes(4);
    m_spiIn = rd32(r, 0);
    if (m_spiIn == 0) m_spiIn = 0x1000;
    m_qmNi = IkeCrypto::randomBytes(20);

    // PFS: FortiClient mặc định bật Perfect Forward Secrecy -> Quick Mode trao KE riêng (cùng nhóm Phase 1) và
    // transform mang thuộc tính Group. KEYMAT khi đó ghép g^xy(Quick Mode) vào đầu (RFC 2409 §5.5).
    quint16 pfsGroup = 0;
    m_qmDh.reset();
    m_qmGxy.clear();
    if (m_cfg.pfs)
    {
        m_qmDh = std::make_unique<IkeCrypto::Dh>();
        QString dhErr;
        if (!m_qmDh->generate(m_cfg.dhGroup, &dhErr))
        {
            fail("không tạo được khóa DH Quick Mode (PFS): " + dhErr);
            return;
        }
        pfsGroup = static_cast<quint16>(m_cfg.dhGroup);
    }

    // SA ESP: 1 proposal, NHIỀU transform (AES-256/128 × SHA256/SHA1 × encap UDP-Tunnel(3,NAT-T)/Tunnel(1)) -
    // gateway chọn 1; transform chọn được đọc lại trong QM2 (applyChosenEspTransform) để dẫn xuất khóa cho đúng.
    struct E { quint16 key, auth, encap; };
    QList<E> all;
    const quint16 keys[] = {256, 128};
    const quint16 auths[] = {5 /*HMAC-SHA256*/, 2 /*HMAC-SHA1*/};
    // Encap ESP: cấu hình được. Sau NAT, FortiClient chào UDP-Encapsulated-Tunnel (draft 61443) nên FortiGate bọc ESP
    // trong UDP-4500; nếu chỉ chào Tunnel(1) thuần, FortiGate có thể kỳ vọng ESP proto-50 thuần -> không qua được NAT.
    QList<quint16> encaps;
    switch (m_cfg.qmEncap)
    {
        case 1: encaps = {61443}; break;            // draft UDP-Tunnel (FortiOS cũ)
        case 2: encaps = {3}; break;                // RFC 3947 UDP-Encapsulated-Tunnel
        case 3: encaps = {61443, 3, 1}; break;      // đa transform, để gateway tự chọn
        default: encaps = {1}; break;               // Tunnel thuần (cũ)
    }
    for (quint16 k : keys)
        for (quint16 au : auths)
            for (quint16 en : encaps)
                all.append({k, au, en});
    QByteArray tfs;
    for (int i = 0; i < all.size(); ++i)
    {
        QByteArray a;
        a += attribute(1, 1);                   // SA Life Type = seconds
        a += attributeVar(2, be32b(3600));      // SA Life Duration
        if (pfsGroup)
            a += attribute(3, pfsGroup);        // Group Description (chỉ khi PFS)
        a += attribute(4, all[i].encap);        // Encapsulation Mode
        a += attribute(5, all[i].auth);         // Authentication Algorithm
        a += attribute(6, all[i].key);          // Key Length
        tfs += transform(static_cast<quint8>(i + 1), 12 /*ESP_AES*/, a, i + 1 == all.size());
    }
    const QByteArray pr = proposal(1, 3 /*ESP*/, be32b(m_spiIn), tfs, all.size(), true);
    const QByteArray espSa = saPayload(pr);
    m_qmEspSaB = espSa;
    // Mặc định (ghi đè khi đọc transform gateway chọn ở QM2).
    m_espKeyBits = 256;
    m_espEnc = "aes-256-cbc";
    m_espHmac = "sha256";

    // Proxy-ID. FortiClient dialup gửi IDci = IP ảo được cấp (ID_IPV4_ADDR, host đơn), IDcr = 0.0.0.0/0.
    // Nếu không có IP cấp hoặc tắt cờ thì lùi về 0.0.0.0/0 ↔ 0.0.0.0/0.
    QByteArray idci;
    if (m_cfg.qmIdAssigned && m_result.assignedIp != 0)
        idci = idPayload(ID_IPV4_ADDR, 0, 0, be32b(m_result.assignedIp)); // IP ảo/32 kiểu FortiClient
    else
        idci = idPayload(ID_IPV4_SUBNET, 0, 0, be32b(0) + be32b(0)); // 0.0.0.0/0
    const QByteArray idcr = idPayload(ID_IPV4_SUBNET, 0, 0, be32b(0) + be32b(0)); // 0.0.0.0/0

    // Thứ tự Quick Mode: SA, Ni, [KE nếu PFS], IDci, IDcr.
    QList<QPair<quint8, QByteArray>> after = {{SA, espSa}, {NONCE, m_qmNi}};
    if (m_qmDh)
        after.append({KE, m_qmDh->publicKey()});
    after.append({ID, idci});
    after.append({ID, idcr});
    const QByteArray hash = IkeKeys::qmHash1(m_cfg.hash, m_sk.a, m_qmMsgId, chain(after));
    Isakmp::Message m;
    m.exchange = QuickMode;
    m.messageId = m_qmMsgId;
    m.payloads = {{HASH, hash}};
    m.payloads += after;
    send(m, true);
    m_state = State::QuickWait;
    emit log("IKE: gửi Quick Mode msg1 (ESP SA).");
}

void IkeSession::onQuick2(const Isakmp::Message& m)
{
    const QByteArray sa = m.payload(SA);
    m_qmNr = m.payload(NONCE);
    if (sa.isEmpty() || m_qmNr.isEmpty())
    {
        fail("Quick Mode msg2 thiếu SA/Nonce");
        return;
    }
    // CHẨN ĐOÁN: selector (IDci/IDcr) gateway trả về = SA thực gateway đã cài. Lệch với traffic ta gửi -> gateway bỏ gói.
    {
        int idn = 0;
        for (const auto& p : m.payloads)
        {
            if (p.first != ID) continue;
            const QByteArray& b = p.second; // body: [idType][proto][port(2)][data...]
            if (b.size() < 4) continue;
            const quint8 t = static_cast<quint8>(b[0]);
            QString desc;
            if (t == ID_IPV4_ADDR && b.size() >= 8)
                desc = DirectVpn::ipv4ToString(rd32(b, 4)) + "/32";
            else if (t == ID_IPV4_SUBNET && b.size() >= 12)
                desc = DirectVpn::ipv4ToString(rd32(b, 4)) + " mask " + DirectVpn::ipv4ToString(rd32(b, 8));
            else
                desc = QString("idType=%1 (%2 byte)").arg(t).arg(b.size());
            emit log(QString("IKE: QM2 %1 = %2").arg(idn == 0 ? "IDci(nguồn)" : "IDcr(đích)", desc));
            ++idn;
        }
        if (idn == 0)
            emit log("IKE: QM2 KHÔNG có ID payload (gateway không echo selector).");
    }
    // SPI của máy chủ nằm trong proposal: skip DOI(4)+Sit(4) + prop hdr(4) + prop fixed(4)=16, spiSize ở byte 8+4+2.
    if (sa.size() >= 16 + 4)
    {
        const int spiSize = static_cast<quint8>(sa[8 + 4 + 2]);
        if (spiSize == 4 && sa.size() >= 16 + 4)
            m_spiOut = rd32(sa, 16);
    }
    if (m_spiOut == 0)
    {
        fail("Quick Mode: không đọc được SPI máy chủ");
        return;
    }
    applyChosenEspTransform(sa); // đọc enc/keylen/auth gateway chọn TRƯỚC khi dẫn xuất khóa ESP

    // PFS: tính bí mật DH Quick Mode từ KE của gateway (nếu có).
    const QByteArray peerKe = m.payload(KE);
    if (m_qmDh && !peerKe.isEmpty())
    {
        m_qmGxy = m_qmDh->computeShared(peerKe);
        if (m_qmGxy.isEmpty())
        {
            fail("Quick Mode PFS: tính bí mật DH thất bại");
            return;
        }
        emit log(QString("IKE: PFS Quick Mode - bí mật DH %1 byte").arg(m_qmGxy.size()));
    }
    // Gửi QM3 (HASH(3)) rồi dẫn xuất ESP.
    const QByteArray h3 = IkeKeys::qmHash3(m_cfg.hash, m_sk.a, m_qmMsgId, m_qmNi, m_qmNr);
    Isakmp::Message r; r.exchange = QuickMode; r.messageId = m_qmMsgId; r.payloads = {{HASH, h3}};
    send(r, true);
    deriveEsp();
}

// Đọc transform ESP gateway chọn trong QM2 (keylen/auth) -> cập nhật m_espKeyBits/m_espEnc/m_espHmac.
void IkeSession::applyChosenEspTransform(const QByteArray& sa)
{
    if (sa.size() < 16)
        return;
    const int spiSize = static_cast<quint8>(sa[8 + 4 + 2]);
    const int off = 8 + 4 + 4 + spiSize; // tới transform đầu
    if (off + 8 > sa.size())
        return;
    const quint16 tfLen = static_cast<quint16>((static_cast<quint8>(sa[off + 2]) << 8) | static_cast<quint8>(sa[off + 3]));
    const int attrStart = off + 8;
    const int attrEnd = qMin(off + tfLen, sa.size());
    if (attrStart >= attrEnd)
        return;
    const QByteArray attrs = sa.mid(attrStart, attrEnd - attrStart);
    quint32 auth = 0, encap = 0;
    forEachAttribute(attrs, [&](quint16 t, bool var, quint32 val, const QByteArray&) {
        if (var) return;
        if (t == 6 /*Key Length*/) m_espKeyBits = static_cast<int>(val);
        else if (t == 5 /*Auth Algo*/) auth = val;
        else if (t == 4 /*Encap Mode*/) encap = val;
    });
    m_espEnc = (m_espKeyBits == 128) ? "aes-128-cbc" : "aes-256-cbc";
    m_espHmac = (auth == 5) ? "sha256" : (auth == 1 ? "md5" : "sha1");
    emit log(QString("IKE: ESP gateway chọn %1 + %2, encap=%3 (1=Tunnel,3=UDP-Tunnel,61443=UDP-Tunnel draft)")
                 .arg(m_espEnc, m_espHmac).arg(encap));
}

void IkeSession::deriveEsp()
{
    const int espEncLen = (m_espKeyBits == 128) ? 16 : 32;
    const int espHmacLen = (m_espHmac == "sha256") ? 32 : (m_espHmac == "md5" ? 16 : 20);
    const int need = espEncLen + espHmacLen;

    // PFS: ghép g^xy(Quick Mode) vào KEYMAT (m_qmGxy rỗng nếu không PFS).
    const QByteArray kmOut = IkeKeys::keymat(m_cfg.hash, m_sk.d, 3, m_spiOut, m_qmNi, m_qmNr, need, m_qmGxy); // ta GỬI
    const QByteArray kmIn = IkeKeys::keymat(m_cfg.hash, m_sk.d, 3, m_spiIn, m_qmNi, m_qmNr, need, m_qmGxy);   // ta NHẬN

    GlobalProtectProtocol::EspConfig& e = m_result.esp;
    e.present = true;
    e.udpPort = 4500; // NAT-T
    e.encAlgo = m_espEnc;
    e.hmacAlgo = m_espHmac;
    e.spiOut = m_spiOut; // host order (GpEspCrypto htonl khi đặt lên dây)
    e.spiIn = m_spiIn;
    e.encKeyOut = kmOut.left(espEncLen);
    e.hmacKeyOut = kmOut.mid(espEncLen, espHmacLen);
    e.encKeyIn = kmIn.left(espEncLen);
    e.hmacKeyIn = kmIn.mid(espEncLen, espHmacLen);
    e.magicV4 = 0;

    m_result.gatewayIp = m_cfg.gatewayIp;
    // Lộ nguyên liệu KEYMAT cho công cụ kiểm tra tay (thử biến thể dẫn xuất khóa).
    m_result.kmSkeyidD = m_sk.d;
    m_result.kmGxy = m_qmGxy;
    m_result.kmNi = m_qmNi;
    m_result.kmNr = m_qmNr;
    m_result.kmHash = m_cfg.hash;
    m_result.kmEncLen = espEncLen;
    m_result.kmHmacLen = espHmacLen;
    m_state = State::Up;
    emit log(QString("IKE: Quick Mode xong. ESP %1/%2, SPI ra=%3 vào=%4. Đã lên.")
                 .arg(m_espEnc, m_espHmac).arg(m_spiOut, 0, 16).arg(m_spiIn, 0, 16));
    emit established(m_result);
}

void IkeSession::onInformational(const Isakmp::Message& m)
{
    const QByteArray n = m.payload(NOTIFY);
    if (n.size() >= 8)
    {
        const quint16 type = rd16(n, 6);
        emit log(QString("IKE: nhận Notify type=%1").arg(type));
        // DPD (RFC 3706): R-U-THERE(36136) -> phải trả R-U-THERE-ACK(36137), không thì gateway coi ta chết -> DELETE.
        if (type == 36136)
        {
            const int spiSize = static_cast<quint8>(n[5]);
            const QByteArray seqData = n.mid(8 + spiSize); // số thứ tự DPD
            const QByteArray ack = notifyPayload(1 /*ISAKMP*/, m_ckyI + m_ckyR, 36137, seqData);
            const quint32 mid = newMsgId();
            const QByteArray hash = IkeKeys::genericHash(m_cfg.hash, m_sk.a, mid, chain({{NOTIFY, ack}}));
            Isakmp::Message r;
            r.exchange = Informational;
            r.messageId = mid;
            r.payloads = {{HASH, hash}, {NOTIFY, ack}};
            send(r, true);
            emit log("IKE: trả lời DPD (R-U-THERE-ACK).");
            return;
        }
        if (type == 36137) { emit log("IKE: nhận R-U-THERE-ACK (gateway CÒN công nhận IKE SA của ta)"); return; }
        // 14 = NO-PROPOSAL-CHOSEN, 24 = AUTHENTICATION-FAILED...
        if (type == 14) fail("máy chủ từ chối đề xuất (NO-PROPOSAL-CHOSEN) - thử nhóm DH/thuật toán khác");
        else if (type == 24) fail("máy chủ báo xác thực thất bại (PSK?)");
    }
    const QByteArray del = m.payload(DELETE);
    if (!del.isEmpty())
    {
        // DELETE body: [DOI(4)][proto(1)][spiSize(1)][#spi(2)][spis...]. proto 1=ISAKMP(phase1), 3=ESP(phase2).
        // CHỈ đóng phiên nếu DELETE tham chiếu ĐÚNG SA hiện tại. Gateway hay gửi DELETE dọn SA CŨ (SPI khác) -
        // không được tự sập vì những DELETE đó (đó chính là lỗi khiến đường encap=3 bị bỏ oan).
        if (del.size() >= 8)
        {
            const quint8 proto = static_cast<quint8>(del[4]);
            const quint8 spiSize = static_cast<quint8>(del[5]);
            const quint16 nspi = rd16(del, 6);
            bool mine = false;
            if (proto == 3 && spiSize == 4)
            {
                for (int i = 0; i + 4 <= del.size() - 8; i += 4)
                {
                    const quint32 s = rd32(del, 8 + i);
                    if (s == m_spiOut || s == m_spiIn) { mine = true; break; }
                }
                if (mine)
                    fail(QString("máy chủ gửi DELETE ESP/phase2 cho SA HIỆN TẠI (SPI=%1)").arg(rd32(del, 8), 0, 16));
                else
                    emit log(QString("IKE: bỏ qua DELETE ESP cho SA cũ (SPI=%1, không phải SA của ta ra=%2 vào=%3)")
                                 .arg(rd32(del, 8), 0, 16).arg(m_spiOut, 0, 16).arg(m_spiIn, 0, 16));
            }
            else if (proto == 1)
            {
                // ISAKMP/phase1 DELETE: SPI = cặp cookie (16 byte). Chỉ sập nếu khớp cookie của ta.
                const QByteArray spi = del.mid(8, spiSize);
                if (spi == (m_ckyI + m_ckyR) || spiSize == 0)
                    fail("máy chủ gửi DELETE ISAKMP/phase1 (đóng phiên)");
                else
                    emit log("IKE: bỏ qua DELETE ISAKMP cho phiên khác (cookie không khớp)");
            }
            else
                emit log(QString("IKE: bỏ qua DELETE proto=%1 lạ").arg(proto));
        }
    }
}

void IkeSession::sendDpd()
{
    if (m_state != State::Up)
        return;
    static quint32 seq = 0x10000001u;
    QByteArray seqData;
    seqData.append(static_cast<char>((seq >> 24) & 0xff));
    seqData.append(static_cast<char>((seq >> 16) & 0xff));
    seqData.append(static_cast<char>((seq >> 8) & 0xff));
    seqData.append(static_cast<char>(seq & 0xff));
    const QByteArray ruthere = notifyPayload(1 /*ISAKMP*/, m_ckyI + m_ckyR, 36136, seqData);
    const quint32 mid = newMsgId();
    const QByteArray hash = IkeKeys::genericHash(m_cfg.hash, m_sk.a, mid, chain({{NOTIFY, ruthere}}));
    Isakmp::Message r;
    r.exchange = Informational;
    r.messageId = mid;
    r.payloads = {{HASH, hash}, {NOTIFY, ruthere}};
    send(r, true);
    emit log(QString("IKE: gửi DPD R-U-THERE seq=0x%1").arg(seq, 0, 16));
    ++seq;
}

void IkeSession::onDatagram(const QByteArray& raw)
{
    if (m_state == State::Failed || raw.size() < 28)
        return;
    const quint8 ex = exchangeOf(raw);
    const quint8 rawFlags = flagsOf(raw);
    const bool enc = (rawFlags & 0x01) != 0;
    if (ex == QuickMode)
        emit log(QString("IKE: QM2 flags=0x%1 (bit1 COMMIT=%2)").arg(rawFlags, 2, 16, QChar('0')).arg((rawFlags & 0x02) ? "CÓ" : "không"));

    if (ex == Aggressive)
    {
        Isakmp::Message m;
        if (Isakmp::Message::decode(raw, &m)) onAggr2(m);
        return;
    }
    // Các trao đổi sau Phase 1 đều mã hóa.
    Isakmp::Message m;
    bool ok = enc ? decryptMessage(raw, &m) : Isakmp::Message::decode(raw, &m);
    if (!ok)
    {
        emit log("IKE: không giải mã/đọc được bản tin");
        return;
    }
    if (ex == Transaction) onTransaction(m);
    else if (ex == QuickMode) onQuick2(m);
    else if (ex == Informational) onInformational(m);
}

void IkeSession::onRetransmit()
{
    if (m_state == State::Idle || m_state == State::Up || m_state == State::Failed || m_lastSent.isEmpty())
        return;
    emit sendDatagram(m_lastSent, m_lastWasNatT);
}
