#pragma once

#include "../model/VideoFormatInfo.h"

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>

/// Tải MỘT video (định dạng/chất lượng đã chọn) qua `yt-dlp.exe`, chạy như tiến trình con (giống
/// ScrcpyLauncher - không chặn luồng giao diện vì QProcess đã bất đồng bộ qua tín hiệu Qt, không cần
/// QThread). Đọc tiến độ qua `--progress-template` (cách yt-dlp CHÍNH THỨC khuyến nghị cho chương trình
/// khác đọc máy - xem README "EMBEDDING YT-DLP": KHÔNG nên tự phân tích thanh tiến độ dạng người đọc vì
/// có thể đổi định dạng giữa các phiên bản) kết hợp `--newline` để mỗi lần cập nhật là MỘT DÒNG riêng.
class YtDlpDownloadWorker : public QObject
{
    Q_OBJECT

public:
    explicit YtDlpDownloadWorker(QObject* parent = nullptr);
    ~YtDlpDownloadWorker() override;

    /// formatSelector rỗng = để yt-dlp tự chọn mặc định (video+âm thanh tốt nhất, tự ghép qua ffmpeg).
    /// Xem YtDlpDownloadWorkerInternal::formatSelectorFor() để dựng bộ chọn từ một định dạng cụ thể.
    /// MỌI lần gọi đều kết thúc bằng đúng một tín hiệu finished() - kể cả khi yt-dlp.exe không khởi chạy
    /// được (trước đây trường hợp đó không phát gì, giao diện kẹt mãi ở "Đang tải").
    void start(const QString& url, const QString& formatSelector, const QString& destDir);
    void cancel();

    bool isRunning() const;
    /// true nếu lượt chạy gần nhất kết thúc do cancel() - để giao diện không báo "Không tải được" như một
    /// lỗi khi chính người dùng bấm Hủy/đóng cửa sổ.
    bool wasCanceled() const { return m_canceled; }

signals:
    /// total <= 0 nghĩa là chưa rõ (yt-dlp báo "NA") - UI nên hiện thanh tiến độ không xác định lúc đó.
    void progress(qint64 downloadedBytes, qint64 totalBytes, qint64 speedBytesPerSec, qint64 etaSeconds);
    void finished(bool ok, QString error);

private:
    void emitFinishedOnce(bool ok, const QString& error);

    QProcess* m_process{nullptr};
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrAccum;
    qint64 m_lastPid{0}; // PID của yt-dlp.exe đã khởi chạy gần nhất - cần lưu riêng để dọn tiến trình
                         // ffmpeg.exe con còn sót sau khi buộc dừng (xem cancel()/destructor trong .cpp,
                         // cùng lý do với ScrcpyLauncher::m_lastPid).
    bool m_canceled{false};
    bool m_finishedEmitted{false};
};

namespace YtDlpDownloadWorkerInternal
{
/// Dòng tiến độ thật dạng "DLPROG|<downloaded>|<total>|<total_estimate>|<speed>|<eta>" (trường "NA" khi
/// yt-dlp chưa biết) - tách riêng để test được bằng chuỗi mẫu, không cần chạy yt-dlp thật.
struct ParsedProgress
{
    bool ok{false};
    qint64 downloadedBytes{-1};
    qint64 totalBytes{-1};
    qint64 speedBytesPerSec{-1};
    qint64 etaSeconds{-1};
};
ParsedProgress parseProgressLine(const QByteArray& line);

/// Đối số dòng lệnh cho lượt tải. Điểm an toàn:
///  - `--ignore-config`: không nạp tệp cấu hình yt-dlp của người dùng/cạnh exe (có thể chứa --exec...).
///  - `--` ngay trước URL: URL không bao giờ bị hiểu là một tùy chọn, dù nó bắt đầu bằng "-".
///  - Ký tự "%" trong thư mục đích được nhân đôi: `-o` là MẪU của yt-dlp, thư mục tên "100% phim" từng làm
///    hỏng mẫu.
QStringList buildArguments(const QString& url, const QString& formatSelector, const QString& destDir,
                           const QString& ffmpegDir);

/// Giá trị cho `-f` ứng với một định dạng người dùng chọn. Định dạng CHỈ CÓ HÌNH (YouTube chất lượng
/// cao hầu hết là vậy) được ghép kèm luồng âm thanh tốt nhất ("<id>+bestaudio/best") - trước đây truyền
/// trần "<id>" nên tải ra video không có tiếng. Định dạng đã có tiếng/chỉ có tiếng giữ nguyên "<id>".
QString formatSelectorFor(const VideoFormatInfo& format);
} // namespace YtDlpDownloadWorkerInternal
