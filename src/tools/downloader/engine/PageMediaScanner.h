#pragma once

#include "../model/MediaLink.h"

#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/// Quét một trang web: tải HTML (thuần Qt Network, không cần trình duyệt) rồi tìm các liên kết ảnh/
/// video/âm thanh/tài liệu trong mã nguồn trang (thẻ <img>/<video>/<audio>/<source> và <a href=...> có
/// phần mở rộng tài liệu/lưu trữ đã biết). Phân giải mọi liên kết tương đối thành URL tuyệt đối.
///
/// GIỚI HẠN THẬT: đây là phân tích HTML TĨNH (nội dung trang NHƯ SERVER TRẢ VỀ ban đầu) - không chạy
/// JavaScript, nên các trang web hiện đại tải ảnh/video qua JS sau khi trang đã hiển thị (lazy-load,
/// ứng dụng một trang/SPA) có thể KHÔNG được tìm thấy đầy đủ. Dự án này không có QtWebEngine khả dụng
/// trong bản Qt MinGW đang dùng (Qt chỉ phát hành WebEngine cho MSVC trên Windows) nên không thể nhúng
/// trình duyệt thật để render JS - đã xác nhận qua việc kiểm tra thư mục cài đặt Qt.
///
/// Mọi lần scan() LUÔN kết thúc bằng đúng một tín hiệu result() hoặc errorOccurred() (URL không hợp lệ,
/// không phải trang HTML, kết nối treo quá thời gian chờ...) - giao diện khóa nút "Quét" dựa vào đó.
class PageMediaScanner : public QObject
{
    Q_OBJECT

public:
    explicit PageMediaScanner(QObject* parent = nullptr);

    void scan(const QString& pageUrl);

signals:
    void result(QList<MediaLink> links);
    void errorOccurred(QString message);

private:
    QNetworkAccessManager* m_nam{nullptr};
    QNetworkReply* m_activeReply{nullptr};
};

namespace PageMediaScannerInternal
{
/// Chỉ đọc tối đa ngần này byte của trang rồi phân tích phần đã có - trước đây đọc TOÀN BỘ phản hồi vào
/// bộ nhớ, dán nhầm link một tệp vài GB vào ô "Quét trang" là ngốn hết RAM.
inline constexpr qint64 kMaxPageBytes = 10 * 1024 * 1024;

/// Phân tích HTML thuần (không gọi mạng) - tách riêng để test được bằng chuỗi HTML mẫu. Chỉ trả liên
/// kết http/https (bỏ data:, file:, javascript:...); giải mã thực thể HTML trong URL (&amp; -> &).
QList<MediaLink> extractMediaLinks(const QString& html, const QString& baseUrl);

MediaLink::Type classifyByExtension(const QString& url);

/// Giải mã thực thể HTML trong giá trị thuộc tính: &amp; &lt; &gt; &quot; &apos; &nbsp; và dạng số
/// &#38; / &#x26;. Thực thể không biết được giữ nguyên. Thuần chuỗi.
QString decodeHtmlEntities(const QString& text);

/// Content-Type có phải loại phân tích được như HTML không (text/*, *html*, *xml*). Rỗng (server không
/// gửi) coi là có - cứ thử phân tích.
bool isHtmlContentType(const QString& contentType);
} // namespace PageMediaScannerInternal
