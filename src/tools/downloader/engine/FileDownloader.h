#pragma once

#include "../model/DownloadItem.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

/// Tải trực tiếp một hoặc nhiều tệp qua HTTP(S) - thuần Qt Network (QNetworkAccessManager/QNetworkReply),
/// chạy KHÔNG CẦN QThread vì bản thân đã bất đồng bộ qua tín hiệu Qt (đúng mẫu đã dùng cho
/// PublicIpChecker/SpeedTestRunner - QNetworkReply tự xử lý I/O ngầm, không chặn luồng giao diện).
///
/// Dữ liệu được ghi vào `<destPath>.part` và CHỈ đổi tên thành destPath khi tải xong trọn vẹn - một tệp
/// mang tên cuối cùng luôn là tệp hoàn chỉnh (trước đây ghi thẳng vào destPath: thoát ứng dụng/mất mạng
/// giữa chừng để lại tệp cụt trông y như đã tải xong).
///
/// TIẾP TỤC tải dở (tạm dừng rồi tiếp tục, hoặc thử lại sau lỗi, trong cùng phiên chạy): gửi `Range` kèm
/// `If-Range: <ETag/Last-Modified nhận ở lần trước>` - server chỉ trả 206 nếu tệp CHƯA đổi, còn đổi rồi
/// thì trả 200 kèm toàn bộ tệp mới và ta tải lại từ đầu (trước đây gửi Range trần, tệp trên server đổi
/// giữa hai lần là ghép ra tệp hỏng). Lần trước server không cho ETag/Last-Modified nào thì không có gì
/// bảo đảm - tải lại từ đầu.
///
/// Chỉ nhận URL http/https (xem FileDownloaderInternal::isHttpUrl).
class FileDownloader : public QObject
{
    Q_OBJECT

public:
    explicit FileDownloader(QObject* parent = nullptr);
    ~FileDownloader() override;

    /// Thêm một tệp vào hàng đợi (chưa tải ngay - gọi start() để bắt đầu/tiếp tục). Trả về ID duy nhất
    /// để theo dõi/điều khiển mục này về sau. Tải xong, tệp hoàn chỉnh nằm ĐÚNG tại destPath (ghi đè nếu
    /// lúc đó đã có tệp cùng tên).
    int enqueue(const QString& url, const QString& destPath);

    void start(int id);
    void pause(int id);
    void cancel(int id);

    /// Số lượng tải đồng thời tối đa (mặc định 3) - các mục còn lại trong hàng đợi tự động bắt đầu khi
    /// có "chỗ trống" (một tải khác hoàn tất/thất bại/bị hủy).
    void setMaxConcurrent(int n) { m_maxConcurrent = n; }
    void startAllQueued();

    const DownloadItem* item(int id) const;
    QList<int> allIds() const { return m_order; }

    /// Đã có mục nào trong hàng đợi (ở bất kỳ trạng thái nào) nhắm tới đúng đường dẫn đích này chưa -
    /// nơi chọn tên tệp phải hỏi hàm này chứ không chỉ QFile::exists(): mục còn đang CHỜ (quá số tải
    /// đồng thời) chưa tạo tệp nào trên đĩa, hai mục trùng tên từng nhận cùng một đường dẫn và ghi đè/nối
    /// vào tệp của nhau.
    bool isDestPathInUse(const QString& destPath) const;

signals:
    /// Một mục vừa được enqueue() - giao diện hàng đợi nghe tín hiệu này để hiện MỌI mục, dù tab nào thêm.
    void itemAdded(int id);
    void itemUpdated(int id);
    void itemFinished(int id, bool success);

private:
    void startNetworkRequest(int id);
    void tryStartNextQueued();
    void failItem(int id, const QString& error);

    QNetworkAccessManager* m_nam{nullptr};
    QHash<int, DownloadItem> m_items;
    QHash<int, QNetworkReply*> m_activeReplies;
    QHash<int, QByteArray> m_validators; ///< ETag mạnh/Last-Modified của phần đang nằm trong tệp .part
    QList<int> m_order;
    int m_nextId{1};
    int m_maxConcurrent{3};
    bool m_shuttingDown{false};
};

namespace FileDownloaderInternal
{
/// Hậu tố của tệp đang tải dở (`<destPath>.part`).
inline const QString kPartSuffix = QStringLiteral(".part");

/// URL hợp lệ, scheme http/https, có tên máy chủ. Mọi scheme khác (file:, ftp:, data:, javascript:...)
/// bị từ chối - QNetworkAccessManager sẵn lòng "tải" cả file:// (tức chép một tệp cục bộ bất kỳ).
bool isHttpUrl(const QString& url);

/// Làm sạch một tên tệp lấy từ nguồn KHÔNG tin cậy (URL, trang web) để ghép vào thư mục lưu: bỏ mọi
/// thành phần thư mục (kể cả "\" - `QUrl::fileName()` giải mã "%5C" thành "\" và "..%5C..%5Cx.bat" từng
/// thoát được khỏi thư mục lưu), ký tự cấm trên Windows (\ / : * ? " < > | và ký tự điều khiển; ':' còn mở
/// alternate data stream), ký tự đảo chiều chữ (giả mạo phần mở rộng), dấu chấm/khoảng trắng ở cuối, tên
/// thiết bị dành riêng (CON, PRN, AUX, NUL, COM1-9, LPT1-9), và cắt còn tối đa 150 ký tự (giữ phần mở
/// rộng). Trả chuỗi RỖNG nếu không còn gì dùng được (nơi gọi tự đặt tên mặc định). Thuần chuỗi.
QString sanitizeFileName(const QString& rawName);

/// Tên tệp (đã làm sạch) suy ra từ phần đường dẫn của URL; rỗng nếu URL không có tên tệp dùng được.
QString fileNameFromUrl(const QString& url);

/// `<folder>/<fileName>`, tự thêm " (1)", " (2)"... trước phần mở rộng cho tới khi `isTaken` trả false.
/// fileName được làm sạch lại ở đây (rỗng -> "tai-ve") nên kết quả LUÔN nằm ngay trong `folder`.
QString uniqueDestPath(const QString& folder, const QString& fileName,
                       const std::function<bool(const QString&)>& isTaken);

/// Phân tích "Content-Range: bytes <start>-<end>/<total|*>". total = -1 nếu là "*".
bool parseContentRange(const QByteArray& header, qint64* start, qint64* total);

/// Giá trị dùng cho If-Range ở lần tiếp tục sau: ETag MẠNH nếu có (ETag yếu "W/..." không được phép
/// dùng với If-Range), không thì Last-Modified, không có cả hai thì rỗng (= không tiếp tục được).
QByteArray pickResumeValidator(const QByteArray& etag, const QByteArray& lastModified);
} // namespace FileDownloaderInternal
