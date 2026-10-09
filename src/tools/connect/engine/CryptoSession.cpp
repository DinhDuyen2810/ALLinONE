#include "CryptoSession.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <dpapi.h>

#include <QCryptographicHash>
#include <QPasswordDigestor>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace
{
// Salt cố định của riêng ứng dụng cho PBKDF2 khi suy khóa từ mã ghép đôi. Mã ghép đôi là ngẫu nhiên,
// chỉ dùng một lần trong một phiên ngắn, nên salt cố định là đủ (không phải kịch bản lưu trữ mật khẩu).
const QByteArray kPairingSalt = QByteArrayLiteral("OneForAll.ConnectTogether.Pairing.v1");

// Nhãn tách miền khi suy khóa phiên - mỗi chiều một nhãn nên hai chiều không bao giờ trùng khóa.
const QByteArray kLabelInitiatorToResponder = QByteArrayLiteral("OneForAll.ConnectTogether.v2.session.i2r");
const QByteArray kLabelResponderToInitiator = QByteArrayLiteral("OneForAll.ConnectTogether.v2.session.r2i");

// "Entropy" phụ của DPAPI: không phải bí mật (nằm trong exe), chỉ để một blob DPAPI của ứng dụng khác
// (cùng tài khoản Windows) không vô tình mở được như khóa ghép đôi và ngược lại.
const QByteArray kDpapiEntropy = QByteArrayLiteral("OneForAll.ConnectTogether.PeerKey.v1");
} // namespace

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
    if (!BCRYPT_SUCCESS(BCryptGetProperty(m_impl->alg, BCRYPT_OBJECT_LENGTH, (PUCHAR)&objLen, sizeof(objLen), &copied, 0)) ||
        objLen == 0)
        return;
    m_impl->keyObject.resize(static_cast<int>(objLen));

    auto keyBytes = key256bit; // BCrypt cần buffer có thể ghi, không nhận const trực tiếp
    const NTSTATUS st = BCryptGenerateSymmetricKey(
        m_impl->alg, &m_impl->key, reinterpret_cast<PUCHAR>(m_impl->keyObject.data()), objLen,
        reinterpret_cast<PUCHAR>(keyBytes.data()), static_cast<ULONG>(keyBytes.size()), 0);
    SecureZeroMemory(keyBytes.data(), static_cast<SIZE_T>(keyBytes.size())); // bản sao tạm của khóa - không để sót trong heap

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
        if (!m_impl->keyObject.isEmpty())
            SecureZeroMemory(m_impl->keyObject.data(), static_cast<SIZE_T>(m_impl->keyObject.size()));
        delete m_impl;
    }
}

bool CryptoSession::encryptWithNonce(const QByteArray& nonceIn, const QByteArray& aadIn, const QByteArray& plaintext,
                                     QByteArray* outCipherAndTag, QString* error) const
{
    if (!m_valid || !outCipherAndTag)
    {
        if (error) *error = "Phiên mã hóa chưa sẵn sàng.";
        return false;
    }
    if (nonceIn.size() != kNonceBytes)
    {
        if (error) *error = "Nonce không đúng kích thước.";
        return false;
    }

    // BCrypt nhận buffer không-const cho mọi tham số - sao chép ra bản ghi được.
    QByteArray nonce = nonceIn;
    QByteArray aad = aadIn;
    QByteArray input = plaintext;
    QByteArray tag(kTagBytes, 0);
    QByteArray cipher(plaintext.size(), 0);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = reinterpret_cast<PUCHAR>(nonce.data());
    info.cbNonce = kNonceBytes;
    info.pbAuthData = aad.isEmpty() ? nullptr : reinterpret_cast<PUCHAR>(aad.data());
    info.cbAuthData = static_cast<ULONG>(aad.size());
    info.pbTag = reinterpret_cast<PUCHAR>(tag.data());
    info.cbTag = kTagBytes;

    ULONG resultLen = 0;
    const NTSTATUS st = BCryptEncrypt(m_impl->key, reinterpret_cast<PUCHAR>(input.data()),
                                      static_cast<ULONG>(input.size()), &info, nullptr, 0,
                                      reinterpret_cast<PUCHAR>(cipher.data()), static_cast<ULONG>(cipher.size()),
                                      &resultLen, 0);
    if (!BCRYPT_SUCCESS(st))
    {
        if (error) *error = QString("Mã hóa thất bại (0x%1).").arg(static_cast<unsigned long>(st), 0, 16);
        return false;
    }

    *outCipherAndTag = cipher.left(static_cast<int>(resultLen)) + tag;
    return true;
}

bool CryptoSession::decryptWithNonce(const QByteArray& nonceIn, const QByteArray& aadIn, const QByteArray& cipherAndTag,
                                     QByteArray* outPlaintext, QString* error) const
{
    if (!m_valid || !outPlaintext)
    {
        if (error) *error = "Phiên mã hóa chưa sẵn sàng.";
        return false;
    }
    if (nonceIn.size() != kNonceBytes || cipherAndTag.size() < kTagBytes)
    {
        if (error) *error = "Gói tin quá ngắn.";
        return false;
    }

    QByteArray nonce = nonceIn;
    QByteArray aad = aadIn;
    QByteArray tag = cipherAndTag.right(kTagBytes);
    QByteArray cipher = cipherAndTag.left(cipherAndTag.size() - kTagBytes);
    QByteArray plain(cipher.size(), 0);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO info;
    BCRYPT_INIT_AUTH_MODE_INFO(info);
    info.pbNonce = reinterpret_cast<PUCHAR>(nonce.data());
    info.cbNonce = kNonceBytes;
    info.pbAuthData = aad.isEmpty() ? nullptr : reinterpret_cast<PUCHAR>(aad.data());
    info.cbAuthData = static_cast<ULONG>(aad.size());
    info.pbTag = reinterpret_cast<PUCHAR>(tag.data());
    info.cbTag = kTagBytes;

    ULONG resultLen = 0;
    const NTSTATUS st = BCryptDecrypt(m_impl->key, reinterpret_cast<PUCHAR>(cipher.data()),
                                      static_cast<ULONG>(cipher.size()), &info, nullptr, 0,
                                      reinterpret_cast<PUCHAR>(plain.data()), static_cast<ULONG>(plain.size()),
                                      &resultLen, 0);
    if (!BCRYPT_SUCCESS(st))
    {
        // STATUS_AUTH_TAG_MISMATCH (khóa sai / dữ liệu bị sửa / gói phát lại) rơi vào đây - cố ý không phân
        // biệt lý do cụ thể với bên ngoài để không lộ thông tin hữu ích cho việc dò khóa.
        if (error) *error = "Giải mã thất bại - sai khóa hoặc dữ liệu không toàn vẹn.";
        return false;
    }

    *outPlaintext = plain.left(static_cast<int>(resultLen));
    return true;
}

bool CryptoSession::encrypt(const QByteArray& plaintext, QByteArray* outPacket, QString* error, const QByteArray& aad) const
{
    if (!m_valid || !outPacket)
    {
        if (error) *error = "Phiên mã hóa chưa sẵn sàng.";
        return false;
    }

    const QByteArray nonce = randomBytes(kNonceBytes);
    if (nonce.size() != kNonceBytes)
    {
        if (error) *error = "Không sinh được số ngẫu nhiên.";
        return false;
    }

    QByteArray cipherAndTag;
    if (!encryptWithNonce(nonce, aad, plaintext, &cipherAndTag, error))
        return false;

    *outPacket = nonce + cipherAndTag;
    return true;
}

bool CryptoSession::decrypt(const QByteArray& packet, QByteArray* outPlaintext, QString* error, const QByteArray& aad) const
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
    return decryptWithNonce(packet.left(kNonceBytes), aad, packet.mid(kNonceBytes), outPlaintext, error);
}

QByteArray CryptoSession::deriveKeyFromPairingCode(const QString& normalizedCode)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha256, normalizedCode.toUtf8(), kPairingSalt,
                                              100000, kKeyBytes);
}

QByteArray CryptoSession::randomBytes(int count)
{
    if (count <= 0)
        return QByteArray();
    QByteArray out(count, Qt::Uninitialized);
    if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(out.data()), static_cast<ULONG>(count),
                                        BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        return QByteArray();
    return out;
}

QByteArray CryptoSession::generateRandomKey()
{
    return randomBytes(kKeyBytes);
}

QByteArray CryptoSession::hmacSha256(const QByteArray& keyIn, const QByteArray& dataIn)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG)))
        return QByteArray();

    QByteArray key = keyIn;
    QByteArray data = dataIn;
    QByteArray out(32, 0);
    bool ok = false;

    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCRYPT_SUCCESS(BCryptCreateHash(alg, &hash, nullptr, 0, reinterpret_cast<PUCHAR>(key.data()),
                                        static_cast<ULONG>(key.size()), 0)))
    {
        ok = BCRYPT_SUCCESS(BCryptHashData(hash, reinterpret_cast<PUCHAR>(data.data()), static_cast<ULONG>(data.size()), 0)) &&
             BCRYPT_SUCCESS(BCryptFinishHash(hash, reinterpret_cast<PUCHAR>(out.data()), static_cast<ULONG>(out.size()), 0));
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!key.isEmpty())
        SecureZeroMemory(key.data(), static_cast<SIZE_T>(key.size()));

    return ok ? out : QByteArray();
}

bool CryptoSession::deriveSessionKeys(const QByteArray& longTermKey, const QByteArray& nonceI, const QByteArray& nonceR,
                                      const QString& initiatorId, const QString& responderId,
                                      QByteArray* keyInitiatorToResponder, QByteArray* keyResponderToInitiator)
{
    if (longTermKey.size() != kKeyBytes || nonceI.isEmpty() || nonceR.isEmpty() || initiatorId.isEmpty() ||
        responderId.isEmpty() || !keyInitiatorToResponder || !keyResponderToInitiator)
        return false;

    // nonceI/nonceR có độ dài cố định (ConnectProtocol::kNonceBytes) và id không chứa byte 0, nên phép nối
    // này không nhập nhằng (hai bộ đầu vào khác nhau không thể cho cùng một chuỗi).
    const QByteArray context = nonceI + nonceR + initiatorId.toUtf8() + QByteArray(1, '\0') + responderId.toUtf8();
    const QByteArray i2r = hmacSha256(longTermKey, kLabelInitiatorToResponder + context);
    const QByteArray r2i = hmacSha256(longTermKey, kLabelResponderToInitiator + context);
    if (i2r.size() != kKeyBytes || r2i.size() != kKeyBytes)
        return false;

    *keyInitiatorToResponder = i2r;
    *keyResponderToInitiator = r2i;
    return true;
}

QByteArray CryptoSession::protectForCurrentUser(const QByteArray& plainIn)
{
    if (plainIn.isEmpty())
        return QByteArray();

    QByteArray plain = plainIn;
    QByteArray entropy = kDpapiEntropy;
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(plain.data())};
    DATA_BLOB ent{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(entropy.data())};
    DATA_BLOB out{0, nullptr};

    // CRYPTPROTECT_UI_FORBIDDEN: không bao giờ bật hộp thoại (ứng dụng có thể đang chạy lúc không ai ngồi máy).
    const BOOL ok = CryptProtectData(&in, L"OneForAll Connect Together", &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    SecureZeroMemory(plain.data(), static_cast<SIZE_T>(plain.size()));
    if (!ok || !out.pbData)
        return QByteArray();

    const QByteArray result(reinterpret_cast<const char*>(out.pbData), static_cast<qsizetype>(out.cbData));
    LocalFree(out.pbData);
    return result;
}

QByteArray CryptoSession::unprotectForCurrentUser(const QByteArray& protectedIn)
{
    if (protectedIn.isEmpty())
        return QByteArray();

    QByteArray blob = protectedIn;
    QByteArray entropy = kDpapiEntropy;
    DATA_BLOB in{static_cast<DWORD>(blob.size()), reinterpret_cast<BYTE*>(blob.data())};
    DATA_BLOB ent{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(entropy.data())};
    DATA_BLOB out{0, nullptr};

    if (!CryptUnprotectData(&in, nullptr, &ent, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out) || !out.pbData)
        return QByteArray();

    const QByteArray result(reinterpret_cast<const char*>(out.pbData), static_cast<qsizetype>(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}
