#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QList>
#include <QNetworkCookie>
#include <QString>

class QNetworkAccessManager;

/// Đọc tệp `cookies.txt` (định dạng Netscape - cùng định dạng yt-dlp dùng, xuất từ trình duyệt của CHÍNH người dùng bằng
/// tiện ích "Get cookies.txt") để tải tệp/trang cần đăng nhập mà người dùng CÓ QUYỀN (vd tệp Google Drive họ là chủ sở hữu
/// hoặc người chỉnh sửa). Cookie chỉ được nạp vào bình cookie CỤC BỘ của QNetworkAccessManager: Qt chỉ gửi cookie tới đúng
/// miền/đường dẫn khớp (kể cả sau chuyển hướng), nên không rò sang trang khác. Không chép/ghi cookie ra nơi nào khác.
namespace CookieFile
{
/// Mỗi dòng: `miền \t gồm-miền-con \t đường-dẫn \t chỉ-https \t hết-hạn \t tên \t giá-trị`; tiền tố `#HttpOnly_` ở miền được
/// nhận; dòng trống/chú thích bỏ qua; cookie đã hết hạn bị loại. Dòng hỏng bị bỏ qua (không làm hỏng cả tệp).
QList<QNetworkCookie> parse(const QByteArray& text, const QDateTime& now = QDateTime::currentDateTimeUtc());

/// Đọc tệp (tối đa 5 MB). Lỗi -> danh sách rỗng và `error` (nếu có) được điền.
QList<QNetworkCookie> load(const QString& path, QString* error = nullptr);

/// Thay TOÀN BỘ cookie của `nam` bằng cookie trong tệp (đường dẫn rỗng = xóa hết). Trả số cookie đã nạp.
int applyTo(QNetworkAccessManager* nam, const QString& path);

/// Dấu hiệu thay đổi của tệp (đường dẫn + thời điểm sửa) - nơi gọi so sánh để khỏi đọc lại tệp mỗi lần.
QString stamp(const QString& path);
} // namespace CookieFile
