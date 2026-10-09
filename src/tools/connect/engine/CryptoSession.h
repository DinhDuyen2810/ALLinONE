#pragma once

#include <QByteArray>
#include <QString>

/**
 * @brief Mã hóa/xác thực AES-256-GCM cho một phiên, dùng Windows CNG (bcrypt.dll) - thư viện mật mã
 * của hệ điều hành, không tự viết thuật toán mã hóa.
 *
 * Hai cách dùng nonce, tùy giai đoạn (NetworkSession quyết định):
 *  - encrypt()/decrypt(): mỗi gói tự mang nonce NGẪU NHIÊN 12 byte (nonce || bản mã || tag 16 byte). Chỉ
 *    dùng cho vài gói bắt tay đầu mỗi kết nối (PairRequest/PairAccept/SessionHello/SessionHelloAck), nơi
 *    cùng một khóa (khóa suy từ mã ghép đôi, hoặc khóa dài hạn) được dùng lại qua nhiều kết nối nên KHÔNG
 *    thể đếm từ 0.
 *  - encryptWithNonce()/decryptWithNonce(): nonce do nơi gọi cấp (bộ đếm tuần tự của khóa PHIÊN - mỗi
 *    kết nối, mỗi chiều một khóa riêng nên bộ đếm không bao giờ lặp lại dưới cùng một khóa).
 *
 * GCM tự kiểm tra tính toàn vẹn: nếu khóa sai (ví dụ nhập sai mã ghép đôi), nonce sai (gói bị phát lại/
 * đảo thứ tự) hoặc dữ liệu kèm theo (AAD) sai (gói bị phản xạ ngược về chính bên gửi), giải mã thất bại
 * rõ ràng thay vì trả về dữ liệu rác - đây chính là cơ chế xác thực 2 máy cùng biết một bí mật, không
 * cần truyền mã ghép đôi qua mạng.
 */
class CryptoSession
{
public:
    static constexpr int kKeyBytes = 32;   // AES-256
    static constexpr int kNonceBytes = 12; // kích thước nonce khuyến nghị cho GCM
    static constexpr int kTagBytes = 16;

    explicit CryptoSession(const QByteArray& key256bit);
    ~CryptoSession();

    CryptoSession(const CryptoSession&) = delete;
    CryptoSession& operator=(const CryptoSession&) = delete;

    bool isValid() const { return m_valid; }

    /// Gói tin ra: nonce(12, ngẫu nhiên) || ciphertext || tag(16). 'aad' được xác thực nhưng không mã hóa
    /// và không nằm trong gói - hai bên phải tự biết cùng một giá trị.
    bool encrypt(const QByteArray& plaintext, QByteArray* outPacket, QString* error = nullptr,
                 const QByteArray& aad = QByteArray()) const;
    bool decrypt(const QByteArray& packet, QByteArray* outPlaintext, QString* error = nullptr,
                 const QByteArray& aad = QByteArray()) const;

    /// Gói tin ra: ciphertext || tag(16) - nonce (đúng 12 byte) do nơi gọi quản lý, không nằm trong gói.
    bool encryptWithNonce(const QByteArray& nonce, const QByteArray& aad, const QByteArray& plaintext,
                          QByteArray* outCipherAndTag, QString* error = nullptr) const;
    bool decryptWithNonce(const QByteArray& nonce, const QByteArray& aad, const QByteArray& cipherAndTag,
                          QByteArray* outPlaintext, QString* error = nullptr) const;

    /// Suy khóa 256-bit từ mã ghép đôi (PBKDF2-HMAC-SHA256, 100k vòng, salt cố định theo ứng dụng -
    /// chấp nhận được vì mã ghép đôi chỉ dùng một lần, ngắn hạn, không phải mật khẩu lâu dài).
    /// CHẬM có chủ đích (~0,1 giây) - nơi gọi tính MỘT LẦN cho mỗi mã, không tính lại cho mỗi kết nối.
    static QByteArray deriveKeyFromPairingCode(const QString& normalizedCode);

    /// Khóa dài hạn ngẫu nhiên mạnh (256-bit) để dùng cho các phiên sau khi đã ghép đôi.
    static QByteArray generateRandomKey();

    /// 'count' byte ngẫu nhiên từ bộ sinh số ngẫu nhiên mật mã của hệ điều hành (BCryptGenRandom).
    static QByteArray randomBytes(int count);

    /// HMAC-SHA256 qua CNG. Rỗng nếu thất bại.
    static QByteArray hmacSha256(const QByteArray& key, const QByteArray& data);

    /// Suy hai khóa PHIÊN (mỗi chiều một khóa) từ khóa dài hạn + nonce ngẫu nhiên của CẢ HAI bên + id hai
    /// máy: K = HMAC-SHA256(longTermKey, nhãn chiều || nonceI || nonceR || idI || 0 || idR). Mỗi kết nối
    /// có khóa riêng (nonce mới) nên gói bắt được ở kết nối cũ vô dụng ở kết nối mới (chống phát lại);
    /// mỗi chiều có khóa riêng nên gói của chính mình bị dội ngược lại không giải mã được (chống phản xạ).
    /// "I" = bên mở kết nối TCP, "R" = bên nhận kết nối. false nếu đầu vào không hợp lệ.
    static bool deriveSessionKeys(const QByteArray& longTermKey, const QByteArray& nonceI, const QByteArray& nonceR,
                                  const QString& initiatorId, const QString& responderId,
                                  QByteArray* keyInitiatorToResponder, QByteArray* keyResponderToInitiator);

    /// Bọc/mở dữ liệu bằng DPAPI của Windows (CryptProtectData, phạm vi NGƯỜI DÙNG Windows hiện tại): chỉ
    /// đúng tài khoản này trên đúng máy này mở lại được. Dùng để lưu khóa dài hạn xuống đĩa (PeerStore) -
    /// chép tệp sang máy/tài khoản khác thì không dùng được khóa. Rỗng nếu thất bại.
    static QByteArray protectForCurrentUser(const QByteArray& plain);
    static QByteArray unprotectForCurrentUser(const QByteArray& protectedBlob);

private:
    struct Impl;
    Impl* m_impl{nullptr};
    bool m_valid{false};
};
