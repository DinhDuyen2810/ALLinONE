#include "ProtocolMessage.h"

#include <QDataStream>
#include <QIODevice>

QByteArray ProtocolMessage::toBytes() const
{
    QByteArray out;
    QDataStream s(&out, QIODevice::WriteOnly);
    s.setVersion(QDataStream::Qt_6_0);
    s << static_cast<quint8>(type) << textA << textB << longTermKey << nonce << qint32(intA) << qint32(intB) << flagA
      << flagB;
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
    s >> rawType >> out->textA >> out->textB >> out->longTermKey >> out->nonce >> ia >> ib >> out->flagA >> out->flagB;

    if (s.status() != QDataStream::Ok)
        return false;
    if (!s.atEnd())
        return false; // thừa dữ liệu phía sau - không phải thông điệp do bản này tạo ra
    if (rawType < static_cast<quint8>(MessageType::PairRequest) || rawType > static_cast<quint8>(MessageType::SessionConfirm))
        return false;

    out->type = static_cast<MessageType>(rawType);
    out->intA = ia;
    out->intB = ib;
    return true;
}

namespace
{
const QByteArray kPreambleMagic = QByteArrayLiteral("OFCT");
}

QByteArray ConnectProtocol::buildPreamble(LinkPurpose purpose, const QString& senderId)
{
    const QByteArray id = senderId.toUtf8();
    QByteArray out = kPreambleMagic;
    out.append(static_cast<char>(kVersion));
    out.append(static_cast<char>(purpose));
    out.append(static_cast<char>(qMin<qsizetype>(id.size(), 255)));
    out.append(id.left(255));
    return out;
}

bool ConnectProtocol::parsePreamble(const QByteArray& data, int* version, LinkPurpose* purpose, QString* senderId)
{
    const qsizetype headerSize = kPreambleMagic.size() + 3;
    if (data.size() < headerSize || !data.startsWith(kPreambleMagic))
        return false;

    const int ver = static_cast<quint8>(data.at(kPreambleMagic.size()));
    if (version)
        *version = ver;
    if (ver != kVersion)
        return true; // đọc được phần đầu nhưng khác phiên bản - phần sau có thể đổi nghĩa, không đọc tiếp

    const quint8 rawPurpose = static_cast<quint8>(data.at(kPreambleMagic.size() + 1));
    const int idLen = static_cast<quint8>(data.at(kPreambleMagic.size() + 2));
    if (rawPurpose != static_cast<quint8>(LinkPurpose::Pairing) && rawPurpose != static_cast<quint8>(LinkPurpose::Session))
        return false;
    if (data.size() != headerSize + idLen)
        return false;

    const QString id = QString::fromUtf8(data.mid(headerSize, idLen));
    if (!isValidPeerId(id))
        return false;

    if (purpose)
        *purpose = static_cast<LinkPurpose>(rawPurpose);
    if (senderId)
        *senderId = id;
    return true;
}

bool ConnectProtocol::isValidPeerId(const QString& id)
{
    if (id.isEmpty() || id.size() > kMaxIdChars)
        return false;
    for (const QChar c : id)
    {
        const ushort u = c.unicode();
        const bool ok = (u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z') || (u >= '0' && u <= '9') || u == '-' ||
                        u == '_' || u == '.';
        if (!ok)
            return false;
    }
    return true;
}

QString ConnectProtocol::sanitizeMachineName(const QString& name)
{
    QString out;
    out.reserve(qMin<qsizetype>(name.size(), kMaxNameChars));
    for (const QChar c : name)
    {
        if (out.size() >= kMaxNameChars)
            break;
        // Bỏ ký tự điều khiển/xuống dòng (tên này được đưa thẳng lên bảng, nhật ký, hộp thoại) và dấu
        // '<' '>' để QLabel/QMessageBox không tự nhận là rich text.
        if (c.category() == QChar::Other_Control || c.category() == QChar::Other_Format || c == '<' || c == '>')
            continue;
        out += c;
    }
    out = out.trimmed();
    return out.isEmpty() ? QStringLiteral("(không tên)") : out;
}
