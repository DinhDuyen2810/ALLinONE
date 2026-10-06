#pragma once

#include <QByteArray>
#include <QString>

/**
 * @brief Mã hóa/xác thực AES-256-GCM cho một phiên, dùng Windows CNG (bcrypt.dll) - thư viện mật mã
 * của hệ điều hành, không tự viết thuật toán mã hóa.
 *
 * Mỗi gói tin độc lập: nonce ngẫu nhiên 12 byte + bản mã + thẻ xác thực (tag) 16 byte, nối liền nhau.
 * GCM tự kiểm tra tính toàn vẹn: nếu khóa sai (ví dụ nhập sai mã ghép đôi), decrypt() thất bại rõ ràng
 * thay vì trả về dữ liệu rác - đây chính là cơ chế xác thực 2 máy cùng biết một bí mật, không cần
 * truyền mã ghép đôi qua mạng.
 */
class CryptoSession
{
public:
    explicit CryptoSession(const QByteArray& key256bit);
    ~CryptoSession();

    CryptoSession(const CryptoSession&) = delete;
    CryptoSession& operator=(const CryptoSession&) = delete;

    bool isValid() const { return m_valid; }

    /// Gói tin ra: nonce(12) || ciphertext || tag(16).
    bool encrypt(const QByteArray& plaintext, QByteArray* outPacket, QString* error = nullptr) const;
    bool decrypt(const QByteArray& packet, QByteArray* outPlaintext, QString* error = nullptr) const;

    /// Suy khóa 256-bit từ mã ghép đôi (PBKDF2-HMAC-SHA256, 100k vòng, salt cố định theo ứng dụng -
    /// chấp nhận được vì mã ghép đôi chỉ dùng một lần, ngắn hạn, không phải mật khẩu lâu dài).
    static QByteArray deriveKeyFromPairingCode(const QString& normalizedCode);

    /// Khóa dài hạn ngẫu nhiên mạnh (256-bit) để dùng cho các phiên sau khi đã ghép đôi.
    static QByteArray generateRandomKey();

private:
    struct Impl;
    Impl* m_impl{nullptr};
    bool m_valid{false};
};
