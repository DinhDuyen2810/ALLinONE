#include "DownloadQueue.h"

#include "../DownloadHistoryStore.h"
#include "../DownloaderSettings.h"
#include "DriveViewerCapture.h"
#include "FileDownloader.h"
#include "LinkResolver.h"
#include "LinkResolverCore.h"
#include "YtDlpDownloadWorker.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <algorithm>

namespace
{
constexpr int kMaxResolvers = 2; ///< Mỗi bộ phân giải có thể bật yt-dlp/Edge ẩn - không chạy quá nhiều cùng lúc
constexpr int kMaxYtConcurrent = 2;
constexpr int kMaxDriveConcurrent = 1; ///< Mỗi lượt chụp Drive là một trình duyệt ẩn chạy vài phút - làm lần lượt

QueueItem::State stateFromHttp(DownloadItem::Status s)
{
    switch (s)
    {
        case DownloadItem::Status::Queued: return QueueItem::State::Queued;
        case DownloadItem::Status::Downloading: return QueueItem::State::Downloading;
        case DownloadItem::Status::Paused: return QueueItem::State::Paused;
        case DownloadItem::Status::Completed: return QueueItem::State::Completed;
        case DownloadItem::Status::Failed: return QueueItem::State::Failed;
        case DownloadItem::Status::Canceled: return QueueItem::State::Canceled;
    }
    return QueueItem::State::Idle;
}

bool isTerminal(QueueItem::State s)
{
    return s == QueueItem::State::Completed || s == QueueItem::State::Failed || s == QueueItem::State::Canceled;
}

/// Tệp mà yt-dlp vừa tạo: khớp theo tên gốc đã chọn (không phải .part/.ytdl/.tmp); không có tên gốc thì lấy tệp mới nhất.
QString findYtOutput(const QString& dir, const QString& base, const QDateTime& since)
{
    const QFileInfoList files = QDir(dir).entryInfoList(QDir::Files, QDir::Time);
    for (const QFileInfo& fi : files)
    {
        const QString name = fi.fileName().toLower();
        if (name.endsWith(".part") || name.endsWith(".ytdl") || name.endsWith(".temp") || name.endsWith(".tmp"))
            continue;
        if (!base.isEmpty())
        {
            if (fi.completeBaseName() == base || fi.fileName().startsWith(base + "."))
                return fi.absoluteFilePath();
        }
        else if (fi.lastModified() >= since.addSecs(-2))
            return fi.absoluteFilePath();
    }
    return {};
}
} // namespace

// ---------------------------------------------------------------------------------------------- QueueItem
QString QueueItem::displayName() const
{
    if (!media.title.isEmpty() && media.kind == ResolvedMedia::Kind::YtDlp)
        return media.title;
    if (!media.fileName.isEmpty())
        return media.fileName;
    if (!media.title.isEmpty())
        return media.title;
    return media.url.isEmpty() ? sourceUrl : media.url;
}

QString QueueItem::linkToOpen() const
{
    return media.url.isEmpty() ? sourceUrl : media.url;
}

QString QueueItem::stateLabel(State s)
{
    switch (s)
    {
        case State::Idle: return "Chưa tải";
        case State::Queued: return "Đang chờ";
        case State::Downloading: return "Đang tải";
        case State::Paused: return "Tạm dừng";
        case State::Completed: return "Hoàn tất";
        case State::Failed: return "Thất bại";
        case State::Canceled: return "Đã hủy";
    }
    return {};
}

QString QueueItem::checkLabel(Check c)
{
    switch (c)
    {
        case Check::Waiting: return "Chờ kiểm tra";
        case Check::Checking: return "Đang kiểm tra...";
        case Check::Ok: return "OK";
        case Check::Err: return "ERR";
    }
    return {};
}

// ---------------------------------------------------------------------------------------------- DownloadQueue
DownloadQueue::DownloadQueue(DownloaderSettings* settings, DownloadHistoryStore* history, FileDownloader* downloader,
                             QObject* parent)
    : QObject(parent)
    , m_settings(settings)
    , m_history(history)
    , m_downloader(downloader)
{
    connect(m_downloader, &FileDownloader::itemAdded, this, &DownloadQueue::onHttpAdded);
    connect(m_downloader, &FileDownloader::itemUpdated, this, &DownloadQueue::onHttpUpdated);
}

DownloadQueue::~DownloadQueue()
{
    for (LinkResolver* r : m_resolvers)
    {
        r->disconnect(this);
        r->cancel();
        r->deleteLater();
    }
    m_resolvers.clear();
    for (LinkResolver* r : m_photoChecks)
    {
        r->disconnect(this);
        r->cancel();
        r->deleteLater();
    }
    m_photoChecks.clear();
    for (YtDlpDownloadWorker* w : m_ytActive)
    {
        w->disconnect(this);
        w->cancel();
    }
    m_ytActive.clear();
    for (DriveViewerCapture* w : m_driveActive)
    {
        w->disconnect(this);
        w->requestCancel(); // luồng tự dọn trình duyệt ẩn rồi tự xóa mình (finished -> deleteLater)
        w->wait(15000);
    }
    m_driveActive.clear();
}

const QueueItem* DownloadQueue::item(int id) const
{
    const auto it = m_items.constFind(id);
    return it == m_items.constEnd() ? nullptr : &it.value();
}

int DownloadQueue::findItemByMediaUrl(const QString& url) const
{
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it)
        if (it->check == QueueItem::Check::Ok && it->media.url == url)
            return it.key();
    return -1;
}

void DownloadQueue::insertItem(const QueueItem& item, int afterId)
{
    m_items.insert(item.id, item);
    const int pos = afterId >= 0 ? m_order.indexOf(afterId) : -1;
    if (pos >= 0)
        m_order.insert(pos + 1, item.id);
    else
        m_order << item.id;
    emit itemAdded(item.id);
}

// ---------------------------------------------------------------------------------------------- Thêm mục
int DownloadQueue::addSource(const QString& url)
{
    const QString u = url.trimmed();
    if (!FileDownloaderInternal::isHttpUrl(u))
    {
        emit message("Bỏ qua - không phải link http/https: " + u.left(80));
        return -1;
    }
    for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it)
        if (it->sourceUrl == u)
        {
            emit message("Link này đã có trong hàng đợi: " + u.left(80));
            return -1;
        }
    QueueItem it;
    it.id = m_nextId++;
    it.sourceUrl = u;
    it.media.url = u;
    it.check = QueueItem::Check::Waiting;
    it.checkNote = "Chờ kiểm tra...";
    insertItem(it, -1);
    m_resolveQueue << it.id;
    pumpResolve();
    return it.id;
}

QList<int> DownloadQueue::addSources(const QStringList& urls)
{
    QList<int> ids;
    for (const QString& u : urls)
    {
        const int id = addSource(u);
        if (id >= 0)
            ids << id;
    }
    return ids;
}

int DownloadQueue::addResolved(const ResolvedMedia& media, const QString& sourceUrl)
{
    if (findItemByMediaUrl(media.url) >= 0)
        return -1;
    QueueItem it;
    it.id = m_nextId++;
    it.sourceUrl = sourceUrl.isEmpty() ? media.url : sourceUrl;
    it.media = media;
    it.check = QueueItem::Check::Ok;
    it.checkNote = media.note;
    it.total = media.size;
    insertItem(it, -1);
    return it.id;
}

int DownloadQueue::addFailed(const QString& url, const QString& reason)
{
    const QString u = url.trimmed();
    for (auto it = m_items.begin(); it != m_items.end(); ++it)
        if (it->sourceUrl == u && it->check == QueueItem::Check::Err)
        {
            it->checkNote = reason;
            emit itemUpdated(it.key());
            return it.key();
        }
    QueueItem it;
    it.id = m_nextId++;
    it.sourceUrl = u;
    it.media.url = u;
    it.check = QueueItem::Check::Err;
    it.checkNote = reason;
    insertItem(it, -1);
    return it.id;
}

// ---------------------------------------------------------------------------------------------- Kiểm tra link
void DownloadQueue::pumpResolve()
{
    while (m_resolvers.size() < kMaxResolvers && !m_resolveQueue.isEmpty())
    {
        const int id = m_resolveQueue.takeFirst();
        auto found = m_items.find(id);
        if (found == m_items.end())
            continue;
        found->check = QueueItem::Check::Checking;
        found->checkNote = "Đang kiểm tra...";
        emit itemUpdated(id);

        auto* r = new LinkResolver(this);
        LinkResolver::Options o;
        o.maxItems = m_settings->profileMaxItems();
        o.useBrowser = m_settings->useBrowserFallback();
        o.ytDlpExtraArgs = m_settings->ytDlpExtraArguments();
        o.cookieFile = m_settings->httpCookieFile();
        r->setOptions(o);
        m_resolvers.insert(id, r);
        connect(r, &LinkResolver::progress, this, [this, id](QString text) {
            auto it = m_items.find(id);
            if (it == m_items.end() || it->check != QueueItem::Check::Checking)
                return;
            it->checkNote = text;
            emit itemUpdated(id);
        });
        connect(r, &LinkResolver::finished, this, [this, id, r](ResolveResult res) {
            m_resolvers.remove(id);
            r->deleteLater();
            onResolved(id, res);
            pumpResolve();
        });
        r->resolve(found->sourceUrl);
    }
}

void DownloadQueue::onResolved(int id, const ResolveResult& res, QList<int>* createdIds)
{
    auto found = m_items.find(id);
    if (found == m_items.end())
        return; // mục đã bị xóa trong lúc kiểm tra
    if (!res.ok())
    {
        found->check = QueueItem::Check::Err;
        found->checkNote = res.error;
        found->media.url = found->sourceUrl;
        emit itemUpdated(id);
        return;
    }

    int added = 0, duplicates = 0;
    int afterId = id;
    bool first = true;
    for (const ResolvedMedia& m : res.items)
    {
        if (first)
        {
            found->media = m;
            found->check = QueueItem::Check::Ok;
            found->checkNote = m.note;
            found->warning = res.warning;
            found->total = m.size;
            first = false;
            emit itemUpdated(id);
            if (createdIds)
                *createdIds << id;
            ++added;
            continue;
        }
        if (findItemByMediaUrl(m.url) >= 0)
        {
            ++duplicates;
            continue;
        }
        QueueItem extra;
        extra.id = m_nextId++;
        extra.sourceUrl = found->sourceUrl;
        extra.media = m;
        extra.check = QueueItem::Check::Ok;
        extra.checkNote = m.note;
        extra.warning = res.warning;
        extra.total = m.size;
        insertItem(extra, afterId);
        afterId = extra.id;
        if (createdIds)
            *createdIds << extra.id;
        ++added;
    }
    if (added > 1 || res.isCollection)
    {
        QString text = QString("Tìm thấy %1 mục từ: %2").arg(added).arg(found->sourceUrl.left(70));
        if (res.truncated)
            text += QString(" (đã cắt theo giới hạn %1 mục ở tab Cài đặt)").arg(m_settings->profileMaxItems());
        if (!res.warning.isEmpty())
            text += " - " + res.warning;
        emit message(text);
    }
    Q_UNUSED(duplicates)
}

void DownloadQueue::recheck(int id)
{
    auto found = m_items.find(id);
    if (found == m_items.end() || found->check == QueueItem::Check::Checking || found->check == QueueItem::Check::Waiting)
        return;
    if (found->state == QueueItem::State::Downloading || found->state == QueueItem::State::Queued)
        return;
    found->check = QueueItem::Check::Waiting;
    found->checkNote = "Chờ kiểm tra...";
    found->media.url = found->sourceUrl;
    found->state = QueueItem::State::Idle;
    found->error.clear();
    found->formatSelector.clear(); // kiểm tra lại có thể ra video khác - định dạng đã chọn không còn đúng
    found->formatLabel.clear();
    emit itemUpdated(id);
    m_resolveQueue << id;
    pumpResolve();
}

void DownloadQueue::setItemFormat(int id, const QString& selector, const QString& label)
{
    auto found = m_items.find(id);
    if (found == m_items.end() || found->media.kind != ResolvedMedia::Kind::YtDlp)
        return;
    found->formatSelector = selector;
    found->formatLabel = selector.isEmpty() ? QString() : label;
    emit itemUpdated(id);
}

// ---------------------------------------------------------------------------------------------- Bắt đầu / dừng
int DownloadQueue::startItems(const QList<int>& ids)
{
    int started = 0, skippedErr = 0, skippedWait = 0;
    for (int id : ids)
    {
        const QueueItem* it = item(id);
        if (!it)
            continue;
        if (it->check == QueueItem::Check::Err)
        {
            ++skippedErr;
            continue;
        }
        if (it->check != QueueItem::Check::Ok)
        {
            ++skippedWait;
            continue;
        }
        if (startItem(id))
            ++started;
    }
    QStringList notes;
    if (started > 0)
        notes << QString("Đã bắt đầu/xếp hàng tải %1 mục.").arg(started);
    if (skippedErr > 0)
        notes << QString("Bỏ qua %1 mục ERR (không ra được tệp để tải).").arg(skippedErr);
    if (skippedWait > 0)
        notes << QString("Bỏ qua %1 mục còn đang kiểm tra.").arg(skippedWait);
    if (!notes.isEmpty())
        emit message(notes.join(' '));
    return started;
}

bool DownloadQueue::startItem(int id)
{
    auto found = m_items.find(id);
    if (found == m_items.end() || found->check != QueueItem::Check::Ok)
        return false;
    switch (found->state)
    {
        case QueueItem::State::Downloading:
        case QueueItem::State::Queued:
        case QueueItem::State::Completed:
            return false;
        default:
            break;
    }
    found->error.clear();
    found->historyDone = false;
    if (found->media.kind == ResolvedMedia::Kind::HttpFile)
        return startHttp(id);
    if (found->media.kind == ResolvedMedia::Kind::DriveViewer)
    {
        queueDrive(id);
        return true;
    }
    // Mục của hồ sơ TikTok chưa biết là video hay bài ảnh: yt-dlp tải bài ảnh ra file nhạc mp3 chứ không có ảnh - kiểm tra lại
    // đúng mục này trước khi tải.
    if (found->media.maybePhotoPost)
        return startPhotoCheck(id);
    queueYt(id);
    return true;
}

bool DownloadQueue::startPhotoCheck(int id)
{
    auto found = m_items.find(id);
    if (found == m_items.end() || m_photoChecks.contains(id))
        return false;
    found->state = QueueItem::State::Queued;
    found->received = 0;
    emit itemUpdated(id);

    auto* r = new LinkResolver(this);
    LinkResolver::Options o;
    o.maxItems = 60;
    o.useBrowser = m_settings->useBrowserFallback();
    o.ytDlpExtraArgs = m_settings->ytDlpExtraArguments();
    o.cookieFile = m_settings->httpCookieFile();
    r->setOptions(o);
    m_photoChecks.insert(id, r);
    connect(r, &LinkResolver::finished, this, [this, id, r](ResolveResult res) {
        m_photoChecks.remove(id);
        r->deleteLater();
        onPhotoChecked(id, res);
    });
    r->resolve(found->media.url);
    return true;
}

void DownloadQueue::onPhotoChecked(int id, const ResolveResult& res)
{
    auto found = m_items.find(id);
    if (found == m_items.end())
        return;
    if (res.error == QStringLiteral("Đã hủy."))
    {
        found->state = QueueItem::State::Canceled;
        emit itemUpdated(id);
        finalize(id, false);
        return;
    }
    found->media.maybePhotoPost = false;
    bool allImages = res.ok();
    for (const ResolvedMedia& m : res.items)
        allImages = allImages && m.kind == ResolvedMedia::Kind::HttpFile;
    if (!allImages)
    {
        // Là video (hoặc chưa phân loại được): tải bằng yt-dlp như thường.
        queueYt(id);
        return;
    }
    // Là bài ảnh: mục này (và các mục chèn thêm ngay sau nó) trở thành các ảnh, rồi tải luôn.
    QList<int> ids;
    found->state = QueueItem::State::Idle;
    onResolved(id, res, &ids);
    emit message(QString("Bài đăng ảnh TikTok: tách thành %1 ảnh.").arg(ids.size()));
    for (int newId : ids)
        startItem(newId);
}

QString DownloadQueue::chooseHttpDestPath(const QueueItem& item) const
{
    QString name = FileDownloaderInternal::sanitizeFileName(item.media.fileName);
    if (name.isEmpty())
        name = FileDownloaderInternal::fileNameFromUrl(item.media.url);
    if (name.isEmpty() || !name.contains('.'))
        name = QString("tai-ve-%1").arg(QDateTime::currentMSecsSinceEpoch());
    const QString folder = m_settings->downloadFolder();
    return FileDownloaderInternal::uniqueDestPath(folder, name, [this](const QString& path) {
        if (QFile::exists(path) || m_downloader->isDestPathInUse(path))
            return true;
        for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it) // mục khác trong hàng đợi đã nhắm đích này
            if (QDir::cleanPath(it->destPath).compare(QDir::cleanPath(path), Qt::CaseInsensitive) == 0)
                return true;
        return false;
    });
}

bool DownloadQueue::startHttp(int id)
{
    QueueItem& it = m_items[id];
    m_downloader->setCookieFile(m_settings->httpCookieFile());
    // Đã từng có mục FileDownloader (tạm dừng/lỗi/hủy): tiếp tục/thử lại chính nó.
    if (it.httpId >= 0)
    {
        const DownloadItem* d = m_downloader->item(it.httpId);
        if (d && d->status != DownloadItem::Status::Completed && d->status != DownloadItem::Status::Downloading)
        {
            m_downloader->setMaxConcurrent(m_settings->maxConcurrent());
            m_downloader->start(it.httpId);
            return true;
        }
    }
    it.destPath = chooseHttpDestPath(it);
    QDir().mkpath(QFileInfo(it.destPath).absolutePath());
    m_enqueueingOwn = true;
    const int fid = m_downloader->enqueue(it.media.url, it.destPath, it.media.referer);
    m_enqueueingOwn = false;
    it.httpId = fid;
    it.state = QueueItem::State::Queued;
    m_httpToItem.insert(fid, id);
    emit itemUpdated(id);
    m_downloader->setMaxConcurrent(m_settings->maxConcurrent());
    m_downloader->start(fid);
    return true;
}

void DownloadQueue::pause(int id)
{
    auto found = m_items.find(id);
    if (found == m_items.end())
        return;
    if (found->httpId >= 0)
        m_downloader->pause(found->httpId);
    else if (found->media.kind == ResolvedMedia::Kind::YtDlp)
        emit message("Video/luồng tải bằng yt-dlp không tạm dừng được - hãy dùng \"Hủy\" rồi tải lại.");
    else if (found->media.kind == ResolvedMedia::Kind::DriveViewer)
        emit message("Tài liệu Drive đang chụp từng trang không tạm dừng được - hãy dùng \"Hủy\" rồi tải lại.");
}

void DownloadQueue::stopProcesses()
{
    // Thứ tự quan trọng: hủy các mục ĐANG CHỜ trước, nếu không việc hủy mục đang chạy sẽ kích hoạt mục chờ kế tiếp
    // (pumpResolve/pumpYt gọi lại sau mỗi lần kết thúc) và tiến trình ngoài mới lại được sinh ra.
    const QList<int> waitingResolve = m_resolveQueue;
    m_resolveQueue.clear();
    for (int id : waitingResolve)
    {
        auto it = m_items.find(id);
        if (it == m_items.end())
            continue;
        it->check = QueueItem::Check::Err;
        it->checkNote = "Đã dừng khi đóng cửa sổ - bấm \"Kiểm tra lại\" để thử lại.";
        emit itemUpdated(id);
    }
    const QList<int> waitingYt = m_ytQueue;
    for (int id : waitingYt)
        cancel(id);
    const QList<int> waitingDrive = m_driveQueue;
    for (int id : waitingDrive)
        cancel(id);
    for (int id : m_resolvers.keys())
        cancel(id);
    for (int id : m_photoChecks.keys())
        cancel(id);
    for (int id : m_ytActive.keys())
        cancel(id);
    for (int id : m_driveActive.keys())
        cancel(id);
}

void DownloadQueue::cancel(int id)
{
    auto found = m_items.find(id);
    if (found == m_items.end())
        return;
    // Đang kiểm tra link.
    if (m_resolvers.contains(id))
    {
        m_resolvers.value(id)->cancel(); // phát finished("Đã hủy.") -> onResolved đánh dấu ERR
        return;
    }
    if (m_photoChecks.contains(id))
    {
        m_photoChecks.value(id)->cancel(); // phát finished("Đã hủy.") -> onPhotoChecked
        return;
    }
    if (m_resolveQueue.removeAll(id) > 0)
    {
        found->check = QueueItem::Check::Err;
        found->checkNote = "Đã hủy kiểm tra.";
        emit itemUpdated(id);
        return;
    }
    if (found->httpId >= 0)
    {
        m_downloader->cancel(found->httpId);
        return;
    }
    if (YtDlpDownloadWorker* w = m_ytActive.value(id, nullptr))
    {
        w->cancel(); // finished(false, "Đã hủy.") -> onYtFinished
        return;
    }
    if (m_ytQueue.removeAll(id) > 0)
    {
        found->state = QueueItem::State::Canceled;
        emit itemUpdated(id);
        finalize(id, false);
        return;
    }
    if (DriveViewerCapture* w = m_driveActive.value(id, nullptr))
    {
        w->requestCancel(); // captureFinished(false, "", "Đã hủy.") -> onDriveFinished
        return;
    }
    if (m_driveQueue.removeAll(id) > 0)
    {
        found->state = QueueItem::State::Canceled;
        emit itemUpdated(id);
        finalize(id, false);
    }
}

void DownloadQueue::remove(const QList<int>& ids)
{
    for (int id : ids)
    {
        auto found = m_items.find(id);
        if (found == m_items.end())
            continue;
        if (LinkResolver* r = m_resolvers.take(id))
        {
            r->disconnect(this);
            r->cancel();
            r->deleteLater();
        }
        if (LinkResolver* r = m_photoChecks.take(id))
        {
            r->disconnect(this);
            r->cancel();
            r->deleteLater();
        }
        m_resolveQueue.removeAll(id);
        m_ytQueue.removeAll(id);
        m_driveQueue.removeAll(id);
        if (YtDlpDownloadWorker* w = m_ytActive.take(id))
        {
            w->disconnect(this);
            w->cancel();
            w->deleteLater();
        }
        if (DriveViewerCapture* w = m_driveActive.take(id))
        {
            w->disconnect(this);
            w->requestCancel(); // luồng tự dọn rồi tự xóa mình (finished -> deleteLater)
        }
        if (found->httpId >= 0)
        {
            const DownloadItem* d = m_downloader->item(found->httpId);
            if (d && (d->status == DownloadItem::Status::Downloading || d->status == DownloadItem::Status::Queued ||
                      d->status == DownloadItem::Status::Paused))
                m_downloader->cancel(found->httpId);
            m_httpToItem.remove(found->httpId);
        }
        m_items.erase(found);
        m_order.removeAll(id);
        emit itemRemoved(id);
    }
    pumpResolve();
    pumpYt();
    pumpDrive();
}

int DownloadQueue::removeFinished(bool includeFailed)
{
    QList<int> ids;
    for (int id : m_order)
    {
        const QueueItem* it = item(id);
        if (!it)
            continue;
        const bool done = it->state == QueueItem::State::Completed;
        const bool bad = it->state == QueueItem::State::Failed || it->state == QueueItem::State::Canceled ||
                         it->check == QueueItem::Check::Err;
        if (done || (includeFailed && bad))
            ids << id;
    }
    remove(ids);
    return ids.size();
}

// ---------------------------------------------------------------------------------------------- yt-dlp
void DownloadQueue::queueYt(int id)
{
    QueueItem& it = m_items[id];
    it.state = QueueItem::State::Queued;
    it.received = 0;
    it.speed = 0;
    emit itemUpdated(id);
    if (!m_ytQueue.contains(id))
        m_ytQueue << id;
    pumpYt();
}

void DownloadQueue::pumpYt()
{
    const int limit = qMin(kMaxYtConcurrent, qMax(1, m_settings->maxConcurrent()));
    while (m_ytActive.size() < limit && !m_ytQueue.isEmpty())
    {
        const int id = m_ytQueue.takeFirst();
        if (m_items.contains(id))
            startYtWorker(id);
    }
}

QString DownloadQueue::uniqueYtBase(const QueueItem& item) const
{
    QString base = QFileInfo(FileDownloaderInternal::sanitizeFileName(item.media.fileName)).completeBaseName();
    if (base.isEmpty())
        base = FileDownloaderInternal::sanitizeFileName(item.media.title);
    if (base.isEmpty())
        base = QStringLiteral("video");
    const QString folder = m_settings->downloadFolder();
    auto taken = [&](const QString& b) {
        const QFileInfoList existing = QDir(folder).entryInfoList({b + ".*"}, QDir::Files);
        if (!existing.isEmpty())
            return true;
        for (auto it = m_items.constBegin(); it != m_items.constEnd(); ++it)
            if (it->id != item.id && it->ytBase == b && !isTerminal(it->state))
                return true;
        return false;
    };
    QString candidate = base;
    for (int n = 1; taken(candidate) && n < 1000; ++n)
        candidate = QString("%1 (%2)").arg(base).arg(n);
    return candidate;
}

void DownloadQueue::startYtWorker(int id)
{
    QueueItem& it = m_items[id];
    it.state = QueueItem::State::Downloading;
    it.startedAt = QDateTime::currentDateTime();
    it.ytBase = uniqueYtBase(it);
    it.received = 0;
    it.speed = 0;
    emit itemUpdated(id);

    auto* worker = new YtDlpDownloadWorker(this);
    m_ytActive.insert(id, worker);
    connect(worker, &YtDlpDownloadWorker::progress, this, [this, id](qint64 done, qint64 total, qint64 speed, qint64) {
        auto f = m_items.find(id);
        if (f == m_items.end())
            return;
        f->received = qMax<qint64>(0, done);
        if (total > 0)
            f->total = total;
        f->speed = qMax<qint64>(0, speed);
        emit itemUpdated(id);
    });
    connect(worker, &YtDlpDownloadWorker::finished, this, [this, id, worker](bool ok, QString error) {
        onYtFinished(id, worker, ok, error);
    });

    YtDlpDownloadWorker::Options o;
    o.url = it.media.url;
    o.destDir = m_settings->downloadFolder();
    o.outputBaseName = it.ytBase;
    o.formatSelector = it.formatSelector.isEmpty() ? DownloaderSettings::formatSelectorForQuality(m_settings->videoQuality())
                                                   : it.formatSelector;
    o.extraArgs = m_settings->ytDlpExtraArguments();
    if (!it.media.referer.isEmpty())
        o.extraArgs << "--referer" << it.media.referer;
    QDir().mkpath(o.destDir);
    worker->startWithOptions(o);
}

void DownloadQueue::onYtFinished(int id, YtDlpDownloadWorker* worker, bool ok, const QString& error)
{
    const bool wasCanceled = worker->wasCanceled();
    if (m_ytActive.value(id, nullptr) == worker)
        m_ytActive.remove(id);
    worker->deleteLater();

    auto found = m_items.find(id);
    if (found != m_items.end())
    {
        QueueItem& it = *found;
        it.speed = 0;
        if (ok)
        {
            it.destPath = findYtOutput(m_settings->downloadFolder(), it.ytBase, it.startedAt);
            it.state = QueueItem::State::Completed;
            const qint64 size = it.destPath.isEmpty() ? -1 : QFileInfo(it.destPath).size();
            if (size > 0)
                it.total = it.received = size;
        }
        else if (wasCanceled)
        {
            it.state = QueueItem::State::Canceled;
        }
        else
        {
            it.state = QueueItem::State::Failed;
            it.error = LinkResolverCore::translateYtDlpError(error);
        }
        emit itemUpdated(id);
        finalize(id, ok);
    }
    pumpYt();
}

// ---------------------------------------------------------------------------------------------- Drive (chế độ xem)
void DownloadQueue::queueDrive(int id)
{
    QueueItem& it = m_items[id];
    it.state = QueueItem::State::Queued;
    it.received = 0;
    it.speed = 0;
    it.pagesDone = 0;
    it.pagesTotal = 0;
    emit itemUpdated(id);
    if (!m_driveQueue.contains(id))
        m_driveQueue << id;
    pumpDrive();
}

void DownloadQueue::pumpDrive()
{
    while (m_driveActive.size() < kMaxDriveConcurrent && !m_driveQueue.isEmpty())
    {
        const int id = m_driveQueue.takeFirst();
        if (m_items.contains(id))
            startDriveWorker(id);
    }
}

void DownloadQueue::startDriveWorker(int id)
{
    QueueItem& it = m_items[id];
    it.state = QueueItem::State::Downloading;
    it.startedAt = QDateTime::currentDateTime();
    it.destPath = chooseHttpDestPath(it);
    it.received = 0;
    it.speed = 0;
    it.pagesDone = 0;
    it.pagesTotal = 0;
    emit itemUpdated(id);

    auto* worker = new DriveViewerCapture(); // không cha: hủy QThread đang chạy cùng cha sẽ làm sập chương trình
    connect(worker, &QThread::finished, worker, &QObject::deleteLater);
    m_driveActive.insert(id, worker);
    connect(worker, &DriveViewerCapture::pageProgress, this, [this, id](int done, int total) {
        auto f = m_items.find(id);
        if (f == m_items.end())
            return;
        f->pagesDone = done;
        f->pagesTotal = total;
        emit itemUpdated(id);
    });
    connect(worker, &DriveViewerCapture::captureFinished, this, [this, id, worker](bool ok, QString path, QString msg) {
        onDriveFinished(id, worker, ok, path, msg);
    });

    DriveViewerCapture::Params p;
    p.url = it.media.url;
    p.outputPath = it.destPath;
    p.allowAnyHttpUrl = m_allowAnyViewerHost;
    QDir().mkpath(QFileInfo(it.destPath).absolutePath());
    worker->setParams(p);
    worker->start();
}

void DownloadQueue::onDriveFinished(int id, DriveViewerCapture* worker, bool ok, const QString& path, const QString& msg)
{
    if (m_driveActive.value(id, nullptr) == worker)
        m_driveActive.remove(id);

    auto found = m_items.find(id);
    if (found != m_items.end())
    {
        QueueItem& it = *found;
        it.speed = 0;
        if (ok)
        {
            it.destPath = path;
            it.state = QueueItem::State::Completed;
            const qint64 size = QFileInfo(path).size();
            if (size > 0)
                it.total = it.received = size;
            if (!msg.isEmpty())
            {
                it.warning = msg;
                emit message(msg);
            }
        }
        else if (worker->wasCanceled())
        {
            it.state = QueueItem::State::Canceled;
        }
        else
        {
            it.state = QueueItem::State::Failed;
            it.error = msg;
        }
        emit itemUpdated(id);
        finalize(id, ok);
    }
    pumpDrive();
}

// ---------------------------------------------------------------------------------------------- FileDownloader
void DownloadQueue::onHttpAdded(int httpId)
{
    if (m_enqueueingOwn || m_httpToItem.contains(httpId))
        return;
    const DownloadItem* d = m_downloader->item(httpId);
    if (!d)
        return;
    // Mục do nơi khác (vd kiểm thử/tab khác) enqueue thẳng vào FileDownloader - phản chiếu để hiện cùng hàng đợi.
    QueueItem it;
    it.id = m_nextId++;
    it.sourceUrl = d->url;
    it.media.kind = ResolvedMedia::Kind::HttpFile;
    it.media.url = d->url;
    it.media.fileName = d->fileName();
    it.media.referer = d->referer;
    it.media.mediaType = "Tệp";
    it.check = QueueItem::Check::Ok;
    it.checkNote = "thêm trực tiếp";
    it.httpId = httpId;
    it.foreign = true;
    it.destPath = d->destPath;
    it.state = stateFromHttp(d->status);
    it.total = d->totalBytes;
    m_httpToItem.insert(httpId, it.id);
    insertItem(it, -1);
}

void DownloadQueue::syncFromHttp(int httpId, bool emitSignal)
{
    const int id = m_httpToItem.value(httpId, -1);
    auto found = m_items.find(id);
    const DownloadItem* d = m_downloader->item(httpId);
    if (found == m_items.end() || !d)
        return;
    found->state = stateFromHttp(d->status);
    found->total = d->totalBytes > 0 ? d->totalBytes : found->total;
    found->received = d->receivedBytes;
    found->speed = d->speedBytesPerSec;
    found->error = d->error;
    found->destPath = d->destPath;
    if (emitSignal)
        emit itemUpdated(id);
}

void DownloadQueue::onHttpUpdated(int httpId)
{
    if (!m_httpToItem.contains(httpId))
        return;
    syncFromHttp(httpId, true);
    const int id = m_httpToItem.value(httpId);
    const QueueItem* it = item(id);
    if (it && isTerminal(it->state))
        finalize(id, it->state == QueueItem::State::Completed);
}

void DownloadQueue::finalize(int id, bool success)
{
    auto found = m_items.find(id);
    if (found == m_items.end() || found->historyDone)
        return;
    recordHistory(*found, success);
    emit itemFinished(id, success);
}

void DownloadQueue::recordHistory(QueueItem& it, bool ok)
{
    it.historyDone = true;
    if (!m_history)
        return;
    DownloadHistoryEntry e;
    e.title = it.displayName();
    e.fileName = it.destPath.isEmpty() ? it.media.fileName : QFileInfo(it.destPath).fileName();
    e.path = ok ? it.destPath : QString();
    e.folder = it.destPath.isEmpty() ? m_settings->downloadFolder() : QFileInfo(it.destPath).absolutePath();
    e.sourceUrl = it.sourceUrl;
    e.mediaUrl = it.media.url;
    e.status = QueueItem::stateLabel(it.state);
    e.error = it.error;
    e.size = (ok && !it.destPath.isEmpty() && QFileInfo::exists(it.destPath)) ? QFileInfo(it.destPath).size() : -1;
    e.kind = it.media.kind == ResolvedMedia::Kind::YtDlp ? "yt-dlp"
             : it.media.kind == ResolvedMedia::Kind::DriveViewer ? "drive-viewer"
                                                                  : "http";
    m_history->add(e);
}
