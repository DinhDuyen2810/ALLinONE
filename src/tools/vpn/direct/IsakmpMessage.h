#pragma once

#include <QByteArray>
#include <QList>
#include <QPair>

#include <functional>

/// Bản tin ISAKMP/IKEv1 (RFC 2408): tiêu đề + chuỗi payload. THUẦN (không mạng, không crypto) - test được
/// bằng encode/decode round-trip. Chỉ gồm các payload cần cho FortiGate Aggressive mode + XAuth/Mode-config.
namespace Isakmp
{
// Loại payload (RFC 2408 §3.1 + RFC 3947).
enum Payload : quint8
{
    None = 0, SA = 1, Proposal = 2, Transform = 3, KE = 4, ID = 5, CERT = 6, CERTREQ = 7, HASH = 8, SIG = 9,
    NONCE = 10, NOTIFY = 11, DELETE = 12, VID = 13, CP = 14 /*Config/Mode-cfg*/, NATD = 20, NATOA = 21,
};

// Kiểu trao đổi.
enum Exchange : quint8
{
    IdProt = 2, Aggressive = 4, Informational = 5, Transaction = 6, QuickMode = 32,
};

// Giá trị thuộc tính transform Phase 1 (IKE).
enum IkeAttr : quint16
{
    EncAlg = 1, HashAlg = 2, AuthMethod = 3, GroupDesc = 4, LifeType = 11, LifeDuration = 12, KeyLength = 14,
};
enum EncId { ENC_3DES = 5, ENC_AES_CBC = 7 };
enum HashId { HASH_MD5 = 1, HASH_SHA = 2, HASH_SHA256 = 4 };
enum AuthId { AUTH_PSK = 1, AUTH_XAUTH_INIT_PSK = 65001 };
enum CfgType { CFG_REQUEST = 1, CFG_REPLY = 2, CFG_SET = 3, CFG_ACK = 4 };
// Thuộc tính Mode-config / XAuth (RFC 2131-ish + draft-xauth).
enum CfgAttr : quint16
{
    INTERNAL_IP4_ADDRESS = 1, INTERNAL_IP4_NETMASK = 2, INTERNAL_IP4_DNS = 3, INTERNAL_IP4_NBNS = 4,
    INTERNAL_IP4_SUBNET = 13,
    // Thuộc tính XAuth (draft-ietf-ipsec-isakmp-xauth-06): TYPE 16520..ANSWER 16529.
    XAUTH_TYPE = 16520, XAUTH_USER_NAME = 16521, XAUTH_USER_PASSWORD = 16522, XAUTH_PASSCODE = 16523,
    XAUTH_MESSAGE = 16524, XAUTH_CHALLENGE = 16525, XAUTH_DOMAIN = 16526, XAUTH_STATUS = 16527,
};
// ID type.
enum IdType { ID_IPV4_ADDR = 1, ID_FQDN = 2, ID_USER_FQDN = 3, ID_IPV4_SUBNET = 4, ID_KEY_ID = 11 };

struct Message
{
    QByteArray initiatorCookie; // 8 byte
    QByteArray responderCookie; // 8 byte
    quint8 exchange{0};
    quint8 flags{0}; // bit0 = Encryption
    quint32 messageId{0};
    quint8 firstPayload{None};
    QList<QPair<quint8, QByteArray>> payloads; // (type, nội dung payload KHÔNG gồm generic header)

    QByteArray encode() const;
    /// false nếu bản tin hỏng (độ dài/kết cấu sai).
    static bool decode(const QByteArray& raw, Message* out);
    QByteArray payload(quint8 type) const; // payload đầu tiên theo loại (rỗng nếu không có)
};

// ---- Dựng các payload (trả về nội dung, chưa gồm generic header) ----

/// Một thuộc tính TLV (dạng cơ bản 2 byte nếu value<=0xFFFF và basic=true; ngược lại dạng biến thiên).
QByteArray attribute(quint16 type, quint32 value);         // basic (TV)
QByteArray attributeVar(quint16 type, const QByteArray& v); // variable (TLV)

/// Transform payload (transformNum, transformId=KEY_IKE(1) cho P1, attrs đã nối).
QByteArray transform(quint8 num, quint8 transformId, const QByteArray& attrs, bool last);
/// Proposal payload (num, protocolId, spi, các transform đã nối, số transform).
QByteArray proposal(quint8 num, quint8 protocolId, const QByteArray& spi, const QByteArray& transforms, int transformCount,
                    bool last);
/// SA payload (DOI=IPSEC(1), Situation=IDENTITY_ONLY(1), proposals).
QByteArray saPayload(const QByteArray& proposals);

QByteArray idPayload(quint8 idType, quint8 protocolId, quint16 port, const QByteArray& data);
QByteArray notifyPayload(quint8 protocolId, const QByteArray& spi, quint16 type, const QByteArray& data);
QByteArray deletePayload(quint8 protocolId, const QByteArray& spi);
/// Config/Mode-cfg payload: type (CFG_REQUEST/REPLY/SET/ACK), identifier, các thuộc tính đã nối.
QByteArray configPayload(quint8 cfgType, quint16 identifier, const QByteArray& attrs);
/// Duyệt thuộc tính trong một config/transform payload -> gọi fn(type, isVar, value/basic).
bool forEachAttribute(const QByteArray& attrs, const std::function<void(quint16, bool, quint32, const QByteArray&)>& fn);
QByteArray configAttrs(const QByteArray& configPayloadData, quint8* cfgType, quint16* identifier);
} // namespace Isakmp
