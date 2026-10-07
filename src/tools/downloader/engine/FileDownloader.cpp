#include "FileDownloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <memory>

FileDownloader::FileDownloader(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    // Tính tốc độ mỗi 500ms thay vì mỗi lần readyRead (quá dồn dập, tốc độ tức thời sẽ giật) - lưu mẫu
    // trước đó qua thuộc tính động trên từng QNetworkReply (tránh thêm một QHash riêng chỉ để làm việc này).
    auto* speedTimer = new QTimer(this);
    speedTimer->setInterval(500);
    connect(speedTimer, &QTimer::timeout, this, [this]() {
        for (auto it = m_activeReplies.constBegin(); it != m_activeReplies.constEnd(); ++it)
        {
            const int id = it.key();
            QNetworkReply* reply = it.value();
            if (!m_items.contains(id)) continue;
            auto& item = m_items[id];
            const qint64 prev = reply->property("lastSampleBytes").toLongLong();
            item.speedBytesPerSec = (item.receivedBytes - prev) * 2; // *2 vì chu kỳ 500ms = nửa giây
            reply->setProperty("lastSampleBytes", item.receivedBytes);
            emit itemUpdated(id);
        }
    });
    speedTimer->start();
}

FileDownloader::~FileDownloader()
{
    const auto replies = m_activeReplies.values();
    for (auto* reply : replies)
        reply->abort();
}

int FileDownloader::enqueue(const QString& url, const QString& destPath)
{
    const int id = m_nextId++;
    DownloadItem item;
    item.url = url;
    item.destPath = destPath;
    item.status = DownloadItem::Status::Queued;
    m_items[id] = item;
    m_order << id;
    return id;
}

void FileDownloader::start(int id)
{
    if (!m_items.contains(id))
        return;
    auto& it = m_items[id];
    if (it.status == DownloadItem::Status::Downloading || it.status == DownloadItem::Status::Completed)
        return;

    if (m_activeReplies.size() >= m_maxConcurrent)
    {
        it.status = DownloadItem::Status::Queued;
        emit itemUpdated(id);
        return;
    }
    startNetworkRequest(id);
}

void FileDownloader::startAllQueued()
{
    for (int id : std::as_const(m_order))
    {
        if (!m_items.contains(id))
            continue;
        const auto s = m_items[id].status;
        if (s == DownloadItem::Status::Queued || s == DownloadItem::Status::Paused || s == DownloadItem::Status::Failed)
            start(id);
    }
}

void FileDownloader::tryStartNextQueued()
{
    for (int id : std::as_const(m_order))
    {
        if (m_activeReplies.size() >= m_maxConcurrent)
            break;
        if (m_items.contains(id) && m_items[id].status == DownloadItem::Status::Queued)
            startNetworkRequest(id);
    }
}

void FileDownloader::startNetworkRequest(int id)
{
    auto& it = m_items[id];

    const QFileInfo destInfo(it.destPath);
    QDir().mkpath(destInfo.absolutePath());

    const qint64 existingSize = destInfo.exists() ? destInfo.size() : 0;
    const bool resuming = existingSize > 0;

    auto file = std::make_shared<QFile>(it.destPath);
    if (!file->open(resuming ? (QIODevice::WriteOnly | QIODevice::Append) : (QIODevice::WriteOnly | QIODevice::Truncate)))
    {
        it.status = DownloadItem::Status::Failed;
        it.error = "Không mở được tệp đích để ghi (" + file->errorString() + ").";
        emit itemUpdated(id);
        emit itemFinished(id, false);
        return;
    }

    QNetworkRequest req((QUrl(it.url)));
    req.setHeader(QNetworkRequest::UserAgentHeader, "OneForAll-Downloader/1.0");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    if (resuming)
        req.setRawHeader("Range", "bytes=" + QByteArray::number(existingSize) + "-");

    QNetworkReply* reply = m_nam->get(req);
    m_activeReplies[id] = reply;
    it.status = DownloadItem::Status::Downloading;
    it.error.clear();
    if (!resuming)
        it.receivedBytes = 0;
    emit itemUpdated(id);

    auto receivedCounter = std::make_shared<qint64>(resuming ? existingSize : 0);
    auto headerChecked = std::make_shared<bool>(false);

    connect(reply, &QNetworkReply::readyRead, this, [this, id, reply, file, resuming, receivedCounter, headerChecked]() {
        if (!m_items.contains(id))
            return;
        auto& it2 = m_items[id];

        if (!*headerChecked)
        {
            *headerChecked = true;
            const int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool serverHonoredRange = resuming && httpStatus == 206;
            if (resuming && !serverHonoredRange)
            {
                // Server bỏ qua Range (trả 200 OK như tải mới) - tải lại từ đầu, không phải lỗi thật.
                file->close();
                file->open(QIODevice::WriteOnly | QIODevice::Truncate);
                *receivedCounter = 0;
            }
            const QVariant lenHeader = reply->header(QNetworkRequest::ContentLengthHeader);
            if (lenHeader.isValid())
                it2.totalBytes = serverHonoredRange ? (*receivedCounter + lenHeader.toLongLong()) : lenHeader.toLongLong();
        }

        const QByteArray chunk = reply->readAll();
        file->write(chunk);
        *receivedCounter += chunk.size();
        it2.receivedBytes = *receivedCounter;
        emit itemUpdated(id);
    });

    connect(reply, &QNetworkReply::finished, this, [this, id, reply, file]() {
        file->close();
        m_activeReplies.remove(id);
        reply->deleteLater();

        if (!m_items.contains(id))
            return;
        auto& it2 = m_items[id];

        // pause()/cancel() tự đặt trạng thái TRƯỚC khi gọi abort() - không ghi đè thành Failed trong
        // trường hợp đó, QNetworkReply::abort() luôn khiến finished() phát kèm lỗi OperationCanceledError.
        if (it2.status == DownloadItem::Status::Paused || it2.status == DownloadItem::Status::Canceled)
        {
            emit itemUpdated(id);
            tryStartNextQueued();
            return;
        }

        if (reply->error() != QNetworkReply::NoError)
        {
            it2.status = DownloadItem::Status::Failed;
            it2.error = reply->errorString();
            emit itemUpdated(id);
            emit itemFinished(id, false);
        }
        else
        {
            it2.status = DownloadItem::Status::Completed;
            it2.speedBytesPerSec = 0;
            emit itemUpdated(id);
            emit itemFinished(id, true);
        }
        tryStartNextQueued();
    });
}

void FileDownloader::pause(int id)
{
    if (!m_items.contains(id))
        return;
    auto& it = m_items[id];
    if (it.status != DownloadItem::Status::Downloading)
        return;
    it.status = DownloadItem::Status::Paused;
    it.speedBytesPerSec = 0;
    if (auto* reply = m_activeReplies.value(id))
        reply->abort(); // finished() sẽ tự dọn dẹp, giữ nguyên trạng thái Paused vừa đặt ở trên
    else
        emit itemUpdated(id);
}

void FileDownloader::cancel(int id)
{
    if (!m_items.contains(id))
        return;
    auto& it = m_items[id];
    it.status = DownloadItem::Status::Canceled;
    it.speedBytesPerSec = 0;
    if (auto* reply = m_activeReplies.value(id))
        reply->abort();
    QFile::remove(it.destPath); // dọn tệp tải dở - tránh để lại tệp không hoàn chỉnh trông như đã xong
    emit itemUpdated(id);
}

const DownloadItem* FileDownloader::item(int id) const
{
    auto it = m_items.find(id);
    return it != m_items.end() ? &it.value() : nullptr;
}
