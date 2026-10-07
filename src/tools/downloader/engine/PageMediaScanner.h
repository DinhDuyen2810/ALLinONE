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
/// Phân tích HTML thuần (không gọi mạng) - tách riêng để test được bằng chuỗi HTML mẫu.
QList<MediaLink> extractMediaLinks(const QString& html, const QString& baseUrl);

MediaLink::Type classifyByExtension(const QString& url);
} // namespace PageMediaScannerInternal
