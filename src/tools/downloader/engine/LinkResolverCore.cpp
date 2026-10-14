#include "LinkResolverCore.h"

#include "FileDownloader.h"
#include "PageMediaScanner.h"

#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

namespace
{
using PageMediaScannerInternal::decodeHtmlEntities;

bool hostIs(const QString& host, const QString& domain)
{
    return host == domain || host.endsWith('.' + domain);
}

/// Đuôi tệp (chữ thường, không có dấu chấm) của PHẦN ĐƯỜNG DẪN một URL; rỗng nếu không có.
QString pathExtension(const QString& url)
{
    const QString path = QUrl(url).path().toLower();
    const int dot = path.lastIndexOf('.');
    const int slash = path.lastIndexOf('/');
    return (dot > slash && dot >= 0) ? path.mid(dot + 1) : QString();
}

const QSet<QString>& manifestExts() { static const QSet<QString> s = {"m3u8", "mpd"}; return s; }
const QSet<QString>& videoExts() { static const QSet<QString> s = {"mp4", "webm", "mov", "m4v", "mkv", "flv", "avi", "wmv"}; return s; }
const QSet<QString>& audioExts() { static const QSet<QString> s = {"mp3", "m4a", "aac", "ogg", "wav", "flac", "opus"}; return s; }
const QSet<QString>& docExts()
{
    static const QSet<QString> s = {"pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx", "zip", "rar", "7z", "epub"};
    return s;
}

/// Giải mã các dạng escape hay gặp trong chuỗi JS/JSON nhúng trong HTML để regex URL nhìn thấy URL thật.
QString unescapeScriptText(QString t)
{
    t.replace(QStringLiteral("\\/"), QStringLiteral("/"));
    t.replace(QStringLiteral("\\u0026"), QStringLiteral("&"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("\\u002f"), QStringLiteral("/"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("\\u003d"), QStringLiteral("="), Qt::CaseInsensitive);
    t.replace(QStringLiteral("\\u003a"), QStringLiteral(":"), Qt::CaseInsensitive);
    t.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return t;
}

/// Một chuỗi URL tìm thấy trong mã nguồn có thể CHỨA URL khác (`player/?url=https://x/y.m3u8&a=1`, hoặc dạng mã
/// hóa phần trăm `url=https%3A%2F%2Fx%2Fy.m3u8`) - trả chính nó và các URL lồng bên trong.
QStringList expandNestedUrls(const QString& s)
{
    QStringList out{s};
    QString decoded = s;
    if (s.contains(QStringLiteral("%3A%2F%2F"), Qt::CaseInsensitive))
    {
        decoded = QUrl::fromPercentEncoding(s.toUtf8());
        if (decoded != s)
            out << decoded;
    }
    for (const QString& base : {s, decoded})
    {
        int from = 1;
        for (;;)
        {
            const int h1 = base.indexOf(QStringLiteral("http://"), from, Qt::CaseInsensitive);
            const int h2 = base.indexOf(QStringLiteral("https://"), from, Qt::CaseInsensitive);
            int pos = -1;
            if (h1 >= 0 && h2 >= 0) pos = qMin(h1, h2);
            else pos = qMax(h1, h2);
            if (pos < 0)
                break;
            QString nested = base.mid(pos);
            const int amp = nested.indexOf('&'); // sau URL lồng thường là tham số kế tiếp của URL ngoài
            if (amp > 0)
                nested.truncate(amp);
            out << nested;
            from = pos + 1;
        }
    }
    return out;
}

QString trimUrlTail(QString u)
{
    while (!u.isEmpty() && QStringLiteral(")],;.'\"").contains(u.back()))
        u.chop(1);
    return u;
}

MediaLink::Type typeForExt(const QString& ext)
{
    if (manifestExts().contains(ext) || videoExts().contains(ext)) return MediaLink::Type::Video;
    if (audioExts().contains(ext)) return MediaLink::Type::Audio;
    if (docExts().contains(ext)) return MediaLink::Type::Document;
    return MediaLink::Type::Other;
}

QHash<QString, QString> parseAttributes(const QString& tagBody)
{
    static const QRegularExpression attrRx(
        QStringLiteral("([a-zA-Z_:][\\w:.\\-]*)\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s\"'>]+))"));
    QHash<QString, QString> attrs;
    auto it = attrRx.globalMatch(tagBody);
    while (it.hasNext())
    {
        const auto m = it.next();
        attrs.insert(m.captured(1).toLower(), decodeHtmlEntities(m.captured(2) + m.captured(3) + m.captured(4)));
    }
    return attrs;
}
} // namespace

namespace LinkResolverCore
{
Site siteOf(const QString& url)
{
    const QString host = QUrl(url.trimmed()).host().toLower();
    if (hostIs(host, "facebook.com") || hostIs(host, "fb.watch") || hostIs(host, "fb.com")) return Site::Facebook;
    if (hostIs(host, "instagram.com")) return Site::Instagram;
    if (hostIs(host, "tiktok.com")) return Site::TikTok;
    if (hostIs(host, "youtube.com") || hostIs(host, "youtu.be")) return Site::YouTube;
    if (host == "drive.google.com" || host == "drive.usercontent.google.com") return Site::GoogleDrive;
    if (host == "docs.google.com") return Site::GoogleDocs;
    return Site::Generic;
}

// ---------------------------------------------------------------------------------------------- Nhập liệu
QStringList extractUrls(const QString& text)
{
    QStringList out;
    QSet<QString> seen;
    static const QRegularExpression ws(QStringLiteral("\\s+"));
    static const QRegularExpression bareDomain(
        QStringLiteral("^(?:www\\.)?[a-z0-9][a-z0-9\\-]*(?:\\.[a-z0-9][a-z0-9\\-]*)+(?::\\d+)?/\\S*$"),
        QRegularExpression::CaseInsensitiveOption);

    for (QString token : text.split(ws, Qt::SkipEmptyParts))
    {
        while (!token.isEmpty() && QStringLiteral("<([\"'").contains(token.front()))
            token.remove(0, 1);
        while (!token.isEmpty() && QStringLiteral(">]\"'.,;!?").contains(token.back()))
            token.chop(1);
        // ")" thừa ở cuối (link nằm trong ngoặc) - chỉ bỏ nếu không có "(" nào đi cùng trong link.
        if (token.endsWith(')') && !token.contains('('))
            token.chop(1);
        if (token.isEmpty())
            continue;

        QString url;
        if (token.startsWith(QStringLiteral("http://"), Qt::CaseInsensitive) ||
            token.startsWith(QStringLiteral("https://"), Qt::CaseInsensitive))
            url = token;
        else if (token.startsWith(QStringLiteral("www."), Qt::CaseInsensitive) || bareDomain.match(token).hasMatch())
            url = "https://" + token;
        if (url.isEmpty() || !FileDownloaderInternal::isHttpUrl(url))
            continue;
        if (seen.contains(url))
            continue;
        seen.insert(url);
        out << url;
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- Google Drive / Docs
bool parseDriveFileId(const QString& url, QString* fileId)
{
    const QUrl u(url.trimmed());
    const QString host = u.host().toLower();
    if (host != "drive.google.com" && host != "drive.usercontent.google.com")
        return false;
    const QString path = u.path();
    if (path.contains(QStringLiteral("/folders/")))
        return false; // thư mục - không phải một tệp

    static const QRegularExpression fileRx(QStringLiteral("/file/d/([A-Za-z0-9_\\-]{10,})"));
    static const QRegularExpression idRx(QStringLiteral("^[A-Za-z0-9_\\-]{10,}$"));
    QString id;
    const auto m = fileRx.match(path);
    if (m.hasMatch())
        id = m.captured(1);
    else
    {
        const QString q = QUrlQuery(u).queryItemValue(QStringLiteral("id"));
        if (idRx.match(q).hasMatch())
            id = q;
    }
    if (id.isEmpty())
        return false;
    if (fileId)
        *fileId = id;
    return true;
}

QString driveDirectUrl(const QString& fileId)
{
    return QStringLiteral("https://drive.usercontent.google.com/download?id=%1&export=download&confirm=t").arg(fileId);
}

bool googleDocsExportUrl(const QString& url, QString* exportUrl, QString* suggestedExt)
{
    const QUrl u(url.trimmed());
    if (u.host().toLower() != "docs.google.com")
        return false;
    static const QRegularExpression rx(QStringLiteral("^/(document|spreadsheets|presentation)/d/([A-Za-z0-9_\\-]{10,})"));
    const auto m = rx.match(u.path());
    if (!m.hasMatch())
        return false;
    const QString kind = m.captured(1), id = m.captured(2);
    QString exportPath, ext;
    if (kind == "document") { exportPath = QStringLiteral("/document/d/%1/export?format=pdf").arg(id); ext = "pdf"; }
    else if (kind == "spreadsheets") { exportPath = QStringLiteral("/spreadsheets/d/%1/export?format=xlsx").arg(id); ext = "xlsx"; }
    else { exportPath = QStringLiteral("/presentation/d/%1/export/pptx").arg(id); ext = "pptx"; }
    if (exportUrl) *exportUrl = "https://docs.google.com" + exportPath;
    if (suggestedExt) *suggestedExt = ext;
    return true;
}

DriveBlock classifyDriveHtml(const QString& html)
{
    const QString low = html.left(60000).toLower();
    if (low.contains(QStringLiteral("quota exceeded")) || low.contains(QStringLiteral("too many users have viewed or downloaded")))
        return DriveBlock::Quota;
    if (low.contains(QStringLiteral("id=\"download-form\"")) || low.contains(QStringLiteral("name=\"confirm\"")))
        return DriveBlock::NeedsConfirm;
    if (low.contains(QStringLiteral("uc-error-caption")) || low.contains(QStringLiteral("permission to download")) ||
        low.contains(QStringLiteral("can&#39;t download file")) || low.contains(QStringLiteral("can't download file")))
        return DriveBlock::NoPermission;
    if (low.contains(QStringLiteral("accounts.google.com")) &&
        (low.contains(QStringLiteral("servicelogin")) || low.contains(QStringLiteral("signin"))))
        return DriveBlock::SignIn;
    return DriveBlock::None;
}

QString driveConfirmUrl(const QString& html, const QString& baseUrl)
{
    static const QRegularExpression formRx(
        QStringLiteral("<form\\b([^>]*id=\"download-form\"[^>]*)>(.*?)</form>"),
        QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    const auto fm = formRx.match(html);
    if (!fm.hasMatch())
        return {};
    const QString action = parseAttributes(fm.captured(1)).value(QStringLiteral("action"));
    if (action.isEmpty())
        return {};

    QUrlQuery query;
    static const QRegularExpression inputRx(QStringLiteral("<input\\b([^>]*)>"), QRegularExpression::CaseInsensitiveOption);
    auto it = inputRx.globalMatch(fm.captured(2));
    while (it.hasNext())
    {
        const auto attrs = parseAttributes(it.next().captured(1));
        if (attrs.value(QStringLiteral("type")).toLower() == "hidden" && attrs.contains(QStringLiteral("name")))
            query.addQueryItem(attrs.value(QStringLiteral("name")), attrs.value(QStringLiteral("value")));
    }
    QUrl url = QUrl(baseUrl).resolved(QUrl(action));
    if (!url.isValid() || url.host().isEmpty())
        return {};
    url.setQuery(query);
    return url.toString();
}

QString driveBlockMessage(DriveBlock block)
{
    switch (block)
    {
        case DriveBlock::NoPermission: return "Chủ sở hữu không cho phép tải tệp này về (chỉ chủ sở hữu/người chỉnh sửa tải được).";
        case DriveBlock::Quota: return "Tệp Google Drive đã vượt hạn mức tải trong ngày - thử lại sau.";
        case DriveBlock::NeedsConfirm: return "Google Drive yêu cầu xác nhận tải nhưng không dựng được liên kết tải.";
        case DriveBlock::SignIn: return "Tệp Google Drive yêu cầu đăng nhập tài khoản Google có quyền truy cập.";
        case DriveBlock::None: break;
    }
    return {};
}

DriveViewerName driveViewerName(const QString& pageTitle)
{
    DriveViewerName out;
    static const QRegularExpression suffix(QStringLiteral(R"(\s*[-–]\s*Google Drive\s*$)"), QRegularExpression::CaseInsensitiveOption);
    QString title = pageTitle;
    title.remove(suffix);
    title = title.simplified();
    if (title.isEmpty() || title.compare(QStringLiteral("Google Drive"), Qt::CaseInsensitive) == 0)
        return out; // trang không mang tên tệp (đăng nhập, lỗi...)

    out.baseName = title;
    const int dot = title.lastIndexOf('.');
    if (dot > 0 && title.size() - dot - 1 <= 5)
    {
        const QString ext = title.mid(dot + 1).toLower();
        static const QRegularExpression extRx(QStringLiteral("^[a-z0-9]{1,5}$"));
        if (extRx.match(ext).hasMatch())
        {
            out.extension = ext;
            // Tài liệu quen thuộc: bỏ đuôi (PDF kết quả sẽ có đuôi .pdf). Đuôi lạ: giữ cả tên.
            static const QSet<QString> documentExt = {"pdf", "ppt", "pptx", "pps", "ppsx", "doc", "docx", "xls", "xlsx", "odt", "odp",
                                                      "ods", "rtf", "txt", "csv", "epub", "key", "pages", "numbers"};
            // Loại KHÔNG xem theo trang: không chụp từng trang thành PDF được.
            static const QSet<QString> nonPaged = {"mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "3gp", "mp3", "wav", "flac",
                                                   "m4a", "ogg", "aac", "zip", "rar", "7z", "gz", "tar", "iso", "exe", "msi", "apk",
                                                   "dmg", "jpg", "jpeg", "png", "gif", "bmp", "webp", "svg", "heic", "tif", "tiff"};
            if (documentExt.contains(ext))
                out.baseName = title.left(dot);
            if (nonPaged.contains(ext))
                out.pageable = false;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- Loại link
bool isLikelyProfileOrCollectionUrl(const QString& url)
{
    const QUrl u(url.trimmed());
    const QString path = u.path();
    const QUrlQuery q(u);
    switch (siteOf(url))
    {
        case Site::YouTube:
            if (path.startsWith(QStringLiteral("/@")) || path.startsWith(QStringLiteral("/channel/")) ||
                path.startsWith(QStringLiteral("/c/")) || path.startsWith(QStringLiteral("/user/")))
                return true;
            if (path == QStringLiteral("/playlist"))
                return true;
            return q.hasQueryItem(QStringLiteral("list")) && !q.hasQueryItem(QStringLiteral("v"));
        case Site::TikTok:
        {
            static const QRegularExpression rx(QStringLiteral("^/@[^/]+/?$"));
            return rx.match(path).hasMatch();
        }
        case Site::Instagram:
        {
            static const QRegularExpression rx(QStringLiteral("^/([A-Za-z0-9._]+)/?$"));
            static const QSet<QString> reserved = {"p", "reel", "reels", "tv", "explore", "stories", "accounts", "direct",
                                                   "about", "legal", "developer", "web", "challenge", "emails"};
            const auto m = rx.match(path);
            return m.hasMatch() && !reserved.contains(m.captured(1).toLower());
        }
        case Site::Facebook:
        {
            if (path == QStringLiteral("/profile.php"))
                return q.hasQueryItem(QStringLiteral("id"));
            static const QRegularExpression rx(QStringLiteral("^/([A-Za-z0-9.\\-]+)/?(?:videos|reels|photos|posts)?/?$"));
            static const QSet<QString> reserved = {"photo", "photo.php", "watch", "reel", "reels", "share", "groups", "events",
                                                   "marketplace", "story.php", "permalink.php", "login", "dialog", "sharer",
                                                   "sharer.php", "plugins", "stories", "gaming", "pages", "hashtag", "help",
                                                   "policies", "privacy", "terms", "ads", "business"};
            const auto m = rx.match(path);
            return m.hasMatch() && !reserved.contains(m.captured(1).toLower());
        }
        default:
            return false;
    }
}

bool isPhotoPostUrl(const QString& url)
{
    const QUrl u(url.trimmed());
    const QString path = u.path();
    switch (siteOf(url))
    {
        case Site::Facebook:
            return path.startsWith(QStringLiteral("/photo")) || path.contains(QStringLiteral("/photos/")) ||
                   path.startsWith(QStringLiteral("/media/set"));
        case Site::TikTok:
            return path.contains(QStringLiteral("/photo/"));
        default:
            return false;
    }
}

bool isManifestUrl(const QString& url)
{
    return manifestExts().contains(pathExtension(url));
}

// ---------------------------------------------------------------------------------------------- Phân tích HTML
QString htmlTitle(const QString& html)
{
    static const QRegularExpression rx(QStringLiteral("<title[^>]*>(.*?)</title>"),
                                       QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
    const auto m = rx.match(html);
    return m.hasMatch() ? decodeHtmlEntities(m.captured(1)).simplified() : QString();
}

QString metaContent(const QString& html, const QString& key)
{
    static const QRegularExpression metaRx(QStringLiteral("<meta\\b([^>]*)>"), QRegularExpression::CaseInsensitiveOption);
    auto it = metaRx.globalMatch(html);
    while (it.hasNext())
    {
        const auto attrs = parseAttributes(it.next().captured(1));
        const QString name = attrs.value(QStringLiteral("property"), attrs.value(QStringLiteral("name")));
        if (name.compare(key, Qt::CaseInsensitive) == 0 && attrs.contains(QStringLiteral("content")))
            return attrs.value(QStringLiteral("content")).trimmed();
    }
    return {};
}

DeepScan deepScanHtml(const QString& html, const QString& baseUrl)
{
    DeepScan out;
    out.title = metaContent(html, QStringLiteral("og:title"));
    if (out.title.isEmpty())
        out.title = htmlTitle(html);

    const QUrl base(baseUrl);
    QSet<QString> seen;
    auto add = [&](const QString& rawUrl, MediaLink::Type forcedType) {
        const QString trimmed = decodeHtmlEntities(rawUrl).trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive))
            return;
        const QUrl resolved = base.isValid() ? base.resolved(QUrl(trimmed)) : QUrl(trimmed);
        const QString scheme = resolved.scheme().toLower();
        if (!resolved.isValid() || (scheme != "http" && scheme != "https") || resolved.host().isEmpty())
            return;
        const QString absolute = resolved.toString();
        if (seen.contains(absolute))
            return;
        seen.insert(absolute);
        MediaLink link;
        link.url = absolute;
        link.type = forcedType;
        if (link.type == MediaLink::Type::Other)
            link.type = isManifestUrl(absolute) ? MediaLink::Type::Video : PageMediaScannerInternal::classifyByExtension(absolute);
        out.media << link;
    };

    // 1) Thẻ HTML quen thuộc (bộ quét sẵn có).
    for (const MediaLink& l : PageMediaScannerInternal::extractMediaLinks(html, baseUrl))
        add(l.url, l.type);

    // 2) Thẻ <meta> (Open Graph/Twitter) - nhiều trang đặt URL video/ảnh chính ở đây.
    for (const QString& k : {"og:video", "og:video:url", "og:video:secure_url", "twitter:player:stream"})
        if (const QString v = metaContent(html, k); !v.isEmpty())
            add(v, MediaLink::Type::Video);
    if (const QString v = metaContent(html, QStringLiteral("og:audio")); !v.isEmpty())
        add(v, MediaLink::Type::Audio);
    for (const QString& k : {"og:image", "og:image:secure_url", "twitter:image"})
        if (const QString v = metaContent(html, k); !v.isEmpty())
            add(v, MediaLink::Type::Image);

    // 3) CHUỖI THÔ trong mã nguồn (script/JSON): URL tuyệt đối hoặc giao thức tương đối có đuôi video/âm thanh/
    // luồng/tài liệu. Ảnh KHÔNG quét ở đây (quá nhiều thứ không phải nội dung: logo, quảng cáo...).
    {
        const QString text = unescapeScriptText(html);
        static const QRegularExpression urlRx(QStringLiteral("(?:https?:)?//[^\\s\"'<>\\\\`{}|^]{4,}"));
        auto it = urlRx.globalMatch(text);
        int budget = 20000;
        while (it.hasNext() && budget-- > 0)
        {
            QString raw = it.next().captured(0);
            for (QString cand : expandNestedUrls(raw))
            {
                cand = trimUrlTail(cand);
                if (cand.startsWith(QStringLiteral("//")))
                    cand = "https:" + cand;
                const QString ext = pathExtension(cand);
                if (manifestExts().contains(ext) || videoExts().contains(ext) || audioExts().contains(ext) || docExts().contains(ext))
                    add(cand, typeForExt(ext));
            }
        }
        // Đường dẫn tương đối trong chuỗi ("/hls/index.m3u8").
        static const QRegularExpression relRx(
            QStringLiteral("[\"']((?:\\./|/)[^\"'\\s<>\\\\]+\\.(?:m3u8|mpd|mp4|webm|mp3|m4a|flv|mov)(?:\\?[^\"'\\s<>\\\\]*)?)[\"']"),
            QRegularExpression::CaseInsensitiveOption);
        auto rit = relRx.globalMatch(text);
        while (rit.hasNext())
        {
            const QString cand = rit.next().captured(1);
            add(cand, typeForExt(pathExtension(cand)));
        }
    }

    // 4) Khung nhúng (trình phát) - để nơi gọi tải tiếp một mức.
    {
        static const QRegularExpression ifrRx(QStringLiteral("<iframe\\b([^>]*)>"), QRegularExpression::CaseInsensitiveOption);
        static const QStringList skipHosts = {"doubleclick.net", "googletagmanager.com", "google.com/recaptcha",
                                              "facebook.com/plugins", "googlesyndication.com", "adservice.google"};
        QSet<QString> seenFrames;
        auto it = ifrRx.globalMatch(html);
        while (it.hasNext() && out.iframes.size() < 8)
        {
            const QString src = parseAttributes(it.next().captured(1)).value(QStringLiteral("src")).trimmed();
            if (src.isEmpty() || src.startsWith(QStringLiteral("about:")) || src.startsWith(QStringLiteral("javascript:")) ||
                src.startsWith(QStringLiteral("data:")))
                continue;
            const QUrl resolved = base.isValid() ? base.resolved(QUrl(src)) : QUrl(src);
            const QString abs = resolved.toString();
            if (resolved.host().isEmpty() || (resolved.scheme() != "http" && resolved.scheme() != "https"))
                continue;
            bool skip = false;
            for (const QString& s : skipHosts)
                if (abs.contains(s, Qt::CaseInsensitive)) { skip = true; break; }
            if (!skip && !seenFrames.contains(abs))
            {
                seenFrames.insert(abs);
                out.iframes << abs;
            }
        }
    }
    return out;
}

bool looksLikeChallengePage(const QString& html)
{
    const QString low = html.left(40000).toLower();
    return low.contains(QStringLiteral("<title>just a moment")) || low.contains(QStringLiteral("cf-browser-verification")) ||
           low.contains(QStringLiteral("/cdn-cgi/challenge-platform")) || low.contains(QStringLiteral("attention required! | cloudflare")) ||
           low.contains(QStringLiteral("checking your browser before accessing")) || low.contains(QStringLiteral("cf-chl-"));
}

bool looksLikeLoginWall(const QString& html)
{
    const QString title = htmlTitle(html).toLower();
    if (title.contains(QStringLiteral("log in")) || title.contains(QStringLiteral("login")) ||
        title.contains(QStringLiteral("sign in")) || title.contains(QStringLiteral("đăng nhập")))
        return true;
    return html.left(200000).contains(QStringLiteral("type=\"password\""), Qt::CaseInsensitive);
}

// ---------------------------------------------------------------------------------------------- Trình duyệt ẩn
bool isJunkMediaUrl(const QString& url)
{
    const QUrl u(url);
    const QString host = u.host().toLower();
    const QString path = u.path().toLower();
    return host.contains(QStringLiteral("static")) || path.contains(QStringLiteral("login_static")) ||
           path.contains(QStringLiteral("/static/")) || path.contains(QStringLiteral("webapp-desktop"));
}

QStringList mediaUrlsFromNetLog(const QByteArray& netLog, int maxCount)
{
    const QString text = unescapeScriptText(QString::fromUtf8(netLog));
    static const QRegularExpression rx(QStringLiteral("\"url\"\\s*:\\s*\"(https?://[^\"\\\\]+)\""));
    QStringList manifests, others;
    QSet<QString> seen;
    auto it = rx.globalMatch(text);
    while (it.hasNext())
    {
        const QString url = it.next().captured(1);
        if (seen.contains(url) || isJunkMediaUrl(url))
            continue;
        const QString ext = pathExtension(url);
        // Đoạn luồng (.ts/.m4s) và thành phần vô nghĩa không đáng liệt kê; aac thường là đoạn HLS âm thanh.
        if (manifestExts().contains(ext))
            manifests << url;
        else if (videoExts().contains(ext) || ext == "mp3" || ext == "m4a" || ext == "ogg" || ext == "wav")
        {
            if (url.contains(QStringLiteral("init.mp4"), Qt::CaseInsensitive))
                continue;
            others << url;
        }
        else
            continue;
        seen.insert(url);
    }
    QStringList all = manifests + others;
    while (all.size() > maxCount)
        all.removeLast();
    return all;
}

QList<MediaLink> photosFromDom(const QString& dom, const QString& pageUrl, int maxCount)
{
    QList<MediaLink> out;
    QSet<QString> seenKeys;
    const QUrl base(pageUrl);
    const Site site = siteOf(pageUrl);

    auto accept = [&](const QString& url) {
        const QString low = url.toLower();
        if (low.endsWith(QStringLiteral(".svg")) || low.contains(QStringLiteral(".svg?")) || low.contains(QStringLiteral(".ico")))
            return false;
        // Luật theo trang: ảnh nội dung của Facebook/TikTok có dấu hiệu riêng, phần còn lại là ảnh phụ.
        const QString host = QUrl(url).host().toLower();
        if (host.contains(QStringLiteral("fbcdn")) || host.contains(QStringLiteral("scontent")))
        {
            static const QRegularExpression small(
                QStringLiteral("(?:_s\\.|_t\\.|/p\\d+x\\d+/|/s\\d+x\\d+/|cp0_|stp=c\\d|emoji|/rsrc\\.php|static\\.)"));
            if (small.match(low).hasMatch())
                return false;
            return low.contains(QStringLiteral("/t39.30808-6/")) || low.contains(QStringLiteral("cstp=mx")) ||
                   low.contains(QStringLiteral("/t51."));
        }
        if (site == Site::TikTok || host.contains(QStringLiteral("tiktokcdn")) || host.contains(QStringLiteral("byteimg")))
            return low.contains(QStringLiteral("photomode")) && !low.contains(QStringLiteral("cropcenter")) &&
                   !low.contains(QStringLiteral("-avt-")) && !low.contains(QStringLiteral("/avt-"));
        static const QRegularExpression junk(
            QStringLiteral("(?:avatar|icon|logo|sprite|emoji|pixel|1x1|spacer|blank|favicon|badge|loading)"));
        return !junk.match(low).hasMatch();
    };

    auto add = [&](const QString& raw) {
        const QString trimmed = decodeHtmlEntities(raw).trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QStringLiteral("data:"), Qt::CaseInsensitive))
            return;
        const QUrl resolved = base.isValid() ? base.resolved(QUrl(trimmed)) : QUrl(trimmed);
        if (!resolved.isValid() || resolved.host().isEmpty() || (resolved.scheme() != "http" && resolved.scheme() != "https"))
            return;
        const QString abs = resolved.toString();
        if (!accept(abs))
            return;
        const QString key = resolved.host() + resolved.path(); // cùng ảnh khác tham số -> một mục
        if (seenKeys.contains(key) || out.size() >= maxCount)
            return;
        seenKeys.insert(key);
        MediaLink l;
        l.url = abs;
        l.type = MediaLink::Type::Image;
        out << l;
    };

    static const QRegularExpression imgRx(QStringLiteral("<img\\b([^>]*)>"), QRegularExpression::CaseInsensitiveOption);
    auto it = imgRx.globalMatch(dom);
    while (it.hasNext())
    {
        const auto attrs = parseAttributes(it.next().captured(1));
        const QString srcset = attrs.value(QStringLiteral("srcset"));
        if (!srcset.isEmpty() && !srcset.contains(QStringLiteral("data:")))
        {
            // Ứng viên cuối của srcset thường là bản lớn nhất.
            const QStringList cands = srcset.split(',', Qt::SkipEmptyParts);
            if (!cands.isEmpty())
                add(cands.last().trimmed().section(' ', 0, 0));
        }
        add(attrs.value(QStringLiteral("src")));
        add(attrs.value(QStringLiteral("data-src")));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- Bài đăng trong hồ sơ
QString tiktokPostId(const QString& url)
{
    if (siteOf(url) != Site::TikTok)
        return {};
    static const QRegularExpression rx(QStringLiteral("^/@[^/]+/(?:photo|video)/(\\d+)"));
    const auto m = rx.match(QUrl(url.trimmed()).path());
    return m.hasMatch() ? m.captured(1) : QString();
}

QString tiktokPhotoUrlFromVideoUrl(const QString& url)
{
    if (siteOf(url) != Site::TikTok)
        return {};
    QUrl u(url.trimmed());
    static const QRegularExpression rx(QStringLiteral("^(/@[^/]+)/video/(\\d+)"));
    const auto m = rx.match(u.path());
    if (!m.hasMatch())
        return {};
    u.setPath(m.captured(1) + QStringLiteral("/photo/") + m.captured(2));
    u.setQuery(QString());
    u.setFragment(QString());
    return u.toString();
}

QList<PostRef> postLinksFromDom(const QString& dom, const QString& pageUrl, int maxCount)
{
    QList<PostRef> out;
    QSet<QString> seen;
    const QUrl base(pageUrl);
    const Site pageSite = siteOf(pageUrl);
    if (pageSite != Site::Facebook && pageSite != Site::TikTok && pageSite != Site::Instagram)
        return out;

    // Hồ sơ TikTok: chỉ lấy bài của CHÍNH người dùng này (trang còn có gợi ý tài khoản khác).
    QString tiktokOwner;
    if (pageSite == Site::TikTok)
    {
        static const QRegularExpression ownerRx(QStringLiteral("^/(@[^/]+)"));
        const auto om = ownerRx.match(base.path());
        tiktokOwner = om.hasMatch() ? om.captured(1).toLower() : QString();
    }

    static const QRegularExpression aRx(QStringLiteral("<a\\b([^>]*)>"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression tiktokRx(QStringLiteral("^/(@[^/]+)/(photo|video)/(\\d+)"));
    static const QRegularExpression fbVideoRx(QStringLiteral("^/([^/]+)/videos/(\\d+)"));
    static const QRegularExpression fbReelRx(QStringLiteral("^/reel/(\\d+)"));
    static const QRegularExpression fbPostRx(QStringLiteral("^/([^/]+)/posts/([A-Za-z0-9]+)"));
    static const QRegularExpression igRx(QStringLiteral("^/(?:[^/]+/)?(p|reel|tv)/([A-Za-z0-9_\\-]+)"));

    auto push = [&](const QString& url, const QString& id, bool photo) {
        const QString key = (photo ? QStringLiteral("p:") : QStringLiteral("v:")) + id;
        if (id.isEmpty() || seen.contains(key) || out.size() >= maxCount)
            return;
        seen.insert(key);
        PostRef r;
        r.url = url;
        r.id = id;
        r.photo = photo;
        out << r;
    };

    auto it = aRx.globalMatch(dom);
    while (it.hasNext())
    {
        const QString href = parseAttributes(it.next().captured(1)).value(QStringLiteral("href")).trimmed();
        if (href.isEmpty() || href.startsWith('#') || href.startsWith(QStringLiteral("javascript:"), Qt::CaseInsensitive))
            continue;
        const QUrl u = base.isValid() ? base.resolved(QUrl(href)) : QUrl(href);
        if (!u.isValid() || u.host().isEmpty() || (u.scheme() != QLatin1String("http") && u.scheme() != QLatin1String("https")))
            continue;
        if (siteOf(u.toString()) != pageSite)
            continue;
        const QString path = u.path();
        const QUrlQuery q(u);

        if (pageSite == Site::TikTok)
        {
            const auto m = tiktokRx.match(path);
            if (!m.hasMatch() || m.captured(1).toLower() != tiktokOwner)
                continue;
            const bool photo = m.captured(2) == QLatin1String("photo");
            push(QStringLiteral("https://www.tiktok.com/%1/%2/%3").arg(m.captured(1), m.captured(2), m.captured(3)), m.captured(3), photo);
        }
        else if (pageSite == Site::Facebook)
        {
            if ((path == QLatin1String("/photo") || path == QLatin1String("/photo/") || path == QLatin1String("/photo.php")) &&
                q.hasQueryItem(QStringLiteral("fbid")))
            {
                const QString id = q.queryItemValue(QStringLiteral("fbid"));
                push(QStringLiteral("https://www.facebook.com/photo/?fbid=") + id, id, true);
                continue;
            }
            auto m = fbVideoRx.match(path);
            if (m.hasMatch())
            {
                push(QStringLiteral("https://www.facebook.com/%1/videos/%2").arg(m.captured(1), m.captured(2)), m.captured(2), false);
                continue;
            }
            m = fbReelRx.match(path);
            if (m.hasMatch())
            {
                push(QStringLiteral("https://www.facebook.com/reel/") + m.captured(1), m.captured(1), false);
                continue;
            }
            if (path == QLatin1String("/watch") || path == QLatin1String("/watch/"))
            {
                const QString v = q.queryItemValue(QStringLiteral("v"));
                if (!v.isEmpty())
                    push(QStringLiteral("https://www.facebook.com/watch/?v=") + v, v, false);
                continue;
            }
            m = fbPostRx.match(path);
            if (m.hasMatch())
                push(QStringLiteral("https://www.facebook.com/%1/posts/%2").arg(m.captured(1), m.captured(2)), m.captured(2), true);
        }
        else // Instagram
        {
            const auto m = igRx.match(path);
            if (m.hasMatch())
                push(QStringLiteral("https://www.instagram.com/%1/%2/").arg(m.captured(1), m.captured(2)), m.captured(2), false);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------------------------- yt-dlp
QStringList ytDlpAnalyzeArguments(const QString& url, int maxItems, const QStringList& extra)
{
    // Xin THÊM MỘT mục so với giới hạn: parseYtDlpJson() chỉ biết danh sách bị cắt (truncated -> cảnh báo "chỉ lấy
    // N mục đầu") khi thấy mục thứ N+1. Xin đúng N như trước thì cờ đó không bao giờ bật.
    QStringList a = {"--ignore-config", "--dump-single-json", "--no-warnings", "--flat-playlist",
                     "--socket-timeout", "20", "--playlist-end", QString::number(qMax(1, maxItems) + 1)};
    if (!isLikelyProfileOrCollectionUrl(url))
        a << "--no-playlist"; // link một video nằm trong danh sách phát: chỉ lấy video đó
    a << extra;
    a << "--" << url; // sau "--" chỉ còn địa chỉ
    return a;
}

ResolveResult parseYtDlpJson(const QByteArray& json, const QString& sourceUrl, int maxItems)
{
    ResolveResult res;
    res.sourceUrl = sourceUrl;
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
    {
        res.error = "yt-dlp không trả về dữ liệu đọc được.";
        return res;
    }
    const QJsonObject root = doc.object();
    const QString extractor = root.value("extractor_key").toString();
    const QString note = extractor.isEmpty() ? QStringLiteral("yt-dlp") : QStringLiteral("yt-dlp (%1)").arg(extractor);

    auto nameFor = [](const QString& title, const QString& ext) {
        QString n = FileDownloaderInternal::sanitizeFileName(title);
        if (n.isEmpty())
            return QString();
        return ext.isEmpty() ? n : n + "." + ext;
    };

    const QString type = root.value("_type").toString();
    if (type == "playlist" || root.contains("entries"))
    {
        const QJsonArray entries = root.value("entries").toArray();
        res.isCollection = true;
        for (const QJsonValue& v : entries)
        {
            if (!v.isObject())
                continue;
            const QJsonObject e = v.toObject();
            QString url = e.value("url").toString();
            if (url.isEmpty())
                url = e.value("webpage_url").toString();
            if (url.isEmpty() && e.value("ie_key").toString() == "Youtube" && !e.value("id").toString().isEmpty())
                url = "https://www.youtube.com/watch?v=" + e.value("id").toString();
            if (!FileDownloaderInternal::isHttpUrl(url))
                continue;
            if (res.items.size() >= maxItems)
            {
                res.truncated = true;
                break;
            }
            ResolvedMedia m;
            m.kind = ResolvedMedia::Kind::YtDlp;
            m.url = url;
            m.title = e.value("title").toString();
            m.fileName = nameFor(m.title, QString());
            m.mediaType = "Video";
            const double size = e.value("filesize_approx").toDouble(e.value("filesize").toDouble(-1));
            m.size = size > 0 ? static_cast<qint64>(size) : -1;
            m.referer = sourceUrl;
            m.note = note;
            res.items << m;
        }
        if (res.items.isEmpty())
            res.error = "Danh sách/hồ sơ này không có mục nào tải được (rỗng hoặc cần đăng nhập để xem).";
        return res;
    }

    // Video đơn.
    ResolvedMedia m;
    m.kind = ResolvedMedia::Kind::YtDlp;
    m.url = root.value("webpage_url").toString();
    if (!FileDownloaderInternal::isHttpUrl(m.url))
        m.url = sourceUrl;
    m.title = root.value("title").toString();
    const QString ext = root.value("ext").toString();
    m.fileName = nameFor(m.title, ext);
    const bool audioOnly = root.value("vcodec").toString() == "none";
    m.mediaType = audioOnly ? "Âm thanh" : "Video";
    // Bài đăng ẢNH của TikTok: yt-dlp chỉ thấy nhạc nền (định dạng duy nhất là mp3, không có hình) - tải ra chỉ được mp3.
    // Báo địa chỉ /photo/ để nơi gọi lấy ảnh thật bằng trình duyệt ẩn.
    if (audioOnly && extractor.startsWith(QStringLiteral("TikTok")))
        res.photoPostUrl = tiktokPhotoUrlFromVideoUrl(m.url);
    double size = root.value("filesize").toDouble(root.value("filesize_approx").toDouble(-1));
    if (size <= 0)
    {
        for (const QJsonValue& f : root.value("formats").toArray())
        {
            const QJsonObject fo = f.toObject();
            size = qMax(size, fo.value("filesize").toDouble(fo.value("filesize_approx").toDouble(-1)));
        }
    }
    m.size = size > 0 ? static_cast<qint64>(size) : -1;
    m.referer = sourceUrl;
    m.note = note;
    res.items << m;
    return res;
}

QString translateYtDlpError(const QString& stderrText, bool* unsupported)
{
    if (unsupported)
        *unsupported = false;
    const QString low = stderrText.toLower();
    if (low.contains(QStringLiteral("unsupported url")))
    {
        if (unsupported)
            *unsupported = true;
        return "yt-dlp không hỗ trợ địa chỉ này.";
    }
    if (low.contains(QStringLiteral("http error 403")) || low.contains(QStringLiteral("forbidden")))
        return "Trang chặn truy cập tự động (HTTP 403).";
    if (low.contains(QStringLiteral("http error 404")) || low.contains(QStringLiteral("not found")))
        return "Không tìm thấy trang/video (HTTP 404).";
    if (low.contains(QStringLiteral("login")) || low.contains(QStringLiteral("sign in")) ||
        low.contains(QStringLiteral("cookies")) || low.contains(QStringLiteral("private")) ||
        low.contains(QStringLiteral("authentication")))
        return "Cần đăng nhập - đặt cookie trình duyệt ở tab \"Cài đặt\" rồi thử lại.";
    if (low.contains(QStringLiteral("geo")) || low.contains(QStringLiteral("not available in your country")))
        return "Nội dung bị giới hạn khu vực.";
    if (low.contains(QStringLiteral("video unavailable")) || low.contains(QStringLiteral("this video is not available")) ||
        low.contains(QStringLiteral("has been removed")))
        return "Video không còn khả dụng.";
    if (low.contains(QStringLiteral("unable to extract")))
        return "yt-dlp không đọc được trang này (có thể cần đăng nhập hoặc trang đã đổi cấu trúc).";
    if (low.contains(QStringLiteral("timed out")) || low.contains(QStringLiteral("timeout")))
        return "Hết thời gian chờ khi truy cập trang.";
    // Lỗi khác: lấy dòng ERROR cuối cùng cho dễ đọc.
    QString last;
    for (const QString& line : stderrText.split('\n'))
        if (line.trimmed().startsWith(QStringLiteral("ERROR")))
            last = line.trimmed();
    if (last.isEmpty())
        last = stderrText.trimmed().section('\n', -1);
    return last.isEmpty() ? QStringLiteral("yt-dlp thất bại.") : last.left(300);
}

// ---------------------------------------------------------------------------------------------- Khác
QString fileNameFromContentDisposition(const QByteArray& header)
{
    const QString s = QString::fromUtf8(header);
    static const QRegularExpression starRx(QStringLiteral("filename\\*\\s*=\\s*([^']*)'[^']*'([^;]+)"),
                                           QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression quotedRx(QStringLiteral("filename\\s*=\\s*\"([^\"]+)\""), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression plainRx(QStringLiteral("filename\\s*=\\s*([^;\\s]+)"), QRegularExpression::CaseInsensitiveOption);
    QString name;
    if (auto m = starRx.match(s); m.hasMatch())
        name = QUrl::fromPercentEncoding(m.captured(2).trimmed().toUtf8());
    else if (auto q = quotedRx.match(s); q.hasMatch())
        name = q.captured(1);
    else if (auto p = plainRx.match(s); p.hasMatch())
        name = p.captured(1);
    return FileDownloaderInternal::sanitizeFileName(name);
}

QString friendlyTypeLabel(const QString& contentType, const QString& url)
{
    const QString ct = contentType.section(';', 0, 0).trimmed().toLower();
    if (ct.startsWith(QStringLiteral("image/"))) return "Ảnh";
    if (ct.startsWith(QStringLiteral("video/")) || ct.contains(QStringLiteral("mpegurl")) || ct.contains(QStringLiteral("dash+xml"))) return "Video";
    if (ct.startsWith(QStringLiteral("audio/"))) return "Âm thanh";
    if (ct == "application/pdf" || ct.contains(QStringLiteral("msword")) || ct.contains(QStringLiteral("officedocument")) ||
        ct.contains(QStringLiteral("ms-excel")) || ct.contains(QStringLiteral("ms-powerpoint")) || ct == "application/zip" ||
        ct.contains(QStringLiteral("rar")) || ct.contains(QStringLiteral("7z")) || ct == "text/plain" || ct == "text/csv")
        return "Tài liệu";
    switch (PageMediaScannerInternal::classifyByExtension(url))
    {
        case MediaLink::Type::Image: return "Ảnh";
        case MediaLink::Type::Video: return "Video";
        case MediaLink::Type::Audio: return "Âm thanh";
        case MediaLink::Type::Document: return "Tài liệu";
        default: break;
    }
    if (isManifestUrl(url))
        return "Video";
    return "Tệp";
}
} // namespace LinkResolverCore
