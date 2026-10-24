#pragma once

#include "IkeCrypto.h"

#include <QByteArray>

/// Dẫn xuất khóa IKEv1 (RFC 2409 §5) cho Phase 1 (Aggressive + PSK) và Phase 2 (Quick Mode): SKEYID và các
/// khóa con d/a/e, HASH_I/HASH_R, mở rộng khóa mã hóa, IV cho bản tin mã hóa, băm Quick Mode, và KEYMAT (khóa
/// ESP). THUẦN (chỉ dựa IkeCrypto) nên test được bằng vector tự nhất quán giữa hai vai initiator/responder.
namespace IkeKeys
{
using IkeCrypto::Hash;

struct Skeyids
{
    QByteArray skeyid; ///< khóa gốc dùng cho HASH_I/HASH_R
    QByteArray d;      ///< sinh KEYMAT (Phase 2)
    QByteArray a;      ///< bảo vệ toàn vẹn các bản tin (HASH mode-cfg/quick mode)
    QByteArray e;      ///< sinh khóa mã hóa phiên thương lượng
};

/// SKEYID khi xác thực bằng PSK = prf(psk, Ni_b | Nr_b).
QByteArray skeyidPsk(Hash h, const QByteArray& psk, const QByteArray& niB, const QByteArray& nrB);

/// d/a/e từ SKEYID, bí mật DH g^xy và hai cookie (mỗi cái 8 byte).
Skeyids deriveSkeyids(Hash h, const QByteArray& skeyid, const QByteArray& gxy, const QByteArray& ckyI,
                      const QByteArray& ckyR);

/// HASH_I = prf(SKEYID, g^xi | g^xr | CKY-I | CKY-R | SAi_b | IDii_b).
QByteArray hashI(Hash h, const QByteArray& skeyid, const QByteArray& gxi, const QByteArray& gxr, const QByteArray& ckyI,
                 const QByteArray& ckyR, const QByteArray& saiB, const QByteArray& idiiB);
/// HASH_R = prf(SKEYID, g^xr | g^xi | CKY-R | CKY-I | SAi_b | IDir_b).
QByteArray hashR(Hash h, const QByteArray& skeyid, const QByteArray& gxr, const QByteArray& gxi, const QByteArray& ckyR,
                 const QByteArray& ckyI, const QByteArray& saiB, const QByteArray& idirB);

/// Mở rộng SKEYID_e thành khóa mã hóa đủ 'need' byte (RFC 2409 Phụ lục B): K1=prf(e,0), K2=prf(e,K1), ...
QByteArray expandKey(Hash h, const QByteArray& skeyidE, int need);

/// IV khởi tạo cho bản tin mã hóa Phase 1 = hash(g^xi | g^xr) cắt còn blockSize.
QByteArray phase1Iv(Hash h, const QByteArray& gxi, const QByteArray& gxr, int blockSize);
/// IV khởi tạo cho một trao đổi Phase 2 (message-ID khác 0) = hash(phase1Iv | M-ID) cắt còn blockSize.
QByteArray phase2Iv(Hash h, const QByteArray& phase1IvVal, quint32 msgid, int blockSize);

/// KEYMAT cho một SA ESP: nối K1=prf(d, [g^xy]|proto|SPI|Ni|Nr), K2=prf(d, K1|[g^xy]|proto|SPI|Ni|Nr), ... đủ
/// 'length' byte. SPI theo thứ tự byte MẠNG (như trên dây). proto = 3 (ESP). pfsGxy rỗng = không PFS; nếu có
/// (Quick Mode trao KE) thì g^xy của Quick Mode được ghép vào ĐẦU như RFC 2409 §5.5.
QByteArray keymat(Hash h, const QByteArray& skeyidD, quint8 proto, quint32 spiNet, const QByteArray& niB,
                  const QByteArray& nrB, int length, const QByteArray& pfsGxy = QByteArray());

/// HASH chung cho bản tin Mode-config/Transaction/Thông tin = prf(SKEYID_a, M-ID | <payload sau HASH>).
QByteArray genericHash(Hash h, const QByteArray& skeyidA, quint32 msgid, const QByteArray& afterHash);
/// Quick Mode HASH(1) = prf(SKEYID_a, M-ID | <payload sau HASH>).
QByteArray qmHash1(Hash h, const QByteArray& skeyidA, quint32 msgid, const QByteArray& afterHash);
/// Quick Mode HASH(2) = prf(SKEYID_a, M-ID | Ni_b | <payload sau HASH>).
QByteArray qmHash2(Hash h, const QByteArray& skeyidA, quint32 msgid, const QByteArray& niB, const QByteArray& afterHash);
/// Quick Mode HASH(3) = prf(SKEYID_a, 0 | M-ID | Ni_b | Nr_b).
QByteArray qmHash3(Hash h, const QByteArray& skeyidA, quint32 msgid, const QByteArray& niB, const QByteArray& nrB);
} // namespace IkeKeys
