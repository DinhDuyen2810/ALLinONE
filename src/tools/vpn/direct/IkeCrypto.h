#pragma once

#include <QByteArray>
#include <QString>

/// Nguyên thủy mật mã cho IKEv1 (RFC 2409) dùng cho kết nối trực tiếp FortiGate IPsec - trao đổi khóa
/// Diffie-Hellman (nhóm MODP) + hàm giả ngẫu nhiên PRF (HMAC) + băm + AES-CBC để mã hóa phiên thương lượng.
/// Dựa trên BCrypt của Windows (không thêm thư viện). "Thuần" theo nghĩa không mạng - test bằng vector chuẩn.
namespace IkeCrypto
{
/// Nhóm Diffie-Hellman MODP hỗ trợ (số nhóm theo IANA/RFC 3526).
enum class DhGroup
{
    Modp1024 = 2,
    Modp1536 = 5,
    Modp2048 = 14,
};

/// Một phiên Diffie-Hellman (giữ khóa riêng qua BCrypt). Dùng: khởi tạo nhóm -> publicKey() gửi đi ->
/// computeShared(peer) lấy bí mật chung. Độ dài public/shared = kích thước nhóm (128/192/256 byte).
class Dh
{
public:
    Dh() = default;
    ~Dh();
    Dh(const Dh&) = delete;
    Dh& operator=(const Dh&) = delete;

    bool generate(DhGroup group, QString* error = nullptr);
    QByteArray publicKey() const { return m_public; }
    /// Bí mật chung g^xy (đệm 0 bên trái cho đủ kích thước nhóm). Rỗng nếu lỗi/peer sai độ dài.
    QByteArray computeShared(const QByteArray& peerPublic) const;
    int sizeBytes() const { return m_size; }

private:
    void* m_key{nullptr}; // BCRYPT_KEY_HANDLE
    void* m_alg{nullptr}; // BCRYPT_ALG_HANDLE
    QByteArray m_public;
    int m_size{0};
};

enum class Hash
{
    Sha1,
    Sha256,
    Md5,
};

/// Độ dài kết quả băm (byte).
int hashLen(Hash h);
/// Băm thường.
QByteArray hash(Hash h, const QByteArray& data);
/// PRF của IKEv1 = HMAC-<hash>.
QByteArray prf(Hash h, const QByteArray& key, const QByteArray& data);
/// "prf+" kiểu IKE: T1=prf(key, seed|0x01), T2=prf(key, T1|seed|0x02), ... ghép tới khi đủ 'length' byte.
QByteArray prfPlus(Hash h, const QByteArray& key, const QByteArray& seed, int length);

/// AES-CBC (không đệm - dữ liệu phải bội số 16). key 16/24/32 byte, iv 16 byte. Rỗng nếu lỗi.
QByteArray aesCbcEncrypt(const QByteArray& key, const QByteArray& iv, const QByteArray& data);
QByteArray aesCbcDecrypt(const QByteArray& key, const QByteArray& iv, const QByteArray& data);

/// Byte ngẫu nhiên mã hóa an toàn.
QByteArray randomBytes(int n);
} // namespace IkeCrypto
