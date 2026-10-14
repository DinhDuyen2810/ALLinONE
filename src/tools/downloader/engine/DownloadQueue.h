#pragma once

#include "../model/ResolvedMedia.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

class DownloaderSettings;
class DownloadHistoryStore;
class FileDownloader;
class DriveViewerCapture;
class LinkResolver;
class YtDlpDownloadWorker;

/// Một mục trong hàng đợi tải hợp nhất.
struct QueueItem
{
    /// Kết quả KIỂM TRA link (chưa tải gì): Waiting = chờ tới lượt, Checking = đang kiểm tra, Ok = ra được tệp để tải,
    /// Err = không ra được tệp nào (lý do ở checkNote).
    enum class Check { Waiting, Checking, Ok, Err };
    enum class State { Idle, Queued, Downloading, Paused, Completed, Failed, Canceled };

    int id{0};
    QString sourceUrl;     ///< Link người dùng dán (hoặc trang đã tìm ra mục này)
    ResolvedMedia media;   ///< Thứ sẽ tải (hợp lệ khi check == Ok)
    Check check{Check::Waiting};
    QString checkNote;     ///< Ghi chú nguồn khi OK; LÝ DO khi Err; tiến trình khi Checking
    QString warning;       ///< Cảnh báo kết quả chưa đầy đủ (vd hồ sơ chỉ lấy được phần công khai)
    State state{State::Idle};
    qint64 total{-1};
    qint64 received{0};
    qint64 speed{0};
    int pagesDone{0};      ///< DriveViewer: số trang đã chụp
    int pagesTotal{0};     ///< DriveViewer: tổng số trang (0 = chưa biết)
    QString error;
    QString destPath;      ///< HttpFile/DriveViewer: biết khi bắt đầu tải; yt-dlp: biết khi xong
    int httpId{-1};        ///< ID trong FileDownloader (HttpFile)
    QString ytBase;        ///< Tên tệp (không đuôi) đã chọn cho yt-dlp
    QString formatSelector; ///< Bộ chọn `-f` riêng của mục (yt-dlp); rỗng = theo chất lượng chung ở DownloaderSettings
    QString formatLabel;    ///< Mô tả định dạng đã chọn riêng (để hiện cho người dùng)
    QDateTime startedAt;
    bool foreign{false};   ///< Mục do nơi khác enqueue thẳng vào FileDownloader (phản chiếu để hiện cùng một hàng đợi)
    bool historyDone{false};

    QString displayName() const;
    QString linkToOpen() const;
    static QString stateLabel(State s);
    static QString checkLabel(Check c);
};

/// HÀNG ĐỢI TẢI HỢP NHẤT của công cụ Downloader:
///  - addSource(): thêm một link (dùng trong kiểm thử; giao diện đưa kết quả nút "Quét" vào bằng addResolved()/addFailed())
///    -> CHƯA TẢI; bộ phân giải (LinkResolver) kiểm tra link và
///    đánh dấu OK (ra được tệp/video để tải) hoặc ERR (kèm lý do). Một link có thể bung thành NHIỀU mục (hồ sơ, danh sách
///    phát, nhiều ảnh trong bài đăng).
///  - startItems(): chỉ các mục OK mới được tải; tệp HTTP đi qua FileDownloader, video/luồng đi qua yt-dlp, tài liệu Google
///    Drive bị chủ tệp tắt "tải xuống" đi qua DriveViewerCapture (chụp từng trang ở chế độ xem -> PDF).
///  - Mục do tab khác đưa thẳng vào FileDownloader cũng hiện ở đây (phản chiếu).
///  - Mỗi mục tải xong/thất bại/bị hủy được ghi vào lịch sử.
class DownloadQueue : public QObject
{
    Q_OBJECT

public:
    DownloadQueue(DownloaderSettings* settings, DownloadHistoryStore* history, FileDownloader* downloader,
                  QObject* parent = nullptr);
    ~DownloadQueue() override;

    /// Thêm một link (CHƯA tải). Trả ID, hoặc -1 nếu không hợp lệ/đã có trong hàng đợi (có tín hiệu message()).
    int addSource(const QString& url);
    QList<int> addSources(const QStringList& urls);
    /// Thêm thẳng một mục đã biết là tải được (vd từ chức năng Quét trang) - check = OK ngay. -1 nếu trùng link tệp.
    int addResolved(const ResolvedMedia& media, const QString& sourceUrl = QString());

    /// Thêm một dòng ERR (link đã quét mà không ra tệp nào) kèm lý do. Đã có dòng ERR của đúng link này thì chỉ cập nhật lý do.
    /// Trả ID dòng đó. "Kiểm tra lại" trên dòng này chạy lại bộ phân giải của hàng đợi.
    int addFailed(const QString& url, const QString& reason);

    /// Bắt đầu tải các mục; mục chưa OK/đã hoàn tất bị bỏ qua. Trả số mục thực sự được bắt đầu/xếp hàng.
    int startItems(const QList<int>& ids);
    bool startItem(int id);
    void pause(int id);
    void cancel(int id);
    void remove(const QList<int>& ids);
    /// Xóa các mục đã Hoàn tất (và, nếu includeFailed, cả mục Thất bại/Đã hủy/ERR). Trả số mục đã xóa.
    int removeFinished(bool includeFailed);
    void recheck(int id);
    /// Chọn định dạng/chất lượng RIÊNG cho một mục yt-dlp (selector rỗng = quay về chất lượng chung). Chỉ có tác dụng với
    /// lượt tải sau đó; mục đang tải/đã xong không đổi.
    void setItemFormat(int id, const QString& selector, const QString& label);
    /// Dừng NGAY mọi tiến trình ngoài do hàng đợi sinh ra (kiểm tra link: yt-dlp/trình duyệt ẩn; tải video: yt-dlp/ffmpeg).
    /// Gọi khi đóng cửa sổ/thoát ứng dụng - nếu không các tiến trình này mồ côi chạy ngầm (xem core/WinProcessTree.h).
    /// Tải tệp HTTP (thuần QNetworkAccessManager) không có tiến trình ngoài nên không bị đụng tới.
    void stopProcesses();

    const QueueItem* item(int id) const;
    /// ID mục OK đã có trong hàng đợi với đúng link tệp này, hoặc -1.
    int idOfMediaUrl(const QString& url) const { return findItemByMediaUrl(url); }
    QList<int> allIds() const { return m_order; }
    int resolvingCount() const { return m_resolvers.size() + m_resolveQueue.size(); }

    /// CHỈ cho kiểm thử: cho phép mục DriveViewer trỏ tới máy chủ cục bộ thay vì chỉ drive.google.com.
    void setAllowAnyViewerHost(bool allow) { m_allowAnyViewerHost = allow; }

signals:
    void itemAdded(int id);
    void itemUpdated(int id);
    void itemRemoved(int id);
    void itemFinished(int id, bool success);
    /// Thông báo ngắn cho thanh trạng thái giao diện.
    void message(QString text);

private slots:
    void onHttpAdded(int httpId);
    void onHttpUpdated(int httpId);

private:
    void pumpResolve();
    void onResolved(int id, const ResolveResult& result, QList<int>* createdIds = nullptr);
    bool startPhotoCheck(int id);
    void pumpPhotoChecks();
    void launchPhotoCheck(int id);
    void onPhotoChecked(int id, const ResolveResult& result);
    void insertItem(const QueueItem& item, int afterId);
    bool startHttp(int id);
    void queueDrive(int id);
    void pumpDrive();
    void startDriveWorker(int id);
    void onDriveFinished(int id, DriveViewerCapture* worker, bool ok, const QString& path, const QString& message);
    void queueYt(int id);
    void pumpYt();
    void startYtWorker(int id);
    void onYtFinished(int id, YtDlpDownloadWorker* worker, bool ok, const QString& error);
    void syncFromHttp(int httpId, bool emitSignal);
    void finalize(int id, bool success);
    void recordHistory(QueueItem& item, bool ok);
    QString chooseHttpDestPath(const QueueItem& item) const;
    QString uniqueYtBase(const QueueItem& item) const;
    int findItemByMediaUrl(const QString& url) const;
public:
    /// Xóa tệp tạm (.part/.ytdl/.part-FragN/.temp) mà yt-dlp để lại cho tiền tố `base` trong `dir` - gọi khi tải
    /// yt-dlp bị hủy/thất bại. Công khai để kiểm thử.
    static void removeYtLeftovers(const QString& dir, const QString& base);
private:

    DownloaderSettings* m_settings{nullptr};
    DownloadHistoryStore* m_history{nullptr};
    FileDownloader* m_downloader{nullptr};

    QHash<int, QueueItem> m_items;
    QList<int> m_order;
    int m_nextId{1};

    QHash<int, LinkResolver*> m_resolvers; ///< id -> bộ phân giải đang chạy
    QHash<int, LinkResolver*> m_photoChecks; ///< id -> kiểm tra "bài ảnh hay video" ngay trước khi tải (hồ sơ TikTok)
    QList<int> m_photoQueue;                 ///< mục chờ tới lượt kiểm tra (tối đa kMaxPhotoChecks chạy cùng lúc)
    QList<int> m_resolveQueue;
    QHash<int, int> m_httpToItem;
    bool m_enqueueingOwn{false};
    QHash<int, YtDlpDownloadWorker*> m_ytActive;
    QList<int> m_ytQueue;
    QHash<int, DriveViewerCapture*> m_driveActive; ///< id -> luồng chụp trang Drive đang chạy (tối đa 1: mỗi luồng là một trình duyệt ẩn)
    QList<int> m_driveQueue;
    bool m_allowAnyViewerHost{false};
};
