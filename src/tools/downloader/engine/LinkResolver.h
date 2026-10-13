#pragma once

#include "../model/ResolvedMedia.h"

#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class HeadlessBrowser;

/// Biến MỘT địa chỉ người dùng dán vào thành danh sách thứ TẢI ĐƯỢC (hoặc báo rõ vì sao không ra) - bất đồng bộ,
/// mọi lần resolve() LUÔN kết thúc bằng đúng một tín hiệu finished().
///
/// Các bậc, dừng ở bậc đầu tiên ra kết quả (logic thuần nằm ở LinkResolverCore, test được không cần mạng):
///  1. Google Docs/Sheets/Slides công khai -> xuất PDF/XLSX/PPTX; Google Drive -> tải trực tiếp (xử lý trang "xác
///     nhận tải", nhận ra "chủ sở hữu không cho tải"/vượt hạn mức).
///  2. Một yêu cầu GET: nếu server trả TỆP (không phải HTML) -> xong. Lỗi 404 -> báo ngay.
///  3. yt-dlp (`--dump-single-json --flat-playlist`): video nền tảng (Facebook/TikTok/YouTube/...), luồng HLS và cả
///     HỒ SƠ/KÊNH/DANH SÁCH PHÁT (nhiều mục). Bỏ qua với bài đăng ẢNH mà yt-dlp không hỗ trợ.
///  4. Quét HTML sâu: thẻ, meta, CHUỖI THÔ trong script (link .m3u8/.mp4 của trình phát JavaScript), rồi iframe nhúng.
///  5. Trình duyệt ẩn (Edge/Chrome): dựng trang thật, đọc DOM + toàn bộ yêu cầu mạng - bắt được cả trang cấm chuột
///     phải và ảnh/video do JavaScript chèn.
class LinkResolver : public QObject
{
    Q_OBJECT

public:
    struct Options
    {
        int maxItems{100};             ///< Giới hạn số mục khi là hồ sơ/danh sách phát
        bool useYtDlp{true};           ///< Cho phép bậc 3 (tắt trong test để không phụ thuộc yt-dlp.exe)
        bool useBrowser{true};         ///< Cho phép bậc 5 (cần Edge/Chrome trên máy)
        QStringList ytDlpExtraArgs;    ///< Đối số thêm cho yt-dlp (cookie...)
        int ytDlpTimeoutMs{150000};
        int browserTimeoutMs{70000};
    };

    explicit LinkResolver(QObject* parent = nullptr);
    ~LinkResolver() override;

    void setOptions(const Options& options) { m_opts = options; }
    void resolve(const QString& url);
    void cancel();
    bool isRunning() const { return m_running; }

signals:
    void progress(QString message);
    void finished(ResolveResult result);

private:
    struct Fetch
    {
        int status{0};
        QString contentType;
        QString finalUrl;
        qint64 length{-1};
        QByteArray disposition;
        QString html;
        QString error;      ///< Lỗi mạng (không có phản hồi HTTP)
        bool isFile{false}; ///< 2xx và không phải HTML (hoặc có Content-Disposition: attachment)
    };

    void fetch(const QString& url, const QString& referer, std::function<void(const Fetch&)> done);

    void stageGoogleDocs(const QString& exportUrl, const QString& ext);
    void stageDrive(const QString& fileId);
    void stageProbe();
    void stagePage();
    void stageYtDlp();
    void stageStatic();
    void stageFrames(int index);
    void stageBrowser();

    ResolvedMedia fileItem(const QString& url, const Fetch& f, const QString& referer) const;
    void finishOk(const QList<ResolvedMedia>& items, bool collection = false, bool truncated = false);
    void finishError(const QString& message);
    QString bestFailureMessage() const;
    void resetState();

    Options m_opts;
    QNetworkAccessManager* m_nam{nullptr};
    QNetworkReply* m_reply{nullptr};
    QProcess* m_ytProcess{nullptr};
    QTimer* m_ytTimeout{nullptr};
    HeadlessBrowser* m_browser{nullptr};

    QString m_url;
    QString m_finalUrl;
    QString m_html;
    QString m_pageTitle;
    QStringList m_iframes;
    int m_pageStatus{0};
    bool m_challenge{false};
    bool m_loginWall{false};
    QString m_ytError;
    bool m_ytUnsupported{false};
    QString m_blockMessage;   ///< Drive: lý do cụ thể không tải được
    QString m_browserError;
    QString m_warning;        ///< Chuyển vào ResolveResult::warning
    int m_ytAttempts{0};
    bool m_running{false};
    bool m_done{true};
    bool m_canceled{false};
};
