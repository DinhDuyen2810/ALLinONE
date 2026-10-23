#include "IsakmpMessage.h"

namespace
{
void put16(QByteArray& b, quint16 v)
{
    b.append(static_cast<char>((v >> 8) & 0xFF));
    b.append(static_cast<char>(v & 0xFF));
}
void put32(QByteArray& b, quint32 v)
{
    b.append(static_cast<char>((v >> 24) & 0xFF));
    b.append(static_cast<char>((v >> 16) & 0xFF));
    b.append(static_cast<char>((v >> 8) & 0xFF));
    b.append(static_cast<char>(v & 0xFF));
}
quint16 get16(const QByteArray& b, int off)
{
    return static_cast<quint16>((static_cast<quint8>(b[off]) << 8) | static_cast<quint8>(b[off + 1]));
}
quint32 get32(const QByteArray& b, int off)
{
    return (static_cast<quint32>(static_cast<quint8>(b[off])) << 24) |
           (static_cast<quint32>(static_cast<quint8>(b[off + 1])) << 16) |
           (static_cast<quint32>(static_cast<quint8>(b[off + 2])) << 8) |
           static_cast<quint32>(static_cast<quint8>(b[off + 3]));
}
} // namespace

namespace Isakmp
{
QByteArray Message::encode() const
{
    QByteArray body;
    for (int i = 0; i < payloads.size(); ++i)
    {
        const quint8 next = (i + 1 < payloads.size()) ? payloads[i + 1].first : static_cast<quint8>(None);
        const QByteArray& d = payloads[i].second;
        body.append(static_cast<char>(next));
        body.append(static_cast<char>(0)); // reserved / critical
        put16(body, static_cast<quint16>(4 + d.size()));
        body.append(d);
    }

    QByteArray out;
    out.append(initiatorCookie.leftJustified(8, '\0', true));
    out.append(responderCookie.leftJustified(8, '\0', true));
    out.append(static_cast<char>(payloads.isEmpty() ? static_cast<quint8>(None) : payloads.first().first));
    out.append(static_cast<char>(0x10)); // version 1.0
    out.append(static_cast<char>(exchange));
    out.append(static_cast<char>(flags));
    put32(out, messageId);
    put32(out, static_cast<quint32>(28 + body.size()));
    out.append(body);
    return out;
}

bool Message::decode(const QByteArray& raw, Message* out)
{
    if (!out || raw.size() < 28)
        return false;
    out->payloads.clear();
    out->initiatorCookie = raw.mid(0, 8);
    out->responderCookie = raw.mid(8, 8);
    quint8 next = static_cast<quint8>(raw[16]);
    out->firstPayload = next;
    out->exchange = static_cast<quint8>(raw[18]);
    out->flags = static_cast<quint8>(raw[19]);
    out->messageId = get32(raw, 20);
    const quint32 declared = get32(raw, 24);
    if (declared < 28 || declared > static_cast<quint32>(raw.size()))
        return false;

    int off = 28;
    const int end = static_cast<int>(declared);
    while (next != None && off + 4 <= end)
    {
        const quint8 thisType = next;
        const quint8 nextType = static_cast<quint8>(raw[off]);
        const quint16 len = get16(raw, off + 2);
        if (len < 4 || off + len > end)
            return false;
        out->payloads.append(qMakePair(thisType, raw.mid(off + 4, len - 4)));
        off += len;
        next = nextType;
    }
    return true;
}

QByteArray Message::payload(quint8 type) const
{
    for (const auto& p : payloads)
        if (p.first == type)
            return p.second;
    return {};
}

// ---- Thuộc tính ----
QByteArray attribute(quint16 type, quint32 value)
{
    QByteArray b;
    put16(b, static_cast<quint16>((type & 0x7FFF) | 0x8000)); // AF=1 (TV)
    put16(b, static_cast<quint16>(value & 0xFFFF));
    return b;
}
QByteArray attributeVar(quint16 type, const QByteArray& v)
{
    QByteArray b;
    put16(b, static_cast<quint16>(type & 0x7FFF)); // AF=0 (TLV)
    put16(b, static_cast<quint16>(v.size()));
    b.append(v);
    return b;
}

// ---- SA / proposal / transform (các hàm này TRẢ VỀ payload KÈM generic header lồng trong) ----
QByteArray transform(quint8 num, quint8 transformId, const QByteArray& attrs, bool last)
{
    QByteArray body;
    body.append(static_cast<char>(num));
    body.append(static_cast<char>(transformId));
    body.append(static_cast<char>(0));
    body.append(static_cast<char>(0));
    body.append(attrs);
    QByteArray out;
    out.append(static_cast<char>(last ? None : Transform));
    out.append(static_cast<char>(0));
    put16(out, static_cast<quint16>(4 + body.size()));
    out.append(body);
    return out;
}
QByteArray proposal(quint8 num, quint8 protocolId, const QByteArray& spi, const QByteArray& transforms, int transformCount,
                    bool last)
{
    QByteArray body;
    body.append(static_cast<char>(num));
    body.append(static_cast<char>(protocolId));
    body.append(static_cast<char>(spi.size()));
    body.append(static_cast<char>(transformCount));
    body.append(spi);
    body.append(transforms);
    QByteArray out;
    out.append(static_cast<char>(last ? None : Proposal));
    out.append(static_cast<char>(0));
    put16(out, static_cast<quint16>(4 + body.size()));
    out.append(body);
    return out;
}
QByteArray saPayload(const QByteArray& proposals)
{
    QByteArray b;
    put32(b, 1); // DOI = IPSEC
    put32(b, 1); // Situation = SIT_IDENTITY_ONLY
    b.append(proposals);
    return b;
}

QByteArray idPayload(quint8 idType, quint8 protocolId, quint16 port, const QByteArray& data)
{
    QByteArray b;
    b.append(static_cast<char>(idType));
    b.append(static_cast<char>(protocolId));
    put16(b, port);
    b.append(data);
    return b;
}
QByteArray notifyPayload(quint8 protocolId, const QByteArray& spi, quint16 type, const QByteArray& data)
{
    QByteArray b;
    put32(b, 1); // DOI = IPSEC
    b.append(static_cast<char>(protocolId));
    b.append(static_cast<char>(spi.size()));
    put16(b, type);
    b.append(spi);
    b.append(data);
    return b;
}
QByteArray deletePayload(quint8 protocolId, const QByteArray& spi)
{
    QByteArray b;
    put32(b, 1); // DOI = IPSEC
    b.append(static_cast<char>(protocolId));
    b.append(static_cast<char>(spi.size()));
    put16(b, 1); // #SPI
    b.append(spi);
    return b;
}
QByteArray configPayload(quint8 cfgType, quint16 identifier, const QByteArray& attrs)
{
    QByteArray b;
    b.append(static_cast<char>(cfgType));
    b.append(static_cast<char>(0)); // reserved
    put16(b, identifier);
    b.append(attrs);
    return b;
}

bool forEachAttribute(const QByteArray& attrs, const std::function<void(quint16, bool, quint32, const QByteArray&)>& fn)
{
    int off = 0;
    while (off + 4 <= attrs.size())
    {
        const quint16 raw = get16(attrs, off);
        const bool basic = (raw & 0x8000) != 0;
        const quint16 type = raw & 0x7FFF;
        const quint16 lenOrVal = get16(attrs, off + 2);
        off += 4;
        if (basic)
        {
            fn(type, false, lenOrVal, QByteArray());
        }
        else
        {
            if (off + lenOrVal > attrs.size())
                return false;
            fn(type, true, 0, attrs.mid(off, lenOrVal));
            off += lenOrVal;
        }
    }
    return off == attrs.size();
}

QByteArray configAttrs(const QByteArray& configPayloadData, quint8* cfgType, quint16* identifier)
{
    if (configPayloadData.size() < 4)
        return {};
    if (cfgType)
        *cfgType = static_cast<quint8>(configPayloadData[0]);
    if (identifier)
        *identifier = get16(configPayloadData, 2);
    return configPayloadData.mid(4);
}
} // namespace Isakmp
