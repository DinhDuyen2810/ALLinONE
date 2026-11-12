#pragma once

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

/// Một tài khoản xác thực hai bước (TOTP/HOTP, RFC 6238/4226) - cùng loại với Google Authenticator,
/// Microsoft Authenticator, Authy, Aegis, 2FAS...
struct OtpAccount
{
    enum class Type
    {
        Totp,
        Hotp
    };

    QString id;      // định danh nội bộ trong kho (rỗng cho tới khi thêm vào kho)
    QString issuer;  // nhà cung cấp (Google, GitHub...)
    QString name;    // tài khoản (email/tên đăng nhập)
    QByteArray secret;
    Type type{Type::Totp};
    QString algorithm{"SHA1"}; // SHA1 | SHA256 | SHA512
    int digits{6};
    int period{30};            // chỉ TOTP
    qint64 counter{0};         // chỉ HOTP: giá trị bộ đếm SẼ dùng cho mã kế tiếp

    QString displayName() const; // "Issuer (name)" hoặc phần nào có
    bool sameAccount(const OtpAccount& o) const; // cùng khóa + cùng nhà cung cấp + cùng tài khoản
};

/// Toàn bộ phần thuật toán (không widget, không I/O) của tab Authenticator.
class Otp
{
public:
    /// Base32 RFC 4648: bỏ khoảng trắng/dấu gạch, không phân biệt hoa thường, chấp nhận thiếu '='.
    static QByteArray base32Decode(const QString& text, bool* ok = nullptr);
    static QString base32Encode(const QByteArray& data);

    /// Phân tích `otpauth://totp|hotp/<nhãn>?secret=...&issuer=...&algorithm=...&digits=...&period=...&counter=...`.
    static bool parseUri(const QString& uri, OtpAccount* out, QString* error = nullptr);
    /// Dựng ngược lại URI otpauth chuẩn (để xuất thành mã QR nhập sang ứng dụng khác).
    static QString buildUri(const OtpAccount& account);

    /// Phân tích `otpauth-migration://offline?data=<base64 protobuf>` - mã QR "Xuất tài khoản" của Google Authenticator
    /// (một mã có thể chứa NHIỀU tài khoản, và một lần xuất lớn được chia thành nhiều mã QR). Bỏ qua tài khoản dùng
    /// thuật toán không hỗ trợ (MD5) và ghi vào `error` nếu có tài khoản bị bỏ qua; trả về danh sách các tài khoản đọc được.
    static QList<OtpAccount> parseMigration(const QString& uri, QString* error = nullptr);

    static bool isOtpUri(const QString& text);
    /// Che khóa bí mật trong URI (secret=... / data=...) để hiện lên màn hình/ghi log.
    static QString maskUri(const QString& uri);

    /// Mã HOTP (RFC 4226) cho bộ đếm cho trước.
    static QString hotp(const OtpAccount& account, qint64 counter);
    /// Mã hiện tại: TOTP (RFC 6238) theo thời gian Unix; HOTP dùng account.counter.
    static QString code(const OtpAccount& account, qint64 unixSeconds);
    /// Số giây còn lại của mã TOTP hiện tại (1..period). HOTP: 0.
    static int remainingSeconds(const OtpAccount& account, qint64 unixSeconds);

    /// Bọc/mở bằng DPAPI của Windows (phạm vi NGƯỜI DÙNG Windows hiện tại): kho lưu khóa không đọc được nếu chép sang
    /// tài khoản/máy khác. Trả về mảng rỗng nếu thất bại.
    static QByteArray protect(const QByteArray& plain);
    static QByteArray unprotect(const QByteArray& cipher);
};

/// Kho tài khoản, lưu ở AppPaths::profileFile("authenticator.json"). Khóa bí mật được bọc DPAPI từng cái; phần còn lại
/// (nhà cung cấp, tài khoản, tham số) để rõ trong JSON. Mỗi thao tác ghi đều đọc lại tệp TRƯỚC (hai bản ứng dụng cùng chạy
/// không đè mất thay đổi của nhau) rồi ghi nguyên tử qua QSaveFile.
class AuthenticatorStore : public QObject
{
    Q_OBJECT

public:
    static AuthenticatorStore& instance();
    explicit AuthenticatorStore(const QString& path, QObject* parent = nullptr);

    const QList<OtpAccount>& accounts() const { return m_accounts; }
    /// Thêm; trả về false nếu tài khoản cùng khóa đã có (không thêm trùng) hoặc ghi thất bại (xem lastError()).
    bool add(OtpAccount account);
    bool update(const OtpAccount& account); // theo id
    bool remove(const QString& id);
    /// Tăng bộ đếm HOTP của tài khoản `id` thêm 1 và lưu (gọi sau khi người dùng đã DÙNG mã hiện tại).
    bool advanceCounter(const QString& id);

    bool load();
    QString filePath() const { return m_path; }
    QString lastError() const { return m_error; }

signals:
    void changed();

private:
    AuthenticatorStore();
    bool save();
    bool readFile(QList<OtpAccount>* out);

    QString m_path;
    QList<OtpAccount> m_accounts;
    QString m_error;
};
