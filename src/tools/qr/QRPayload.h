#pragma once

#include <QList>
#include <QPair>
#include <QString>

enum class QRContentType
{
    Text,
    Url,
    Wifi,
    Email,
    Phone,
    Sms,
    Geo,
    VCard
};

/// Dữ liệu đầu vào khi tạo từng loại nội dung.
struct WifiInfo
{
    QString ssid;
    QString password;
    QString security{"WPA"}; // WPA, WEP, nopass
    bool hidden{false};
};

struct EmailInfo
{
    QString to;
    QString subject;
    QString body;
};

struct SmsInfo
{
    QString number;
    QString message;
};

struct GeoInfo
{
    double latitude{0};
    double longitude{0};
};

struct VCardInfo
{
    QString firstName;
    QString lastName;
    QString organization;
    QString title;
    QString phone;
    QString email;
    QString url;
    QString address;
};

/// Kết quả phân tích nội dung quét được.
struct ParsedPayload
{
    QRContentType type{QRContentType::Text};
    QString typeName;                           // Tên hiển thị của loại
    QList<QPair<QString, QString>> fields;      // Các trường (nhãn, giá trị)
    QString actionUrl;                          // URL có thể mở (http/https/mailto/tel/map...)
    QString actionLabel;                        // Nhãn nút hành động
    int secretField{-1};                        // Chỉ số trong fields của trường NHẠY CẢM (mật khẩu WiFi) - giao
                                                // diện phải che mặc định; -1 nếu không có
};

class QRPayload
{
public:
    static QString typeName(QRContentType type);

    // ---- Tạo nội dung ----
    static QString makeText(const QString& text) { return text; }
    static QString makeUrl(const QString& url);                 // tự thêm https:// nếu thiếu scheme
    static QString makeWifi(const WifiInfo& info);
    static QString makeEmail(const EmailInfo& info);
    static QString makePhone(const QString& number);
    static QString makeSms(const SmsInfo& info);
    static QString makeGeo(const GeoInfo& info);
    static QString makeVCard(const VCardInfo& info);

    // ---- Phân tích nội dung quét được ----
    static ParsedPayload parse(const QString& content);

    /// Bản để HIỂN THỊ của nội dung: nếu là payload WIFI: có mật khẩu thì thay giá trị P: bằng dấu chấm
    /// (••••••••), mọi phần khác giữ nguyên; nội dung loại khác trả về nguyên văn. Dùng cho danh sách kết
    /// quả/lịch sử/tooltip - nơi mật khẩu WiFi không nên nằm phơi trên màn hình khi người dùng chưa chủ
    /// động yêu cầu xem. KHÔNG dùng kết quả này để mã hóa lại/sao chép (đã mất mật khẩu thật).
    static QString maskSecrets(const QString& content);
    static QString secretMask() { return QStringLiteral("••••••••"); }

    /// Escape cho định dạng WIFI: \ ; , : "
    static QString escapeWifi(const QString& s);
    static QString unescapeWifi(const QString& s);
};
