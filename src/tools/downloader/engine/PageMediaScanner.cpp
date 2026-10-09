#include "PageMediaScanner.h"

#include "FileDownloader.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <memory>

namespace
{
/// Hủy nếu không nhận được byte nào trong ngần này thời gian - trước đây không đặt gì, một máy chủ nhận
/// kết nối rồi im lặng làm nút "Quét trang" bị khóa vĩnh viễn.
constexpr int kTransferTimeoutMs = 20000;

struct ScanState
{
    QByteArray body;
    bool headerChecked{false};
    bool truncated{false};      ///< Đã đủ kMaxPageBytes - chủ động dừng tải, vẫn phân tích phần đã có
    bool abortScheduled{false};
    QString failure;
};
} // namespace

PageMediaScanner::PageMediaScanner(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void PageMediaScanner::scan(const QString& pageUrl)
{
    if (m_activeReply)
        return; // đã có yêu cầu đang chạy

    if (!FileDownloaderInternal::isHttpUrl(pageUrl))
    {
        emit errorOccurred("Địa chỉ không hợp lệ - chỉ hỗ trợ trang bắt đầu bằng http:// hoặc https://.");
        return;
    }

    QNetworkRequest req((QUrl(pageUrl.trimmed())));
    req.setHeader(QNetworkRequest::UserAgentHeader, "OneForAll-Downloader/1.0");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(kTransferTimeoutMs);
    m_activeReply = m_nam->get(req);
    QNetworkReply* thisReply = m_activeReply;
    auto state = std::make_shared<ScanState>();

    // abort() phát finished() ngay lập tức - không gọi thẳng từ trong slot readyRead của chính reply đó.
    auto scheduleAbort = [thisReply, state]() {
        if (state->abortScheduled)
            return;
        state->abortScheduled = true;
        QTimer::singleShot(0, thisReply, &QNetworkReply::abort);
    };

    connect(thisReply, &QNetworkReply::readyRead, this, [thisReply, state, scheduleAbort]() {
        if (!state->headerChecked)
        {
            state->headerChecked = true;
            const int status = thisReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString contentType = thisReply->header(QNetworkRequest::ContentTypeHeader).toString();
            if (status >= 200 && status < 300 && !PageMediaScannerInternal::isHtmlContentType(contentType))
                state->failure = QString("Địa chỉ này trả về một tệp (%1) chứ không phải trang web - hãy dán "
                                         "nó vào tab \"Tải trực tiếp\".").arg(contentType);
        }
        if (!state->failure.isEmpty() || state->truncated)
        {
            thisReply->readAll();
            scheduleAbort();
            return;
        }
        state->body += thisReply->readAll();
        if (state->body.size() >= PageMediaScannerInternal::kMaxPageBytes)
        {
            state->body.truncate(PageMediaScannerInternal::kMaxPageBytes);
            state->truncated = true;
            scheduleAbort();
        }
    });

    connect(thisReply, &QNetworkReply::finished, this, [this, thisReply, state]() {
        if (m_activeReply != thisReply)
            return;
        m_activeReply = nullptr;
        thisReply->deleteLater();

        if (!state->failure.isEmpty())
        {
            emit errorOccurred(state->failure);
            return;
        }
        // Tự dừng vì đã đủ dung lượng thì không phải lỗi (abort() luôn để lại OperationCanceledError).
        if (!state->truncated && thisReply->error() != QNetworkReply::NoError)
        {
            // Ta không tự hủy mà vẫn OperationCanceledError = hết thời gian chờ (setTransferTimeout tự abort()).
            emit errorOccurred(thisReply->error() == QNetworkReply::OperationCanceledError
                                   ? QString("Không tải được trang: máy chủ không phản hồi trong %1 giây.").arg(kTransferTimeoutMs / 1000)
                                   : "Không tải được trang: " + thisReply->errorString());
            return;
        }

        const QString html = QString::fromUtf8(state->body);
        // Dùng URL thật sau khi theo chuyển hướng (nếu có) để phân giải liên kết tương đối cho đúng.
        const QUrl finalUrl = thisReply->url();
        const auto links = PageMediaScannerInternal::extractMediaLinks(html, finalUrl.toString());
        emit result(links);
    });
}

namespace PageMediaScannerInternal
{

bool isHtmlContentType(const QString& contentType)
{
    const QString type = contentType.section(';', 0, 0).trimmed().toLower();
    if (type.isEmpty())
        return true;
    return type.startsWith("text/") || type.contains("html") || type.contains("xml");
}

QString decodeHtmlEntities(const QString& text)
{
    if (!text.contains('&'))
        return text;

    static const QHash<QString, QString> named = {
        {"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", " "},
    };
    static const QRegularExpression rx(QStringLiteral("&(#[0-9]{1,7}|#[xX][0-9a-fA-F]{1,6}|[a-zA-Z]{2,8});"));

    QString out;
    out.reserve(text.size());
    qsizetype last = 0;
    auto it = rx.globalMatch(text);
    while (it.hasNext())
    {
        const auto m = it.next();
        out += QStringView(text).mid(last, m.capturedStart() - last);
        last = m.capturedEnd();

        const QString body = m.captured(1);
        QString replacement;
        if (body.startsWith('#'))
        {
            bool ok = false;
            const bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
            const uint code = hex ? body.mid(2).toUInt(&ok, 16) : body.mid(1).toUInt(&ok, 10);
            if (ok && code > 0 && code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF))
            {
                const char32_t ucs4 = code;
                replacement = QString::fromUcs4(&ucs4, 1);
            }
        }
        else
        {
            replacement = named.value(body.toLower());
        }
        out += replacement.isEmpty() ? m.captured(0) : replacement; // không biết thì giữ nguyên văn
    }
    out += QStringView(text).mid(last);
    return out;
}

MediaLink::Type classifyByExtension(const QString& url)
{
    const QString path = QUrl(url).path().toLower();
    const int dot = path.lastIndexOf('.');
    if (dot < 0)
        return MediaLink::Type::Other;
    const QString ext = path.mid(dot + 1);

    static const QSet<QString> images = {"jpg", "jpeg", "png", "gif", "webp", "bmp", "svg", "ico", "avif"};
    static const QSet<QString> videos = {"mp4", "webm", "mov", "avi", "mkv", "m4v", "flv", "wmv"};
    static const QSet<QString> audios = {"mp3", "wav", "ogg", "flac", "m4a", "aac", "wma"};
    static const QSet<QString> docs = {"pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx",
                                        "txt", "csv", "zip", "rar", "7z", "epub"};

    if (images.contains(ext)) return MediaLink::Type::Image;
    if (videos.contains(ext)) return MediaLink::Type::Video;
    if (audios.contains(ext)) return MediaLink::Type::Audio;
    if (docs.contains(ext)) return MediaLink::Type::Document;
    return MediaLink::Type::Other;
}

QList<MediaLink> extractMediaLinks(const QString& html, const QString& baseUrl)
{
    QList<MediaLink> result;
    QSet<QString> seen;
    const QUrl base(baseUrl);

    auto addIfNew = [&](const QString& rawUrl, MediaLink::Type forcedType, bool onlyIfKnownExtension) {
        // Giá trị thuộc tính trong HTML được viết ở dạng thực thể ("a.jpg?w=1&amp;h=2") - không giải mã
        // thì mọi URL có nhiều tham số đều sai.
        const QString trimmed = decodeHtmlEntities(rawUrl).trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith("data:", Qt::CaseInsensitive)) // bỏ ảnh nhúng base64 - không phải "link" tải được
            return;
        const QUrl resolved = base.isValid() ? base.resolved(QUrl(trimmed)) : QUrl(trimmed);
        if (!resolved.isValid())
            return;
        // Chỉ liệt kê thứ tải được qua HTTP(S) - bỏ file:, javascript:, mailto:, ftp:...
        const QString scheme = resolved.scheme().toLower();
        if ((scheme != "http" && scheme != "https") || resolved.host().isEmpty())
            return;
        const QString absolute = resolved.toString();
        if (seen.contains(absolute))
            return;

        MediaLink::Type type = forcedType;
        if (type == MediaLink::Type::Other)
            type = classifyByExtension(absolute);
        if (onlyIfKnownExtension && type == MediaLink::Type::Other)
            return; // thẻ <a> chỉ tính nếu rõ là tài liệu/media - khác hẳn link điều hướng bình thường

        seen.insert(absolute);
        MediaLink link;
        link.url = absolute;
        link.type = type;
        result << link;
    };

    // srcset="a.jpg 1x, b.jpg 2x" - mỗi ứng viên là "<url> <mô tả>", ngăn nhau bằng dấu phẩy.
    auto addSrcset = [&](const QString& rawValue, MediaLink::Type type) {
        const QString value = decodeHtmlEntities(rawValue);
        if (value.contains("data:", Qt::CaseInsensitive))
            return; // data: URI tự chứa dấu phẩy - không tách an toàn được, bỏ cả thuộc tính
        static const QRegularExpression whitespace(QStringLiteral("\\s+"));
        for (const QString& candidate : value.split(',', Qt::SkipEmptyParts))
        {
            const QStringList parts = candidate.trimmed().split(whitespace, Qt::SkipEmptyParts);
            if (!parts.isEmpty())
                addIfNew(parts.first(), type, false);
        }
    };

    // Từng thẻ quan tâm, rồi từng thuộc tính của thẻ đó - giá trị có thể nằm trong "..." , '...' hoặc
    // KHÔNG có dấu nháy (src=a.jpg; bản trước bỏ sót dạng này).
    static const QRegularExpression tagRx(QStringLiteral("<(img|video|audio|source|a)\\b([^>]*)>"),
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression attrRx(
        QStringLiteral("([a-zA-Z_:][\\w:.\\-]*)\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s\"'>]+))"));
    // Thuộc tính các thư viện lazy-load hay dùng để giữ URL ảnh THẬT (src lúc đó chỉ là ảnh giữ chỗ).
    static const QSet<QString> lazySrcAttrs = {"data-src", "data-original", "data-lazy-src", "data-lazy"};

    auto tagIt = tagRx.globalMatch(html);
    while (tagIt.hasNext())
    {
        const auto tagMatch = tagIt.next();
        const QString tag = tagMatch.captured(1).toLower();

        auto attrIt = attrRx.globalMatch(tagMatch.captured(2));
        while (attrIt.hasNext())
        {
            const auto a = attrIt.next();
            const QString name = a.captured(1).toLower();
            const QString value = a.captured(2) + a.captured(3) + a.captured(4); // chỉ một nhóm có nội dung

            if (tag == "img")
            {
                if (name == "src" || lazySrcAttrs.contains(name))
                    addIfNew(value, MediaLink::Type::Image, false);
                else if (name == "srcset" || name == "data-srcset")
                    addSrcset(value, MediaLink::Type::Image);
            }
            else if (tag == "video")
            {
                if (name == "src")
                    addIfNew(value, MediaLink::Type::Video, false);
                else if (name == "poster")
                    addIfNew(value, MediaLink::Type::Image, false);
            }
            else if (tag == "audio")
            {
                if (name == "src")
                    addIfNew(value, MediaLink::Type::Audio, false);
            }
            else if (tag == "source")
            {
                // <source> bên trong <video>/<audio>/<picture> - không biết chắc loại từ chính thẻ này, để
                // classifyByExtension tự suy ra theo phần mở rộng tệp.
                if (name == "src")
                    addIfNew(value, MediaLink::Type::Other, false);
                else if (name == "srcset")
                    addSrcset(value, MediaLink::Type::Image);
            }
            else if (tag == "a")
            {
                // <a href="..."> - CHỈ tính nếu đuôi tệp rõ ràng là tài liệu/media (tránh liệt kê mọi link điều hướng)
                if (name == "href")
                    addIfNew(value, MediaLink::Type::Other, true);
            }
        }
    }

    return result;
}

} // namespace PageMediaScannerInternal
