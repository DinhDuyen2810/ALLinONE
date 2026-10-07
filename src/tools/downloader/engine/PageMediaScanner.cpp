#include "PageMediaScanner.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

PageMediaScanner::PageMediaScanner(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void PageMediaScanner::scan(const QString& pageUrl)
{
    if (m_activeReply)
        return; // đã có yêu cầu đang chạy

    QNetworkRequest req((QUrl(pageUrl)));
    req.setHeader(QNetworkRequest::UserAgentHeader, "OneForAll-Downloader/1.0");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_activeReply = m_nam->get(req);
    QNetworkReply* thisReply = m_activeReply;

    connect(thisReply, &QNetworkReply::finished, this, [this, thisReply, pageUrl]() {
        if (m_activeReply != thisReply)
            return;
        m_activeReply = nullptr;
        thisReply->deleteLater();

        if (thisReply->error() != QNetworkReply::NoError)
        {
            emit errorOccurred("Không tải được trang: " + thisReply->errorString());
            return;
        }

        const QString html = QString::fromUtf8(thisReply->readAll());
        // Dùng URL thật sau khi theo chuyển hướng (nếu có) để phân giải liên kết tương đối cho đúng.
        const QUrl finalUrl = thisReply->url();
        const auto links = PageMediaScannerInternal::extractMediaLinks(html, finalUrl.toString());
        emit result(links);
    });
}

namespace PageMediaScannerInternal
{

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
        const QString trimmed = rawUrl.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith("data:")) // bỏ ảnh nhúng base64 - không phải "link" tải được
            return;
        const QUrl resolved = base.isValid() ? base.resolved(QUrl(trimmed)) : QUrl(trimmed);
        if (!resolved.isValid() || resolved.scheme().isEmpty())
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

    // <img ... src="...">
    {
        QRegularExpression rx(R"(<img\b[^>]*\bsrc\s*=\s*["']([^"']+)["'])", QRegularExpression::CaseInsensitiveOption);
        auto it = rx.globalMatch(html);
        while (it.hasNext())
            addIfNew(it.next().captured(1), MediaLink::Type::Image, false);
    }
    // <video ... src="...">  và  <audio ... src="...">
    {
        QRegularExpression rx(R"(<(video|audio)\b[^>]*\bsrc\s*=\s*["']([^"']+)["'])", QRegularExpression::CaseInsensitiveOption);
        auto it = rx.globalMatch(html);
        while (it.hasNext())
        {
            const auto m = it.next();
            const bool isAudio = m.captured(1).compare("audio", Qt::CaseInsensitive) == 0;
            addIfNew(m.captured(2), isAudio ? MediaLink::Type::Audio : MediaLink::Type::Video, false);
        }
    }
    // <source src="..."> bên trong <video>/<audio> - không biết chắc audio hay video từ chính thẻ này,
    // để classifyByExtension tự suy ra theo phần mở rộng tệp.
    {
        QRegularExpression rx(R"(<source\b[^>]*\bsrc\s*=\s*["']([^"']+)["'])", QRegularExpression::CaseInsensitiveOption);
        auto it = rx.globalMatch(html);
        while (it.hasNext())
            addIfNew(it.next().captured(1), MediaLink::Type::Other, false);
    }
    // <a href="..."> - CHỈ tính nếu đuôi tệp rõ ràng là tài liệu/media (tránh liệt kê mọi link điều hướng)
    {
        QRegularExpression rx(R"(<a\b[^>]*\bhref\s*=\s*["']([^"']+)["'])", QRegularExpression::CaseInsensitiveOption);
        auto it = rx.globalMatch(html);
        while (it.hasNext())
            addIfNew(it.next().captured(1), MediaLink::Type::Other, true);
    }

    return result;
}

} // namespace PageMediaScannerInternal
