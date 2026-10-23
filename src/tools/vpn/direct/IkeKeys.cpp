#include "IkeKeys.h"

namespace
{
using IkeCrypto::Hash;
using IkeCrypto::prf;

QByteArray be32(quint32 v)
{
    QByteArray b(4, '\0');
    b[0] = static_cast<char>((v >> 24) & 0xFF);
    b[1] = static_cast<char>((v >> 16) & 0xFF);
    b[2] = static_cast<char>((v >> 8) & 0xFF);
    b[3] = static_cast<char>(v & 0xFF);
    return b;
}
} // namespace

namespace IkeKeys
{
QByteArray skeyidPsk(Hash h, const QByteArray& psk, const QByteArray& niB, const QByteArray& nrB)
{
    return prf(h, psk, niB + nrB);
}

Skeyids deriveSkeyids(Hash h, const QByteArray& skeyid, const QByteArray& gxy, const QByteArray& ckyI,
                      const QByteArray& ckyR)
{
    Skeyids s;
    s.skeyid = skeyid;
    const QByteArray cc = ckyI + ckyR;
    s.d = prf(h, skeyid, gxy + cc + QByteArray(1, '\x00'));
    s.a = prf(h, skeyid, s.d + gxy + cc + QByteArray(1, '\x01'));
    s.e = prf(h, skeyid, s.a + gxy + cc + QByteArray(1, '\x02'));
    return s;
}

QByteArray hashI(Hash h, const QByteArray& skeyid, const QByteArray& gxi, const QByteArray& gxr, const QByteArray& ckyI,
                 const QByteArray& ckyR, const QByteArray& saiB, const QByteArray& idiiB)
{
    return prf(h, skeyid, gxi + gxr + ckyI + ckyR + saiB + idiiB);
}
QByteArray hashR(Hash h, const QByteArray& skeyid, const QByteArray& gxr, const QByteArray& gxi, const QByteArray& ckyR,
                 const QByteArray& ckyI, const QByteArray& saiB, const QByteArray& idirB)
{
    return prf(h, skeyid, gxr + gxi + ckyR + ckyI + saiB + idirB);
}

QByteArray expandKey(Hash h, const QByteArray& skeyidE, int need)
{
    if (skeyidE.size() >= need)
        return skeyidE.left(need);
    QByteArray out;
    QByteArray k = prf(h, skeyidE, QByteArray(1, '\x00'));
    out += k;
    while (out.size() < need)
    {
        k = prf(h, skeyidE, k);
        out += k;
    }
    return out.left(need);
}

QByteArray phase1Iv(Hash h, const QByteArray& gxi, const QByteArray& gxr, int blockSize)
{
    return IkeCrypto::hash(h, gxi + gxr).left(blockSize);
}
QByteArray phase2Iv(Hash h, const QByteArray& phase1IvVal, quint32 msgid, int blockSize)
{
    return IkeCrypto::hash(h, phase1IvVal + be32(msgid)).left(blockSize);
}

QByteArray keymat(Hash h, const QByteArray& skeyidD, quint8 proto, quint32 spiNet, const QByteArray& niB,
                  const QByteArray& nrB, int length)
{
    const QByteArray fixed = QByteArray(1, static_cast<char>(proto)) + be32(spiNet) + niB + nrB;
    QByteArray out;
    QByteArray k = prf(h, skeyidD, fixed);
    out += k;
    while (out.size() < length)
    {
        k = prf(h, skeyidD, k + fixed);
        out += k;
    }
    return out.left(length);
}

QByteArray genericHash(Hash h, const QByteArray& skeyidA, quint32 msgid, const QByteArray& afterHash)
{
    return prf(h, skeyidA, be32(msgid) + afterHash);
}
QByteArray qmHash1(Hash h, const QByteArray& skeyidA, quint32 msgid, const QByteArray& afterHash)
{
    return prf(h, skeyidA, be32(msgid) + afterHash);
}
QByteArray qmHash2(Hash h, const QByteArray& skeyidA, quint32 msgid, const QByteArray& niB, const QByteArray& afterHash)
{
    return prf(h, skeyidA, be32(msgid) + niB + afterHash);
}
QByteArray qmHash3(Hash h, const QByteArray& skeyidA, quint32 msgid, const QByteArray& niB, const QByteArray& nrB)
{
    return prf(h, skeyidA, QByteArray(1, '\x00') + be32(msgid) + niB + nrB);
}
} // namespace IkeKeys
