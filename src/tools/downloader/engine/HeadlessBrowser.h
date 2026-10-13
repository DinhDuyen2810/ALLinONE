#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <memory>

class QTemporaryDir;
class QTimer;

/// Chạy Microsoft Edge (hoặc Google Chrome) KHÔNG GIAO DIỆN để dựng một trang như trình duyệt thật rồi lấy về:
///  - DOM sau khi JavaScript chạy (`--dump-dom`): ảnh/video do JS chèn vào mới thấy ở đây;
///  - tệp NetLog (`--log-net-log`): MỌI yêu cầu mạng của trang, kể cả luồng .m3u8/.mp4 do trình phát (JW Player...)
///    tự gọi - đúng thứ mà thanh DevTools > Network hiện ra, bất kể trang có cấm chuột phải/sao chép link hay không.
///
/// Dùng khi yt-dlp và quét HTML tĩnh đều không ra gì. Không cần QtWebEngine (không có cho Qt MinGW) vì Edge đã có
/// sẵn trên Windows 10/11. Hồ sơ trình duyệt là thư mục TẠM, xóa sau mỗi lần chạy (không đụng cookie/đăng nhập thật
/// của người dùng). Mọi lần start() LUÔN kết thúc bằng đúng một tín hiệu finished().
class HeadlessBrowser : public QObject
{
    Q_OBJECT

public:
    explicit HeadlessBrowser(QObject* parent = nullptr);
    ~HeadlessBrowser() override;

    /// Đường dẫn Edge rồi Chrome; rỗng nếu máy không có.
    static QString findBrowserExecutable();

    bool isRunning() const;
    /// `timeoutMs`: quá hạn thì buộc dừng và báo lỗi (Edge sinh nhiều tiến trình con - dọn cả cây).
    void start(const QString& url, int timeoutMs = 70000);
    void cancel();

signals:
    void finished(bool ok, QString dom, QByteArray netLog, QString error);

private:
    void emitOnce(bool ok, const QString& error);
    void killTree();

    QProcess* m_process{nullptr};
    QTimer* m_timeout{nullptr};
    std::unique_ptr<QTemporaryDir> m_tmp;
    QByteArray m_stdout;
    qint64 m_pid{0};
    bool m_canceled{false};
    bool m_timedOut{false};
    bool m_emitted{false};
};

namespace HeadlessBrowserInternal
{
/// Đối số dòng lệnh - thuần, test được. `url` PHẢI là http(s) (nơi gọi đã kiểm); đặt SAU cùng nên không bao giờ
/// bị hiểu là một tùy chọn.
QStringList buildArguments(const QString& url, const QString& profileDir, const QString& netLogPath);
QStringList candidatePaths();
} // namespace HeadlessBrowserInternal
