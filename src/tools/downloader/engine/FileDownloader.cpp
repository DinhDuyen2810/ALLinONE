#include "FileDownloader.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace
{
/// Hủy một yêu cầu nếu KHÔNG nhận được byte nào trong ngần này thời gian (không phải giới hạn tổng thời
/// gian tải) - trước đây không đặt gì, một kết nối treo làm mục tải kẹt ở "Đang tải" vô thời hạn và
/// chiếm luôn một trong số chỗ tải đồng thời.
constexpr int kTransferTimeoutMs = 30000;

/// Trạng thái của MỘT lượt yêu cầu mạng (một lần start()), chia sẻ giữa các slot readyRead/finished.
struct RequestState
{
    QFile file;             ///< Tệp `<destPath>.part` đang ghi
    QString partPath;
    qint64 existingSize{0}; ///< Số byte đã có sẵn trong tệp .part khi bắt đầu lượt này (0 = tải mới)
    qint64 received{0};
    bool resuming{false};
    bool headerChecked{false};
    bool httpOk{false};
    int httpStatus{0};
    bool dropPart{false};       ///< Phần đã tải không còn dùng tiếp được - xóa .part, lần sau tải lại từ đầu
    bool abortScheduled{false};
    QString failure;            ///< Lỗi do chính ta phát hiện (ghi đĩa, phản hồi sai) - ưu tiên hơn lỗi mạng
};
} // namespace

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
            const QVariant prevSample = reply->property("lastSampleBytes");
            // Mẫu đầu tiên của một lượt TIẾP TỤC tải: lấy mốc là số byte đã có sẵn, không phải 0 - nếu
            // không, toàn bộ phần tải từ lần trước bị tính thành "tốc độ" của nửa giây đầu.
            const qint64 prev = prevSample.isValid() ? prevSample.toLongLong() : item.receivedBytes;
            item.speedBytesPerSec = qMax<qint64>(0, (item.receivedBytes - prev) * 2); // *2 vì chu kỳ 500ms = nửa giây
            reply->setProperty("lastSampleBytes", item.receivedBytes);
            emit itemUpdated(id);
        }
    });
    speedTimer->start();
}

FileDownloader::~FileDownloader()
{
    // abort() khiến finished() phát ngay - đặt cờ trước để slot đó chỉ dọn dẹp (đóng + xóa tệp .part,
    // phiên sau không còn thông tin để tiếp tục) chứ không phát tín hiệu hay tự khởi động mục đang chờ kế
    // tiếp giữa lúc đối tượng đang bị hủy.
    m_shuttingDown = true;
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
    emit itemAdded(id);
    return id;
}

bool FileDownloader::isDestPathInUse(const QString& destPath) const
{
    const QString wanted = QDir::cleanPath(destPath);
    for (const DownloadItem& item : m_items)
    {
        if (QDir::cleanPath(item.destPath).compare(wanted, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
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

void FileDownloader::failItem(int id, const QString& error)
{
    auto& it = m_items[id];
    it.status = DownloadItem::Status::Failed;
    it.speedBytesPerSec = 0;
    it.error = error;
    emit itemUpdated(id);
    emit itemFinished(id, false);
}

void FileDownloader::startNetworkRequest(int id)
{
    using FileDownloaderInternal::kPartSuffix;
    auto& it = m_items[id];

    if (!FileDownloaderInternal::isHttpUrl(it.url))
    {
        failItem(id, "Liên kết không hợp lệ - chỉ hỗ trợ địa chỉ http:// hoặc https://.");
        return;
    }

    const QFileInfo destInfo(it.destPath);
    QDir().mkpath(destInfo.absolutePath());

    auto st = std::make_shared<RequestState>();
    st->partPath = it.destPath + kPartSuffix;

    // Chỉ TIẾP TỤC khi còn giữ được ETag/Last-Modified của đúng phần đang nằm trong tệp .part (xem ghi chú
    // đầu FileDownloader.h) - không có thì bắt đầu lại từ đầu, đè lên tệp .part cũ nếu còn sót.
    const QByteArray validator = m_validators.value(id);
    const QFileInfo partInfo(st->partPath);
    st->existingSize = (!validator.isEmpty() && partInfo.exists()) ? partInfo.size() : 0;
    st->resuming = st->existingSize > 0;
    st->received = st->existingSize;
    if (!st->resuming)
        m_validators.remove(id);

    st->file.setFileName(st->partPath);
    if (!st->file.open(st->resuming ? (QIODevice::WriteOnly | QIODevice::Append) : (QIODevice::WriteOnly | QIODevice::Truncate)))
    {
        failItem(id, "Không mở được tệp đích để ghi (" + st->file.errorString() + ").");
        return;
    }

    QNetworkRequest req((QUrl(it.url)));
    req.setHeader(QNetworkRequest::UserAgentHeader, "OneForAll-Downloader/1.0");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(kTransferTimeoutMs);
    if (st->resuming)
    {
        req.setRawHeader("Range", "bytes=" + QByteArray::number(st->existingSize) + "-");
        req.setRawHeader("If-Range", validator);
        // Vị trí byte trong tệp .part là của nội dung KHÔNG nén - yêu cầu rõ để server không trả một
        // khoảng byte của bản nén (Qt cũng không tự giải nén khi ta tự đặt header này).
        req.setRawHeader("Accept-Encoding", "identity");
    }

    QNetworkReply* reply = m_nam->get(req);
    m_activeReplies[id] = reply;
    it.status = DownloadItem::Status::Downloading;
    it.error.clear();
    it.receivedBytes = st->received;
    if (!st->resuming)
        it.totalBytes = -1;
    emit itemUpdated(id);

    // Đọc mã trạng thái + header MỘT lần cho mỗi lượt - gọi ở readyRead đầu tiên, và ở finished() cho
    // phản hồi không có thân (không có readyRead nào).
    auto processHeaders = [this, id, reply, st]() {
        if (st->headerChecked)
            return;
        st->headerChecked = true;
        st->httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // CHỈ ghi dữ liệu vào tệp khi server trả mã THÀNH CÔNG (2xx). Phát hiện THẬT qua kiểm tra tay:
        // trước đây luôn ghi bất kể mã trạng thái, khiến nội dung TRANG LỖI (vd HTML "404 Not Found") bị
        // ghi nhầm vào tệp đích khi URL sai/hỏng.
        st->httpOk = st->httpStatus >= 200 && st->httpStatus < 300;
        if (!st->httpOk)
        {
            // 416 khi đang tiếp tục: phần đã có không còn khớp với tệp trên server - bỏ, lần sau tải mới.
            if (st->resuming && st->httpStatus == 416)
                st->dropPart = true;
            return;
        }
        auto itemIt = m_items.find(id);
        if (itemIt == m_items.end())
            return;
        DownloadItem& item = itemIt.value();

        const QVariant lenHeader = reply->header(QNetworkRequest::ContentLengthHeader);
        const qint64 contentLength = lenHeader.isValid() ? lenHeader.toLongLong() : -1;
        const QByteArray newValidator = FileDownloaderInternal::pickResumeValidator(
            reply->rawHeader("ETag"), reply->rawHeader("Last-Modified"));

        if (st->resuming && st->httpStatus == 206)
        {
            // Server nhận Range: kiểm tra nó trả ĐÚNG đoạn bắt đầu từ chỗ ta đang có, ở dạng không nén -
            // sai thì ghép vào sẽ ra tệp hỏng mà vẫn báo "Hoàn tất".
            qint64 rangeStart = -1, rangeTotal = -1;
            const QByteArray encoding = reply->rawHeader("Content-Encoding").trimmed().toLower();
            if (!FileDownloaderInternal::parseContentRange(reply->rawHeader("Content-Range"), &rangeStart, &rangeTotal) ||
                rangeStart != st->existingSize || !(encoding.isEmpty() || encoding == "identity"))
            {
                st->failure = "Máy chủ trả về sai đoạn dữ liệu khi tiếp tục tải - hãy thử lại để tải lại từ đầu.";
                st->dropPart = true;
                return;
            }
            item.totalBytes = rangeTotal > 0 ? rangeTotal
                                             : (contentLength >= 0 ? st->existingSize + contentLength : -1);
            if (!newValidator.isEmpty())
                m_validators[id] = newValidator;
            return;
        }

        if (st->resuming)
        {
            // Mã 200 khi đang tiếp tục: server bỏ qua Range, hoặc tệp đã ĐỔI kể từ lần trước (If-Range
            // không khớp) - nó gửi lại toàn bộ tệp, ta ghi lại từ đầu.
            st->file.close();
            if (!st->file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            {
                st->failure = "Không mở lại được tệp đích để ghi (" + st->file.errorString() + ").";
                return;
            }
            st->resuming = false;
            st->existingSize = 0;
            st->received = 0;
            item.receivedBytes = 0;
        }
        item.totalBytes = contentLength >= 0 ? contentLength : -1;
        if (newValidator.isEmpty())
            m_validators.remove(id);
        else
            m_validators[id] = newValidator;
    };

    // abort() phát finished() ngay lập tức - không gọi thẳng từ trong slot readyRead của chính reply đó.
    auto scheduleAbort = [reply, st]() {
        if (st->abortScheduled)
            return;
        st->abortScheduled = true;
        QTimer::singleShot(0, reply, &QNetworkReply::abort);
    };

    connect(reply, &QNetworkReply::readyRead, this, [this, id, reply, st, processHeaders, scheduleAbort]() {
        auto itemIt = m_items.find(id);
        if (itemIt == m_items.end())
            return;
        processHeaders();

        if (!st->failure.isEmpty())
        {
            reply->readAll();
            scheduleAbort();
            return;
        }
        if (!st->httpOk)
        {
            reply->readAll(); // xả bỏ nội dung trang lỗi (vd HTML "404") - KHÔNG ghi vào tệp đích
            return;
        }

        const QByteArray chunk = reply->readAll();
        if (st->file.write(chunk) != chunk.size())
        {
            // Trước đây không kiểm tra - đĩa đầy giữa chừng vẫn báo "Hoàn tất" với một tệp thiếu dữ liệu.
            st->failure = "Không ghi được dữ liệu vào đĩa (" + st->file.errorString() + ") - kiểm tra dung lượng trống.";
            scheduleAbort();
            return;
        }
        st->received += chunk.size();
        itemIt->receivedBytes = st->received;
        emit itemUpdated(id);
    });

    connect(reply, &QNetworkReply::finished, this, [this, id, reply, st, processHeaders]() {
        processHeaders();

        // QFile có bộ đệm - lỗi ghi (đĩa đầy) có thể chỉ lộ ra ở lúc đẩy phần còn lại xuống đĩa.
        bool flushed = true;
        if (st->file.isOpen())
        {
            flushed = st->file.flush();
            st->file.close();
        }
        m_activeReplies.remove(id);
        reply->deleteLater();

        if (m_shuttingDown)
        {
            QFile::remove(st->partPath);
            return;
        }
        auto itemIt = m_items.find(id);
        if (itemIt == m_items.end())
            return;
        DownloadItem& item = itemIt.value();
        item.speedBytesPerSec = 0;

        // pause()/cancel() tự đặt trạng thái TRƯỚC khi gọi abort() - không ghi đè thành Failed trong
        // trường hợp đó, QNetworkReply::abort() luôn khiến finished() phát kèm lỗi OperationCanceledError.
        if (item.status == DownloadItem::Status::Paused)
        {
            emit itemUpdated(id);
            tryStartNextQueued();
            return;
        }
        if (item.status == DownloadItem::Status::Canceled)
        {
            QFile::remove(st->partPath); // dọn tệp tải dở
            m_validators.remove(id);
            emit itemUpdated(id);
            tryStartNextQueued();
            return;
        }

        QString error = st->failure;
        // Tới đây mà còn OperationCanceledError thì không phải do pause()/cancel()/ta tự hủy (đã xử lý ở
        // trên) - chỉ còn hết thời gian chờ truyền dữ liệu (setTransferTimeout tự abort()).
        if (error.isEmpty() && reply->error() == QNetworkReply::OperationCanceledError)
            error = QString("Hết thời gian chờ - máy chủ không gửi dữ liệu trong %1 giây.").arg(kTransferTimeoutMs / 1000);
        if (error.isEmpty() && reply->error() != QNetworkReply::NoError)
            error = reply->errorString();
        if (error.isEmpty() && !st->httpOk)
            error = QString("Máy chủ trả mã lỗi HTTP %1.").arg(st->httpStatus);
        if (error.isEmpty() && !flushed)
            error = "Không ghi được dữ liệu vào đĩa (" + st->file.errorString() + ") - kiểm tra dung lượng trống.";

        if (error.isEmpty())
        {
            // Tải xong trọn vẹn - giờ mới đưa tệp về đúng tên đích.
            m_validators.remove(id);
            if (QFile::exists(item.destPath) && !QFile::remove(item.destPath))
                error = "Không thay được tệp đích đã có sẵn (đang bị chương trình khác mở?).";
            else if (!QFile::rename(st->partPath, item.destPath))
                error = "Không đổi tên được tệp tạm thành tệp đích.";
            if (!error.isEmpty())
                st->dropPart = true;
        }

        if (!error.isEmpty())
        {
            item.status = DownloadItem::Status::Failed;
            item.error = error;
            // Giữ tệp .part CHỈ KHI lần sau còn tiếp tục được từ đó (có ETag/Last-Modified) - để người
            // dùng thử lại không mất phần đã tải; không thì xóa, tránh để lại tệp rác.
            if (st->dropPart || !m_validators.contains(id))
            {
                QFile::remove(st->partPath);
                m_validators.remove(id);
            }
            emit itemUpdated(id);
            emit itemFinished(id, false);
        }
        else
        {
            item.status = DownloadItem::Status::Completed;
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
    // Đã tải xong thì không còn gì để hủy - trước đây hàm này xóa luôn cả tệp ĐÃ HOÀN TẤT.
    if (it.status == DownloadItem::Status::Completed)
        return;
    it.status = DownloadItem::Status::Canceled;
    it.speedBytesPerSec = 0;
    if (auto* reply = m_activeReplies.value(id))
        reply->abort(); // finished() đóng tệp .part rồi xóa nó (xem nhánh Canceled ở trên)
    QFile::remove(it.destPath + FileDownloaderInternal::kPartSuffix); // mục chưa chạy/đang tạm dừng: tự dọn ở đây
    m_validators.remove(id);
    emit itemUpdated(id);
}

const DownloadItem* FileDownloader::item(int id) const
{
    auto it = m_items.find(id);
    return it != m_items.end() ? &it.value() : nullptr;
}

namespace FileDownloaderInternal
{

bool isHttpUrl(const QString& url)
{
    const QUrl u(url.trimmed());
    if (!u.isValid() || u.host().isEmpty())
        return false;
    const QString scheme = u.scheme().toLower();
    return scheme == "http" || scheme == "https";
}

QString sanitizeFileName(const QString& rawName)
{
    // Bỏ mọi thành phần thư mục - cả hai kiểu dấu phân cách.
    QString name = rawName;
    const int sep = qMax(name.lastIndexOf('/'), name.lastIndexOf('\\'));
    if (sep >= 0)
        name = name.mid(sep + 1);

    static const QString kForbidden = QStringLiteral("\\/:*?\"<>|");
    QString out;
    out.reserve(name.size());
    for (const QChar c : name)
    {
        const ushort u = c.unicode();
        if (u < 0x20 || u == 0x7F)
            continue; // ký tự điều khiển
        if ((u >= 0x200B && u <= 0x200F) || (u >= 0x202A && u <= 0x202E) || (u >= 0x2066 && u <= 0x2069) || u == 0xFEFF)
            continue; // ký tự vô hình/đảo chiều chữ - "anh" + U+202E + "gpj.exe" hiển thị thành "anhexe.jpg"
        out += kForbidden.contains(c) ? QChar('_') : c;
    }

    auto stripTail = [](QString& s) {
        while (!s.isEmpty() && (s.back() == '.' || s.back().isSpace()))
            s.chop(1);
    };
    out = out.trimmed();
    stripTail(out); // Windows tự bỏ dấu chấm/khoảng trắng cuối tên; "." và ".." thành rỗng
    if (out.isEmpty())
        return {};

    // Tên thiết bị dành riêng của Windows (kể cả khi có phần mở rộng: "NUL.txt" vẫn là thiết bị NUL).
    static const QRegularExpression reservedRx(
        QStringLiteral("^(CON|PRN|AUX|NUL|COM[0-9]|LPT[0-9]|CONIN\\$|CONOUT\\$)$"),
        QRegularExpression::CaseInsensitiveOption);
    if (reservedRx.match(out.section('.', 0, 0).trimmed()).hasMatch())
        out.prepend('_');

    constexpr int kMaxLength = 150;
    if (out.size() > kMaxLength)
    {
        const int dot = out.lastIndexOf('.');
        const QString ext = (dot > 0 && out.size() - dot <= 20) ? out.mid(dot) : QString();
        QString stem = out.left(kMaxLength - ext.size());
        if (!stem.isEmpty() && stem.back().isHighSurrogate())
            stem.chop(1); // không cắt đôi một ký tự ngoài BMP
        stripTail(stem);
        out = stem + ext;
    }
    return out;
}

QString fileNameFromUrl(const QString& url)
{
    return sanitizeFileName(QUrl(url).fileName());
}

QString uniqueDestPath(const QString& folder, const QString& fileName,
                       const std::function<bool(const QString&)>& isTaken)
{
    QString name = sanitizeFileName(fileName);
    if (name.isEmpty())
        name = QStringLiteral("tai-ve");

    QString path = folder + "/" + name;
    if (!isTaken(path))
        return path;

    // Tránh ghi đè tệp đã có cùng tên - thêm hậu tố (1), (2)... giống Windows/trình duyệt vẫn làm.
    const int dot = name.lastIndexOf('.');
    const QString base = dot > 0 ? name.left(dot) : name;
    const QString ext = dot > 0 ? name.mid(dot) : QString();
    for (int counter = 1;; ++counter)
    {
        path = folder + "/" + base + " (" + QString::number(counter) + ")" + ext;
        if (!isTaken(path))
            return path;
    }
}

bool parseContentRange(const QByteArray& header, qint64* start, qint64* total)
{
    static const QRegularExpression rx(QStringLiteral("^\\s*bytes\\s+(\\d+)-(\\d+)/(\\d+|\\*)\\s*$"),
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = rx.match(QString::fromLatin1(header));
    if (!m.hasMatch())
        return false;
    if (start) *start = m.captured(1).toLongLong();
    if (total) *total = m.captured(3) == "*" ? -1 : m.captured(3).toLongLong();
    return true;
}

QByteArray pickResumeValidator(const QByteArray& etag, const QByteArray& lastModified)
{
    const QByteArray tag = etag.trimmed();
    if (!tag.isEmpty() && !tag.startsWith("W/"))
        return tag;
    return lastModified.trimmed();
}

} // namespace FileDownloaderInternal
