#pragma once

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

    /// formatId rỗng = để yt-dlp tự chọn mặc định (video+âm thanh tốt nhất, tự ghép qua ffmpeg).
    void start(const QString& url, const QString& formatId, const QString& destDir);
    void cancel();

signals:
    /// total <= 0 nghĩa là chưa rõ (yt-dlp báo "NA") - UI nên hiện thanh tiến độ không xác định lúc đó.
    void progress(qint64 downloadedBytes, qint64 totalBytes, qint64 speedBytesPerSec, qint64 etaSeconds);
    void finished(bool ok, QString error);

private:
    QProcess* m_process{nullptr};
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrAccum;
    qint64 m_lastPid{0}; // PID của yt-dlp.exe đã khởi chạy gần nhất - cần lưu riêng để dọn tiến trình
                         // ffmpeg.exe con còn sót sau khi buộc dừng (xem cancel()/destructor trong .cpp,
                         // cùng lý do với ScrcpyLauncher::m_lastPid).
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

QStringList buildArguments(const QString& url, const QString& formatId, const QString& destDir,
                           const QString& ffmpegDir);
} // namespace YtDlpDownloadWorkerInternal
