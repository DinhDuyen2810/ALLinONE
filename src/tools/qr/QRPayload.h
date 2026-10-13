#pragma once

#include <QList>
#include <QPair>
#include <QString>
#include <QUrl>

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
    /// Phân tích RIÊNG một chuỗi "WIFI:..." thành WifiInfo có cấu trúc (khác parse() trả về danh sách
    /// (nhãn, giá trị) để HIỂN THỊ) - dùng khi cần SSID/mật khẩu/bảo mật tách bạch để LÀM GÌ ĐÓ với nó (vd
    /// nút "Kết nối WiFi" ở QRScanTab). Trả về WifiInfo rỗng (ssid rỗng) nếu content không đúng định dạng.
    static WifiInfo parseWifi(const QString& content);

    // ---- Link Google Maps (dùng cho ô "Dán link Google Maps" ở trang tạo mã QR Vị trí) ----
    /// Tách tọa độ từ một chuỗi chứa link Google Maps ĐẦY ĐỦ (KHÔNG gọi mạng). Thử theo thứ tự ưu tiên:
    /// `!3d<lat>!4d<lon>` (điểm GHIM thật của địa điểm - chính xác nhất), `@<lat>,<lon>` (tâm khung nhìn),
    /// rồi `q=`/`query=`/`ll=`/`center=<lat>,<lon>`. Thử cả bản đã giải mã phần trăm (%2C -> ,) và tham số
    /// `continue=` (trang đồng ý cookie của Google bọc link thật trong đó). Trả về false nếu không khớp
    /// dạng nào hoặc tọa độ ngoài phạm vi hợp lệ (|vĩ độ| <= 90, |kinh độ| <= 180).
    static bool parseGoogleMapsCoordinates(const QString& text, double* lat, double* lon);
    /// true nếu text là link RÚT GỌN của Google Maps (maps.app.goo.gl/..., goo.gl/maps/..., g.co/kgs/...) -
    /// URL của nó KHÔNG chứa tọa độ, phải mở bằng mạng (đọc đích chuyển hướng) mới ra được.
    static bool isGoogleMapsShortLink(const QString& text);
    /// Chuẩn hóa chuỗi người dùng dán thành QUrl https (thêm https:// nếu thiếu scheme) - dùng với
    /// isGoogleMapsShortLink(). Trả về QUrl rỗng nếu không hợp lệ.
    static QUrl normalizeLinkUrl(const QString& text);
    /// Chỉ cho phép THEO chuyển hướng tới host thuộc Google (google.*, goo.gl, g.co) qua https - không bao
    /// giờ đi theo một chuyển hướng ra miền lạ (link rút gọn bị làm giả không thể dẫn app tới máy chủ khác).
    static bool isAllowedMapsRedirectUrl(const QUrl& url);

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
