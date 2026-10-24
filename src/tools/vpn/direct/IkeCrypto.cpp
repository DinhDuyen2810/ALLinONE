#include "IkeCrypto.h"

#include <QRandomGenerator>

#include <windows.h>
#include <bcrypt.h>

#include <cstring>

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

namespace IkeCrypto
{
namespace
{
// Số nguyên tố MODP (RFC 3526 / RFC 2409), generator = 2. Chuỗi hex không dấu cách.
const char* kModp1024 =
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DD"
    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE65381FFFFFFFFFFFFFFFF";
const char* kModp1536 =
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DD"
    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F"
    "83655D23DCA3AD961C62F356208552BB9ED529077096966D670C354E4ABC9804F1746C08CA237327FFFFFFFFFFFFFFFF";
const char* kModp2048 =
    "FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74020BBEA63B139B22514A08798E3404DD"
    "EF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7ED"
    "EE386BFB5A899FA5AE9F24117C4B1FE649286651ECE45B3DC2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F"
    "83655D23DCA3AD961C62F356208552BB9ED529077096966D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B"
    "E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955817183995497CEA956AE515D2261898FA0510"
    "15728E5A8AACAA68FFFFFFFFFFFFFFFF";

QByteArray primeFor(DhGroup g, int* sizeBytes)
{
    const char* hex = nullptr;
    switch (g)
    {
        case DhGroup::Modp1024: hex = kModp1024; *sizeBytes = 128; break;
        case DhGroup::Modp1536: hex = kModp1536; *sizeBytes = 192; break;
        case DhGroup::Modp2048: hex = kModp2048; *sizeBytes = 256; break;
    }
    return QByteArray::fromHex(hex);
}

const wchar_t* bcryptHashAlg(Hash h)
{
    switch (h)
    {
        case Hash::Sha1: return BCRYPT_SHA1_ALGORITHM;
        case Hash::Sha256: return BCRYPT_SHA256_ALGORITHM;
        case Hash::Md5: return BCRYPT_MD5_ALGORITHM;
    }
    return BCRYPT_SHA1_ALGORITHM;
}

QByteArray padLeft(const QByteArray& b, int size)
{
    if (b.size() >= size)
        return b.right(size);
    return QByteArray(size - b.size(), '\0') + b;
}

bool isEcp(DhGroup g) { return g == DhGroup::Ecp256 || g == DhGroup::Ecp384 || g == DhGroup::Ecp521; }

// Thông tin đường cong elip (RFC 5903): định danh thuật toán BCrypt, độ dài toạ độ, số bit, magic blob public.
struct EcpInfo { const wchar_t* alg; int coord; ULONG bits; ULONG pubMagic; };
EcpInfo ecpInfo(DhGroup g)
{
    switch (g)
    {
        case DhGroup::Ecp256: return {BCRYPT_ECDH_P256_ALGORITHM, 32, 256, BCRYPT_ECDH_PUBLIC_P256_MAGIC};
        case DhGroup::Ecp384: return {BCRYPT_ECDH_P384_ALGORITHM, 48, 384, BCRYPT_ECDH_PUBLIC_P384_MAGIC};
        case DhGroup::Ecp521: return {BCRYPT_ECDH_P521_ALGORITHM, 66, 521, BCRYPT_ECDH_PUBLIC_P521_MAGIC};
        default: return {BCRYPT_ECDH_P384_ALGORITHM, 48, 384, BCRYPT_ECDH_PUBLIC_P384_MAGIC};
    }
}
} // namespace

// ---- Diffie-Hellman ----

Dh::~Dh()
{
    if (m_key)
        BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(m_key));
    if (m_alg)
        BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(m_alg), 0);
}

bool Dh::generate(DhGroup group, QString* error)
{
    auto fail = [&](const QString& m) {
        if (error)
            *error = m;
        return false;
    };

    // ---- Đường cong elip (ECDH, nhóm 19/20/21) ----
    if (isEcp(group))
    {
        const EcpInfo ec = ecpInfo(group);
        m_ecp = true;
        m_coord = ec.coord;
        m_size = 2 * ec.coord; // KE = X||Y
        BCRYPT_ALG_HANDLE alg = nullptr;
        if (BCryptOpenAlgorithmProvider(&alg, ec.alg, nullptr, 0) != STATUS_SUCCESS)
            return fail(QStringLiteral("Không mở được ECDH (BCrypt)."));
        m_alg = alg;
        BCRYPT_KEY_HANDLE key = nullptr;
        if (BCryptGenerateKeyPair(alg, &key, ec.bits, 0) != STATUS_SUCCESS)
            return fail(QStringLiteral("Không tạo được khóa ECDH."));
        m_key = key;
        if (BCryptFinalizeKeyPair(key, 0) != STATUS_SUCCESS)
            return fail(QStringLiteral("Không hoàn tất được khóa ECDH."));
        ULONG blobLen = 0;
        BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, nullptr, 0, &blobLen, 0);
        QByteArray blob(static_cast<int>(blobLen), '\0');
        if (BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, reinterpret_cast<PUCHAR>(blob.data()), blobLen, &blobLen, 0) !=
            STATUS_SUCCESS)
            return fail(QStringLiteral("Không xuất được public key ECDH."));
        // BCRYPT_ECCKEY_BLOB { dwMagic, cbKey } rồi X[cbKey], Y[cbKey]. KE của IKE = X||Y (không byte định dạng).
        const int off = sizeof(BCRYPT_ECCKEY_BLOB);
        m_public = blob.mid(off, 2 * ec.coord);
        return m_public.size() == 2 * ec.coord;
    }

    const QByteArray prime = primeFor(group, &m_size);
    const QByteArray gen = padLeft(QByteArray::fromHex("02"), m_size);

    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_DH_ALGORITHM, nullptr, 0) != STATUS_SUCCESS)
        return fail(QStringLiteral("Không mở được DH (BCrypt)."));
    m_alg = alg;

    // Khối tham số DH: BCRYPT_DH_PARAMETER_HEADER { cbLength, dwMagic, cbKeyLength } + prime + generator.
    const int dhSize = m_size;
    QByteArray params(sizeof(BCRYPT_DH_PARAMETER_HEADER) + 2 * dhSize, '\0');
    auto* hdr = reinterpret_cast<BCRYPT_DH_PARAMETER_HEADER*>(params.data());
    hdr->cbLength = static_cast<ULONG>(params.size());
    hdr->dwMagic = BCRYPT_DH_PARAMETERS_MAGIC;
    hdr->cbKeyLength = static_cast<ULONG>(dhSize);
    std::memcpy(params.data() + sizeof(*hdr), prime.constData(), dhSize);
    std::memcpy(params.data() + sizeof(*hdr) + dhSize, gen.constData(), dhSize);

    BCRYPT_KEY_HANDLE key = nullptr;
    if (BCryptGenerateKeyPair(alg, &key, dhSize * 8, 0) != STATUS_SUCCESS)
        return fail(QStringLiteral("Không tạo được khóa DH."));
    m_key = key;
    if (BCryptSetProperty(key, BCRYPT_DH_PARAMETERS, reinterpret_cast<PUCHAR>(params.data()),
                          static_cast<ULONG>(params.size()), 0) != STATUS_SUCCESS)
        return fail(QStringLiteral("Không đặt được tham số DH."));
    if (BCryptFinalizeKeyPair(key, 0) != STATUS_SUCCESS)
        return fail(QStringLiteral("Không hoàn tất được khóa DH."));

    // Xuất public (BCRYPT_DH_PUBLIC_BLOB: header + Modulus + Generator + Public). Lấy phần Public cuối.
    ULONG blobLen = 0;
    BCryptExportKey(key, nullptr, BCRYPT_DH_PUBLIC_BLOB, nullptr, 0, &blobLen, 0);
    QByteArray blob(static_cast<int>(blobLen), '\0');
    if (BCryptExportKey(key, nullptr, BCRYPT_DH_PUBLIC_BLOB, reinterpret_cast<PUCHAR>(blob.data()), blobLen, &blobLen, 0) !=
        STATUS_SUCCESS)
        return fail(QStringLiteral("Không xuất được public key DH."));
    // Bố cục: BCRYPT_DH_KEY_BLOB header (cbKey) rồi Modulus[cbKey], Generator[cbKey], Public[cbKey].
    const int off = sizeof(BCRYPT_DH_KEY_BLOB) + 2 * dhSize;
    m_public = blob.mid(off, dhSize);
    return m_public.size() == dhSize;
}

QByteArray Dh::computeShared(const QByteArray& peerPublic) const
{
    if (!m_key || peerPublic.size() != m_size)
        return {};

    // ---- ECDH: bí mật chung = toạ độ X của điểm chung (RFC 5903), độ dài = 1 toạ độ ----
    if (m_ecp)
    {
        // Suy lại magic public theo độ dài toạ độ.
        const ULONG pubMagic = m_coord == 32 ? BCRYPT_ECDH_PUBLIC_P256_MAGIC
                             : (m_coord == 48 ? BCRYPT_ECDH_PUBLIC_P384_MAGIC : BCRYPT_ECDH_PUBLIC_P521_MAGIC);
        QByteArray blob(sizeof(BCRYPT_ECCKEY_BLOB) + 2 * m_coord, '\0');
        auto* kh = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
        kh->dwMagic = pubMagic;
        kh->cbKey = static_cast<ULONG>(m_coord);
        std::memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), peerPublic.constData(), 2 * m_coord);

        BCRYPT_KEY_HANDLE peer = nullptr;
        if (BCryptImportKeyPair(static_cast<BCRYPT_ALG_HANDLE>(m_alg), nullptr, BCRYPT_ECCPUBLIC_BLOB, &peer,
                                reinterpret_cast<PUCHAR>(blob.data()), static_cast<ULONG>(blob.size()), 0) != STATUS_SUCCESS)
            return {};
        BCRYPT_SECRET_HANDLE secret = nullptr;
        if (BCryptSecretAgreement(static_cast<BCRYPT_KEY_HANDLE>(m_key), peer, &secret, 0) != STATUS_SUCCESS)
        {
            BCryptDestroyKey(peer);
            return {};
        }
        ULONG len = 0;
        QByteArray out;
        if (BCryptDeriveKey(secret, BCRYPT_KDF_RAW_SECRET, nullptr, nullptr, 0, &len, 0) == STATUS_SUCCESS)
        {
            out.resize(static_cast<int>(len));
            if (BCryptDeriveKey(secret, BCRYPT_KDF_RAW_SECRET, nullptr, reinterpret_cast<PUCHAR>(out.data()), len, &len, 0) ==
                STATUS_SUCCESS)
            {
                std::reverse(out.begin(), out.end()); // little-endian -> big-endian toạ độ X
                out = padLeft(out, m_coord);
            }
            else
                out.clear();
        }
        BCryptDestroySecret(secret);
        BCryptDestroyKey(peer);
        return out;
    }

    // Dựng public blob của peer từ cùng tham số (modulus+generator) + public của peer.
    int size = 0;
    DhGroup g = m_size == 128 ? DhGroup::Modp1024 : (m_size == 192 ? DhGroup::Modp1536 : DhGroup::Modp2048);
    const QByteArray prime = primeFor(g, &size);
    const QByteArray gen = padLeft(QByteArray::fromHex("02"), m_size);

    QByteArray blob(sizeof(BCRYPT_DH_KEY_BLOB) + 3 * m_size, '\0');
    auto* kh = reinterpret_cast<BCRYPT_DH_KEY_BLOB*>(blob.data());
    kh->dwMagic = BCRYPT_DH_PUBLIC_MAGIC;
    kh->cbKey = static_cast<ULONG>(m_size);
    char* p = blob.data() + sizeof(BCRYPT_DH_KEY_BLOB);
    std::memcpy(p, prime.constData(), m_size);
    std::memcpy(p + m_size, gen.constData(), m_size);
    std::memcpy(p + 2 * m_size, peerPublic.constData(), m_size);

    BCRYPT_KEY_HANDLE peer = nullptr;
    if (BCryptImportKeyPair(static_cast<BCRYPT_ALG_HANDLE>(m_alg), nullptr, BCRYPT_DH_PUBLIC_BLOB, &peer,
                            reinterpret_cast<PUCHAR>(blob.data()), static_cast<ULONG>(blob.size()), 0) != STATUS_SUCCESS)
        return {};

    BCRYPT_SECRET_HANDLE secret = nullptr;
    if (BCryptSecretAgreement(static_cast<BCRYPT_KEY_HANDLE>(m_key), peer, &secret, 0) != STATUS_SUCCESS)
    {
        BCryptDestroyKey(peer);
        return {};
    }
    // Lấy bí mật thô (không băm): dùng BCRYPT_KDF_RAW_SECRET -> trả theo thứ tự LITTLE-endian, phải đảo lại.
    ULONG len = 0;
    QByteArray out;
    if (BCryptDeriveKey(secret, BCRYPT_KDF_RAW_SECRET, nullptr, nullptr, 0, &len, 0) == STATUS_SUCCESS)
    {
        out.resize(static_cast<int>(len));
        if (BCryptDeriveKey(secret, BCRYPT_KDF_RAW_SECRET, nullptr, reinterpret_cast<PUCHAR>(out.data()), len, &len, 0) ==
            STATUS_SUCCESS)
        {
            std::reverse(out.begin(), out.end()); // little-endian -> big-endian
            out = padLeft(out, m_size);
        }
        else
            out.clear();
    }
    BCryptDestroySecret(secret);
    BCryptDestroyKey(peer);
    return out;
}

// ---- Băm / PRF ----

int hashLen(Hash h)
{
    switch (h)
    {
        case Hash::Sha1: return 20;
        case Hash::Sha256: return 32;
        case Hash::Md5: return 16;
    }
    return 20;
}

QByteArray hash(Hash h, const QByteArray& data)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, bcryptHashAlg(h), nullptr, 0) != STATUS_SUCCESS)
        return {};
    BCRYPT_HASH_HANDLE hh = nullptr;
    QByteArray out(hashLen(h), '\0');
    if (BCryptCreateHash(alg, &hh, nullptr, 0, nullptr, 0, 0) == STATUS_SUCCESS)
    {
        BCryptHashData(hh, reinterpret_cast<PUCHAR>(const_cast<char*>(data.constData())), static_cast<ULONG>(data.size()), 0);
        BCryptFinishHash(hh, reinterpret_cast<PUCHAR>(out.data()), static_cast<ULONG>(out.size()), 0);
        BCryptDestroyHash(hh);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

QByteArray prf(Hash h, const QByteArray& key, const QByteArray& data)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, bcryptHashAlg(h), nullptr, BCRYPT_ALG_HANDLE_HMAC_FLAG) != STATUS_SUCCESS)
        return {};
    BCRYPT_HASH_HANDLE hh = nullptr;
    QByteArray out(hashLen(h), '\0');
    if (BCryptCreateHash(alg, &hh, nullptr, 0, reinterpret_cast<PUCHAR>(const_cast<char*>(key.constData())),
                         static_cast<ULONG>(key.size()), 0) == STATUS_SUCCESS)
    {
        BCryptHashData(hh, reinterpret_cast<PUCHAR>(const_cast<char*>(data.constData())), static_cast<ULONG>(data.size()), 0);
        BCryptFinishHash(hh, reinterpret_cast<PUCHAR>(out.data()), static_cast<ULONG>(out.size()), 0);
        BCryptDestroyHash(hh);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}

QByteArray prfPlus(Hash h, const QByteArray& key, const QByteArray& seed, int length)
{
    QByteArray out, t;
    quint8 counter = 1;
    while (out.size() < length)
    {
        t = prf(h, key, t + seed + QByteArray(1, static_cast<char>(counter)));
        out += t;
        ++counter;
    }
    return out.left(length);
}

// ---- AES-CBC ----

namespace
{
QByteArray aesCbc(const QByteArray& key, const QByteArray& iv, const QByteArray& data, bool enc)
{
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0) != STATUS_SUCCESS)
        return {};
    BCryptSetProperty(alg, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CBC)),
                      sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    DWORD objLen = 0, cb = 0;
    BCryptGetProperty(alg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cb, 0);
    QByteArray keyObj(static_cast<int>(objLen), '\0');
    BCRYPT_KEY_HANDLE key2 = nullptr;
    QByteArray out;
    if (BCryptGenerateSymmetricKey(alg, &key2, reinterpret_cast<PUCHAR>(keyObj.data()), objLen,
                                   reinterpret_cast<PUCHAR>(const_cast<char*>(key.constData())), static_cast<ULONG>(key.size()),
                                   0) == STATUS_SUCCESS)
    {
        QByteArray ivCopy = iv;
        out.resize(data.size() + 16);
        ULONG done = 0;
        const NTSTATUS st = enc
            ? BCryptEncrypt(key2, reinterpret_cast<PUCHAR>(const_cast<char*>(data.constData())),
                            static_cast<ULONG>(data.size()), nullptr, reinterpret_cast<PUCHAR>(ivCopy.data()), 16,
                            reinterpret_cast<PUCHAR>(out.data()), static_cast<ULONG>(out.size()), &done, 0)
            : BCryptDecrypt(key2, reinterpret_cast<PUCHAR>(const_cast<char*>(data.constData())),
                            static_cast<ULONG>(data.size()), nullptr, reinterpret_cast<PUCHAR>(ivCopy.data()), 16,
                            reinterpret_cast<PUCHAR>(out.data()), static_cast<ULONG>(out.size()), &done, 0);
        if (st == STATUS_SUCCESS)
            out.truncate(static_cast<int>(done));
        else
            out.clear();
        BCryptDestroyKey(key2);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return out;
}
} // namespace

QByteArray aesCbcEncrypt(const QByteArray& key, const QByteArray& iv, const QByteArray& data)
{
    return aesCbc(key, iv, data, true);
}
QByteArray aesCbcDecrypt(const QByteArray& key, const QByteArray& iv, const QByteArray& data)
{
    return aesCbc(key, iv, data, false);
}

QByteArray randomBytes(int n)
{
    QByteArray b(n, Qt::Uninitialized);
    QRandomGenerator::system()->generate(b.begin(), b.end());
    return b;
}
} // namespace IkeCrypto
