#pragma once

#include <QString>
#include <QStringList>
#include <QThread>
#include <atomic>

/// Lấy nội dung một tài liệu Google Drive mà CHỦ TỆP ĐÃ TẮT "tải xuống" (chế độ chỉ xem) rồi lưu thành PDF.
///
/// Cơ chế (đã dùng làm bằng chứng cho báo cáo về bảo vệ phía máy khách): để HIỂN THỊ tài liệu, Drive buộc phải gửi từng
/// trang dưới dạng ảnh (`<img src="blob:...">`) về trình duyệt của người xem. Lớp này mở trang xem (`/file/d/<ID>/view`)
/// trong Edge/Chrome KHÔNG GIAO DIỆN với hồ sơ tạm (không đụng cookie/đăng nhập thật), điều khiển bằng Chrome DevTools
/// Protocol: cuộn từ đầu tới cuối, mỗi trang nạp xong thì vẽ ảnh lên canvas lấy JPEG, rồi ghép các JPEG thành PDF
/// (ImagePdf). Tức là chỉ chụp lại thứ trình duyệt của người xem ĐÃ được Drive gửi tới - không đăng nhập, không dùng
/// cookie, không vượt kiểm soát truy cập nào; link phải mở được khi không đăng nhập ("Bất kỳ ai có đường liên kết").
///
/// Kết quả là PDF gồm các ẢNH trang (không có lớp chữ chọn/tìm kiếm được; độ nét theo ảnh Drive gửi về).
///
/// Chạy trên QThread riêng (các thao tác CDP là CHẶN); hủy được giữa chừng qua requestCancel() - luồng kiểm tra cờ mỗi
/// 200 ms, tự dọn cả cây tiến trình trình duyệt và hồ sơ tạm rồi báo captureFinished(false, "", "Đã hủy."). Nơi sở hữu
/// PHẢI requestCancel() + wait() trước khi hủy đối tượng (hoặc để luồng tự xóa: connect finished -> deleteLater).
class DriveViewerCapture : public QThread
{
    Q_OBJECT

public:
    struct Params
    {
        QString url;          ///< https://drive.google.com/file/d/<ID>/view
        QString outputPath;   ///< PDF kết quả (thư mục cha được tạo nếu thiếu; ghi nguyên tử qua QSaveFile)
        bool showWindow{false}; ///< Hiện cửa sổ trình duyệt (chẩn đoán) thay vì chạy ẩn
        double scale{2.0};    ///< --force-device-scale-factor: Drive xin ảnh trang theo kích thước/độ phân giải khung xem
        int timeoutMs{15 * 60 * 1000};
        /// CHỈ cho kiểm thử với máy chủ cục bộ: nhận mọi URL http(s). Mặc định chỉ nhận https://drive.google.com/.
        bool allowAnyHttpUrl{false};
    };

    explicit DriveViewerCapture(QObject* parent = nullptr);
    ~DriveViewerCapture() override;

    /// Gọi TRƯỚC start() cho mỗi lần chụp (đồng thời xóa cờ hủy của lần trước).
    void setParams(const Params& params);
    /// An toàn gọi từ luồng khác.
    void requestCancel() { m_cancel = true; }
    bool wasCanceled() const { return m_canceledByUser; }

signals:
    /// Số trang đã chụp / tổng số trang (tổng = 0 khi chưa biết).
    void pageProgress(int captured, int total);
    /// ok: `path` = PDF đã lưu; `message` có thể là CẢNH BÁO (vd thiếu vài trang). !ok: `message` = lý do đọc được.
    /// Đổi tên khỏi `finished` - trùng tín hiệu của QThread.
    void captureFinished(bool ok, QString path, QString message);

protected:
    void run() override;

private:
    bool runCapture(QString* path, QString* message, QString* error);

    Params m_params;
    std::atomic_bool m_cancel{false};
    std::atomic_bool m_canceledByUser{false};
};

namespace DriveViewerCaptureInternal
{
/// Số cổng DevTools trình duyệt đã ghi vào `<profileDir>/DevToolsActivePort` (dòng đầu) khi chạy với
/// --remote-debugging-port=0; 0 nếu chưa có/không đọc được.
quint16 readDevToolsPort(const QString& profileDir);
/// Chỉ nhận https://drive.google.com/file/d/<ID>/view (hoặc mọi http/https nếu allowAny). Trả ID tệp (rỗng nếu allowAny và
/// URL không phải Drive) qua `fileId`.
bool isAllowedUrl(const QString& url, bool allowAny, QString* fileId = nullptr);

/// Đối số dòng lệnh Edge/Chrome. `url` đặt SAU CÙNG nên không bao giờ bị hiểu là tùy chọn. Cổng DevTools chỉ mở trên
/// 127.0.0.1. Hồ sơ là thư mục tạm riêng.
QStringList buildArguments(quint16 port, const QString& profileDir, const QString& url, bool showWindow, double scale);

/// Mã JavaScript chạy TRONG trang xem: định nghĩa `window.__dx` với status()/top()/step(). Thuần chuỗi, test được.
QString injectedScript();
} // namespace DriveViewerCaptureInternal
