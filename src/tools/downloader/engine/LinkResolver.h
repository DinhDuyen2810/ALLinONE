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
///     nhận tải", nhận ra "chủ sở hữu không cho tải"/vượt hạn mức). Chủ tệp tắt "tải xuống" -> nếu bật trình duyệt ẩn thì
///     trả mục DriveViewer (chụp từng trang ở chế độ xem rồi ghép PDF - xem DriveViewerCapture).
///  2. Một yêu cầu GET: nếu server trả TỆP (không phải HTML) -> xong. Lỗi 404 -> báo ngay.
///  3. yt-dlp (`--dump-single-json --flat-playlist`): video nền tảng (Facebook/TikTok/YouTube/...), luồng HLS và cả
///     HỒ SƠ/KÊNH/DANH SÁCH PHÁT (nhiều mục). Bỏ qua với bài đăng ẢNH mà yt-dlp không hỗ trợ.
///  4. Quét HTML sâu: thẻ, meta, CHUỖI THÔ trong script (link .m3u8/.mp4 của trình phát JavaScript), rồi iframe nhúng.
///  5. Trình duyệt ẩn (Edge/Chrome): dựng trang thật, đọc DOM + toàn bộ yêu cầu mạng - bắt được cả trang cấm chuột
///     phải và ảnh/video do JavaScript chèn. Lần đầu lỗi thì thử lại bằng chế độ ẩn cũ.
///
/// HỒ SƠ (Facebook/TikTok/Instagram): trình duyệt ẩn đọc danh sách bài đăng trong trang rồi QUÉT TỪNG BÀI - bài video giao
/// cho yt-dlp, bài ảnh được dựng riêng (bộ phân giải con) để lấy ảnh ĐẦY ĐỦ chứ không phải ảnh thu nhỏ trên trang hồ sơ.
/// Hồ sơ TikTok: yt-dlp liệt kê bài (kể cả bài ảnh dưới dạng /video/...), trình duyệt ẩn phân biệt bài ảnh/video.
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
        QString cookieFile;            ///< cookies.txt cho các yêu cầu HTTP của chính bộ phân giải (vd Google Drive có quyền)
        bool expandPosts{true};        ///< Hồ sơ: quét từng bài đăng (bộ phân giải con); tắt cho chính bộ phân giải con
        int maxParallelChildren{3};    ///< Số bài ảnh quét cùng lúc (mỗi bài một trình duyệt ẩn)
        /// CHỈ cho kiểm thử: thay "https://drive.google.com" + "https://drive.usercontent.google.com" bằng máy chủ cục bộ
        /// (khi đó <base>/download?id=... là tải trực tiếp, <base>/file/d/<ID>/view là trang xem).
        QString driveBaseOverride;
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
    void stageDriveViewer(const QString& fileId, const QString& blockMessage);
    QString driveDirectUrl(const QString& fileId) const;
    QString driveViewUrl(const QString& fileId) const;
    void stageProbe();
    void stagePage();
    void stageYtDlp();
    void stageStatic();
    void stageFrames(int index);
    void stageBrowser();
    void startBrowser(const QString& url);
    void onBrowserFinished(bool ok, QString dom, QByteArray netLog, QString error);
    void onPageDom(const QString& dom, const QByteArray& netLog);
    void refineTikTokCollection(const ResolveResult& collection);
    void onRefineDom(const QString& dom);

    /// Quét từng bài đăng của hồ sơ: mỗi khe là MỘT mục có sẵn (video) hoặc MỘT bài ảnh cần bộ phân giải con dựng ra ảnh.
    struct Slot
    {
        ResolvedMedia fixed;           ///< Mục đã biết (bài video)
        bool isPhoto{false};
        QString photoUrl;
        bool started{false};
        bool done{false};
        QList<ResolvedMedia> children; ///< Ảnh lấy từ bài này
        QString error;
    };
    void startExpansion(const QList<Slot>& plan, bool collection, bool truncated, const QList<ResolvedMedia>& fallback);
    void pumpSlots();
    void finishExpansion();
    QString workUrl() const { return m_workUrl.isEmpty() ? m_url : m_workUrl; }
    void logOutcome(const QString& text) const;

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
    QString m_cookieStamp;
    QString m_workUrl;        ///< Địa chỉ thực sự được quét (khác m_url khi bài /video/ TikTok hóa ra là bài ảnh /photo/)
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
    int m_browserAttempt{0};
    QString m_browserFirstError;
    bool m_browserRefine{false};          ///< Lượt trình duyệt đang chạy là để phân loại bài của hồ sơ TikTok
    ResolveResult m_pendingCollection;    ///< Danh sách yt-dlp chờ phân loại bài ảnh/video
    QList<Slot> m_slots;
    QList<LinkResolver*> m_children;
    bool m_expCollection{false};
    bool m_expTruncated{false};
    QList<ResolvedMedia> m_expFallback;
    bool m_running{false};
    bool m_done{true};
    bool m_canceled{false};
};
