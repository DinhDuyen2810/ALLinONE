#include "ProtocolMessage.h"

#include <QDataStream>
#include <QIODevice>

QByteArray ProtocolMessage::toBytes() const
{
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setVersion(QDataStream::Qt_6_0);
    s << static_cast<quint8>(type) << textA << textB << longTermKey << qint32(intA) << qint32(intB) << flagA << flagB;
    return out;
}

bool ProtocolMessage::fromBytes(const QByteArray& data, ProtocolMessage* out)
{
    if (!out)
        return false;

    QDataStream s(data);
    s.setVersion(QDataStream::Qt_6_0);

    quint8 rawType = 0;
    qint32 ia = 0, ib = 0;
    s >> rawType >> out->textA >> out->textB >> out->longTermKey >> ia >> ib >> out->flagA >> out->flagB;

    if (s.status() != QDataStream::Ok)
        return false;
    if (rawType < static_cast<quint8>(MessageType::PairRequest) || rawType > static_cast<quint8>(MessageType::Goodbye))
        return false;

    out->type = static_cast<MessageType>(rawType);
    out->intA = ia;
    out->intB = ib;
    return true;
}
