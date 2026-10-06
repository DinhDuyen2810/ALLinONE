#include "CryptoSession.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <QCryptographicHash>
#include <QPasswordDigestor>
#include <QRandomGenerator>

#pragma comment(lib, "bcrypt.lib")

namespace
{
constexpr int kKeyBytes = 32;   // AES-256
constexpr int kNonceBytes = 12; // kích thước nonce khuyến nghị cho GCM
constexpr int kTagBytes = 16;

// Salt cố định của riêng ứng dụng cho PBKDF2 khi suy khóa từ mã ghép đôi. Mã ghép đôi là ngẫu nhiên,
// chỉ dùng một lần trong một phiên ngắn, nên salt cố định là đủ (không phải kịch bản lưu trữ mật khẩu).
const QByteArray kPairingSalt = QByteArrayLiteral("OneForAll.ConnectTogether.Pairing.v1");
}

struct CryptoSession::Impl
{
    BCRYPT_ALG_HANDLE alg{nullptr};
    BCRYPT_KEY_HANDLE key{nullptr};
    QByteArray keyObject;
};

CryptoSession::CryptoSession(const QByteArray& key256bit)
    : m_impl(new Impl)
{
    if (key256bit.size() != kKeyBytes)
        return;

    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&m_impl->alg, BCRYPT_AES_ALGORITHM, nullptr, 0)))
        return;

    if (!BCRYPT_SUCCESS(BCryptSetProperty(m_impl->alg, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_GCM,
                                          sizeof(BCRYPT_CHAIN_MODE_GCM), 0)))
    {
        BCryptCloseAlgorithmProvider(m_impl->alg, 0);
        m_impl->alg = nullptr;
        return;
    }

    DWORD objLen = 0, copied = 0;
    BCryptGetProperty(m_impl->alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &copied, 0);
    m_impl->keyObject.resize(static_cast<int>(objLen));

    auto keyBytes = key256bit; // BCrypt cần buffer có thể ghi, không nhận const trực tiếp
    const NTSTATUS st = BCryptGenerateSymmetricKey(
        m_impl->alg, &m_impl->key, reinterpret_cast<PUCHAR>(m_impl->keyObject.data()), objLen,
        reinterpret_cast<PUCHAR>(keyBytes.data()), static_cast<ULONG>(keyBytes.size()), 0);

    m_valid = BCRYPT_SUCCESS(st);
}

CryptoSession::~CryptoSession()
{
    if (m_impl)
    {
        if (m_impl->key)
            BCryptDestroyKey(m_impl->key);
        if (m_impl->alg)
            BCryptCloseAlgorithmProvider(m_impl->alg, 0);
        delete m_impl;
    }
}

bool CryptoSession::encrypt(const QByteArray& plaintext, QByteArray* outPacket, QString* error) const
{
    if (!m_valid)
    {
        if (error) *error = "Phiên mã hóa chưa sẵn sàng.";
        return false;
    }

    QByteArray nonce(kNonceBytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(nonce.data()),
                                          static_cast<qsizetype>(kNonceBytes / sizeof(quint32)));

    QByteArray tag(kTagBytes, 0);
    QByteArray cipher(plaintext.size(), 0);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = reinterpret_cast<PUCHAR>(nonce.data());
    info.cbNonce = kNonceBytes;
    info.pbTag = reinterpret_cast<PUCHAR>(tag.data());
    info.cbTag = kTagBytes;

    ULONG resultLen = 0;
    QByteArray input = plaintext; // BCryptEncrypt veut un buffer non-const
    const NTSTATUS st = BCryptEncrypt(m_impl->key, reinterpret_cast<PUCHAR>(input.data()),
                                      static_cast<ULONG>(input.size()), &info, nullptr, 0,
                                      reinterpret_cast<PUCHAR>(cipher.data()), static_cast<ULONG>(cipher.size()),
                                      &resultLen, 0);
    if (!BCRYPT_SUCCESS(st))
    {
        if (error) *error = QString("Mã hóa thất bại (0x%1).").arg(static_cast<unsigned long>(st), 0, 16);
        return false;
    }

    outPacket->clear();
    *outPacket = nonce + cipher.left(static_cast<int>(resultLen)) + tag;
    return true;
}

bool CryptoSession::decrypt(const QByteArray& packet, QByteArray* outPlaintext, QString* error) const
{
    if (!m_valid)
    {
        if (error) *error = "Phiên mã hóa chưa sẵn sàng.";
        return false;
    }
    if (packet.size() < kNonceBytes + kTagBytes)
    {
        if (error) *error = "Gói tin quá ngắn.";
        return false;
    }

    QByteArray nonce = packet.left(kNonceBytes);
    QByteArray tag = packet.right(kTagBytes);
    QByteArray cipher = packet.mid(kNonceBytes, packet.size() - kNonceBytes - kTagBytes);
    QByteArray plain(cipher.size(), 0);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = reinterpret_cast<PUCHAR>(nonce.data());
    info.cbNonce = kNonceBytes;
    info.pbTag = reinterpret_cast<PUCHAR>(tag.data());
    info.cbTag = kTagBytes;

    ULONG resultLen = 0;
    const NTSTATUS st = BCryptDecrypt(m_impl->key, reinterpret_cast<PUCHAR>(cipher.data()),
                                      static_cast<ULONG>(cipher.size()), &info, nullptr, 0,
                                      reinterpret_cast<PUCHAR>(plain.data()), static_cast<ULONG>(plain.size()),
                                      &resultLen, 0);
    if (!BCRYPT_SUCCESS(st))
    {
        // STATUS_AUTH_TAG_MISMATCH (khóa sai / dữ liệu bị sửa) rơi vào đây - cố ý không phân biệt lý do
        // cụ thể với bên ngoài để không lộ thông tin hữu ích cho việc dò khóa.
        if (error) *error = "Giải mã thất bại - sai khóa hoặc dữ liệu không toàn vẹn.";
        return false;
    }

    *outPlaintext = plain.left(static_cast<int>(resultLen));
    return true;
}

QByteArray CryptoSession::deriveKeyFromPairingCode(const QString& normalizedCode)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, normalizedCode.toUtf8(), kPairingSalt,
                                              100000, kKeyBytes);
}

QByteArray CryptoSession::generateRandomKey()
{
    QByteArray key(kKeyBytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(key.data()),
                                          static_cast<qsizetype>(kKeyBytes / sizeof(quint32)));
    return key;
}
