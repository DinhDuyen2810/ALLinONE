#include "LinkResolver.h"

#include "CookieFile.h"
#include "FileDownloader.h"
#include "HeadlessBrowser.h"
#include "LinkResolverCore.h"
#include "PageMediaScanner.h"
#include "YtDlpController.h"
#include "core/Logger.h"
#include "core/WinProcessTree.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <memory>
#include <vector>

namespace
{
constexpr int kTransferTimeoutMs = 25000;
const char* kBrowserUa =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/126.0.0.0 Safari/537.36";

QString extForContentType(const QString& contentType)
{
    const QString ct = contentType.section(';', 0, 0).trimmed().toLower();
    static const QHash<QString, QString> map = {
        {"image/jpeg", ".jpg"}, {"image/png", ".png"}, {"image/gif", ".gif"}, {"image/webp", ".webp"},
        {"video/mp4", ".mp4"}, {"video/webm", ".webm"}, {"audio/mpeg", ".mp3"}, {"audio/mp4", ".m4a"},
        {"application/pdf", ".pdf"}, {"application/zip", ".zip"},
    };
    return map.value(ct);
}

/// Trình phát nhúng mà yt-dlp xử lý được - đưa thẳng cho yt-dlp thay vì quét HTML của khung.
bool isYtDlpEmbedHost(const QString& url)
{
    const QString host = QUrl(url).host().toLower();
    for (const char* d : {"youtube.com", "youtube-nocookie.com", "youtu.be", "vimeo.com", "dailymotion.com", "streamable.com"})
        if (host == d || host.endsWith(QString(".") + d))
            return true;
    return false;
}
} // namespace

LinkResolver::LinkResolver(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

LinkResolver::~LinkResolver()
{
    m_canceled = true;
    if (m_reply)
    {
        m_reply->disconnect(this);
        m_reply->abort();
    }
    if (m_ytProcess && m_ytProcess->state() != QProcess::NotRunning)
    {
        m_ytProcess->disconnect(this);
        const qint64 pid = m_ytProcess->processId();
        const std::vector<qint64> kids = pid > 0 ? WinProcessTree::findDescendants(pid) : std::vector<qint64>{};
        m_ytProcess->kill();
        m_ytProcess->waitForFinished(2000);
        WinProcessTree::terminateProcessList(kids);
    }
}

void LinkResolver::resetState()
{
    m_finalUrl.clear();
    m_html.clear();
    m_pageTitle.clear();
    m_iframes.clear();
    m_pageStatus = 0;
    m_challenge = false;
    m_loginWall = false;
    m_ytError.clear();
    m_ytUnsupported = false;
    m_blockMessage.clear();
    m_browserError.clear();
    m_warning.clear();
    m_ytAttempts = 0;
    m_workUrl.clear();
    m_browserAttempt = 0;
    m_browserFirstError.clear();
    m_browserRefine = false;
    m_pendingCollection = ResolveResult();
    m_slots.clear();
    m_expFallback.clear();
}

void LinkResolver::logOutcome(const QString& text) const
{
    // Chỉ ghi máy chủ + đường dẫn (không ghi chuỗi truy vấn - có thể chứa mã/token) để chẩn đoán "vì sao link này không ra".
    QUrl u(m_url);
    u.setQuery(QString());
    u.setFragment(QString());
    Logger::instance().info("Downloader", QString("Phân giải %1 -> %2").arg(u.toString().left(160), text));
}

void LinkResolver::finishOk(const QList<ResolvedMedia>& items, bool collection, bool truncated)
{
    if (m_done)
        return;
    m_done = true;
    m_running = false;
    logOutcome(items.isEmpty() ? QString("không có mục (%1)").arg(bestFailureMessage().left(200))
                               : QString("%1 mục%2").arg(items.size()).arg(m_warning.isEmpty() ? QString() : QString(" (có cảnh báo)")));
    ResolveResult r;
    r.sourceUrl = m_url;
    r.items = items;
    r.isCollection = collection;
    r.truncated = truncated;
    r.warning = m_warning;
    if (items.isEmpty())
        r.error = bestFailureMessage();
    emit finished(r);
}

void LinkResolver::finishError(const QString& message)
{
    if (m_done)
        return;
    m_done = true;
    m_running = false;
    ResolveResult r;
    r.sourceUrl = m_url;
    r.error = message.isEmpty() ? bestFailureMessage() : message;
    logOutcome("lỗi: " + r.error.left(240));
    emit finished(r);
}

QString LinkResolver::bestFailureMessage() const
{
    if (!m_blockMessage.isEmpty())
        return m_blockMessage;
    if (m_challenge)
        return "Trang được bảo vệ chống truy cập tự động (Cloudflare) hoặc yêu cầu đăng nhập/trả phí - không lấy được tệp. "
               "Nếu bạn có quyền truy cập: mở trang bằng trình duyệt (bấm ↗) rồi tải bằng chính tài khoản của bạn.";
    if (m_loginWall)
        return "Trang yêu cầu đăng nhập - đặt cookie trình duyệt ở tab \"Cài đặt\" (nếu yt-dlp hỗ trợ trang này) hoặc dán link tệp trực tiếp.";
    if (!m_ytError.isEmpty() && !m_ytUnsupported)
        return m_ytError;
    QString msg = "Không tìm thấy tệp/video nào tải được ở địa chỉ này (đã thử yt-dlp, quét mã nguồn trang";
    msg += m_opts.useBrowser ? " và trình duyệt ẩn)." : ").";
    if (!m_browserError.isEmpty())
        msg += " " + m_browserError;
    return msg + " Nếu trang cần đăng nhập, hãy dán link tệp trực tiếp.";
}

void LinkResolver::cancel()
{
    if (m_done)
        return;
    m_canceled = true;
    if (m_reply)
    {
        QNetworkReply* r = m_reply;
        m_reply = nullptr;
        r->disconnect(this);
        r->abort();
        r->deleteLater();
    }
    if (m_ytProcess && m_ytProcess->state() != QProcess::NotRunning)
    {
        m_ytProcess->disconnect(this);
        const qint64 pid = m_ytProcess->processId();
        const std::vector<qint64> kids = pid > 0 ? WinProcessTree::findDescendants(pid) : std::vector<qint64>{};
        m_ytProcess->kill();
        WinProcessTree::terminateProcessList(kids);
    }
    if (m_ytTimeout)
        m_ytTimeout->stop();
    if (m_browser)
    {
        m_browser->disconnect(this);
        m_browser->cancel();
    }
    for (LinkResolver* child : std::as_const(m_children))
    {
        child->disconnect(this);
        child->cancel();
        child->deleteLater();
    }
    m_children.clear();
    finishError("Đã hủy.");
}

void LinkResolver::resolve(const QString& url)
{
    if (m_running)
        return;
    resetState();
    m_url = url.trimmed();
    m_running = true;
    m_done = false;
    m_canceled = false;
    const QString stamp = CookieFile::stamp(m_opts.cookieFile);
    if (stamp != m_cookieStamp)
    {
        CookieFile::applyTo(m_nam, m_opts.cookieFile);
        m_cookieStamp = stamp;
    }

    if (!FileDownloaderInternal::isHttpUrl(m_url))
    {
        // Phát tín hiệu bất đồng bộ để nơi gọi kịp nối slot sau resolve().
        QTimer::singleShot(0, this, [this]() { finishError("Địa chỉ không hợp lệ - chỉ hỗ trợ http:// hoặc https://."); });
        return;
    }
    emit progress("Đang kiểm tra liên kết...");

    QString exportUrl, ext, driveId;
    if (LinkResolverCore::googleDocsExportUrl(m_url, &exportUrl, &ext))
        stageGoogleDocs(exportUrl, ext);
    else if (LinkResolverCore::parseDriveFileId(m_url, &driveId))
        stageDrive(driveId);
    else
        stageProbe();
}

// ------------------------------------------------------------------------------------------------ Mạng
void LinkResolver::fetch(const QString& url, const QString& referer, std::function<void(const Fetch&)> done)
{
    QNetworkRequest req((QUrl(url)));
    req.setHeader(QNetworkRequest::UserAgentHeader, kBrowserUa);
    req.setRawHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
    req.setRawHeader("Accept-Language", "vi-VN,vi;q=0.9,en;q=0.8");
    if (!referer.isEmpty())
        req.setRawHeader("Referer", referer.toUtf8());
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(kTransferTimeoutMs);

    QNetworkReply* reply = m_nam->get(req);
    m_reply = reply;

    struct State
    {
        QByteArray body;
        bool headerChecked{false};
        bool isFile{false};
        bool truncated{false};
        bool abortScheduled{false};
    };
    auto st = std::make_shared<State>();
    auto scheduleAbort = [reply, st]() {
        if (st->abortScheduled)
            return;
        st->abortScheduled = true;
        QTimer::singleShot(0, reply, &QNetworkReply::abort);
    };

    connect(reply, &QNetworkReply::readyRead, this, [reply, st, scheduleAbort]() {
        if (!st->headerChecked)
        {
            st->headerChecked = true;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QString ct = reply->header(QNetworkRequest::ContentTypeHeader).toString();
            const QByteArray cd = reply->rawHeader("Content-Disposition");
            const bool attachment = cd.toLower().contains("attachment");
            if (status >= 200 && status < 300 && (attachment || !PageMediaScannerInternal::isHtmlContentType(ct)))
                st->isFile = true;
        }
        if (st->isFile || st->truncated)
        {
            reply->readAll();
            scheduleAbort();
            return;
        }
        st->body += reply->readAll();
        if (st->body.size() >= PageMediaScannerInternal::kMaxPageBytes)
        {
            st->body.truncate(PageMediaScannerInternal::kMaxPageBytes);
            st->truncated = true;
            scheduleAbort();
        }
    });

    connect(reply, &QNetworkReply::finished, this, [this, reply, st, done]() {
        reply->deleteLater();
        if (m_reply != reply)
            return; // đã bị cancel()/thay thế
        m_reply = nullptr;
        if (m_done || m_canceled)
            return;

        Fetch f;
        f.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        f.contentType = reply->header(QNetworkRequest::ContentTypeHeader).toString();
        f.finalUrl = reply->url().toString();
        f.disposition = reply->rawHeader("Content-Disposition");
        const QVariant len = reply->header(QNetworkRequest::ContentLengthHeader);
        f.length = len.isValid() ? len.toLongLong() : -1;
        f.isFile = st->isFile;
        if (!st->isFile)
            f.html = QString::fromUtf8(st->body);
        const bool selfAborted = st->isFile || st->truncated;
        if (!selfAborted && f.status == 0 && reply->error() != QNetworkReply::NoError)
            f.error = reply->error() == QNetworkReply::OperationCanceledError
                          ? QString("máy chủ không phản hồi trong %1 giây").arg(kTransferTimeoutMs / 1000)
                          : reply->errorString();
        done(f);
    });
}

ResolvedMedia LinkResolver::fileItem(const QString& url, const Fetch& f, const QString& referer) const
{
    ResolvedMedia m;
    m.kind = ResolvedMedia::Kind::HttpFile;
    m.url = url;
    QString name = LinkResolverCore::fileNameFromContentDisposition(f.disposition);
    if (name.isEmpty())
        name = FileDownloaderInternal::fileNameFromUrl(f.finalUrl.isEmpty() ? url : f.finalUrl);
    if (name.isEmpty() || !name.contains('.'))
        name = (name.isEmpty() ? QString("tai-ve") : name) + extForContentType(f.contentType);
    m.fileName = name;
    m.title = name;
    m.contentType = f.contentType;
    // Phân loại theo Content-Type, không rõ thì theo đuôi TÊN TỆP thật (Content-Disposition thường là nơi duy nhất có đuôi).
    m.mediaType = LinkResolverCore::friendlyTypeLabel(f.contentType, url);
    if (m.mediaType == "Tệp")
        m.mediaType = LinkResolverCore::friendlyTypeLabel(f.contentType, name);
    m.size = f.length;
    m.referer = referer;
    m.note = "tệp trực tiếp";
    return m;
}

// ------------------------------------------------------------------------------------------------ Bậc 1: Google
void LinkResolver::stageGoogleDocs(const QString& exportUrl, const QString& ext)
{
    emit progress("Đang xuất tài liệu Google...");
    fetch(exportUrl, QString(), [this, exportUrl, ext](const Fetch& f) {
        if (f.isFile)
        {
            ResolvedMedia m = fileItem(exportUrl, f, QString());
            if (!m.fileName.contains('.'))
                m.fileName += "." + ext;
            m.note = "Google Docs (xuất " + ext.toUpper() + ")";
            finishOk({m});
            return;
        }
        if (!f.error.isEmpty())
            finishError("Không truy cập được tài liệu Google: " + f.error);
        else
            finishError("Tài liệu Google không công khai (cần quyền xem/tải) hoặc không cho xuất.");
    });
}

QString LinkResolver::driveDirectUrl(const QString& fileId) const
{
    if (m_opts.driveBaseOverride.isEmpty())
        return LinkResolverCore::driveDirectUrl(fileId);
    return m_opts.driveBaseOverride + "/download?id=" + fileId + "&export=download&confirm=t";
}

QString LinkResolver::driveViewUrl(const QString& fileId) const
{
    return (m_opts.driveBaseOverride.isEmpty() ? QStringLiteral("https://drive.google.com") : m_opts.driveBaseOverride) +
           "/file/d/" + fileId + "/view";
}

void LinkResolver::stageDrive(const QString& fileId)
{
    emit progress("Đang kiểm tra tệp Google Drive...");
    const QString direct = driveDirectUrl(fileId);
    fetch(direct, QString(), [this, direct, fileId](const Fetch& f) {
        if (f.isFile)
        {
            ResolvedMedia m = fileItem(direct, f, QString());
            m.note = "Google Drive";
            finishOk({m});
            return;
        }
        if (!f.error.isEmpty())
        {
            finishError("Không truy cập được Google Drive: " + f.error);
            return;
        }
        using namespace LinkResolverCore;
        const DriveBlock block = classifyDriveHtml(f.html);
        if (block == DriveBlock::NeedsConfirm)
        {
            const QString confirm = driveConfirmUrl(f.html, f.finalUrl.isEmpty() ? direct : f.finalUrl);
            if (!confirm.isEmpty())
            {
                emit progress("Google Drive yêu cầu xác nhận tải - đang xác nhận...");
                fetch(confirm, QString(), [this, confirm](const Fetch& f2) {
                    if (f2.isFile)
                    {
                        ResolvedMedia m = fileItem(confirm, f2, QString());
                        m.note = "Google Drive (đã xác nhận)";
                        finishOk({m});
                        return;
                    }
                    finishError(f2.error.isEmpty() ? driveBlockMessage(classifyDriveHtml(f2.html)).isEmpty()
                                                         ? QString("Google Drive không cho tải tệp này.")
                                                         : driveBlockMessage(classifyDriveHtml(f2.html))
                                                   : "Không truy cập được Google Drive: " + f2.error);
                });
                return;
            }
        }
        QString msg = driveBlockMessage(block);
        if (block == DriveBlock::NoPermission)
        {
            // Chủ tệp tắt "tải xuống" nhưng người xem vẫn XEM được: viewer của Drive gửi từng trang về trình duyệt - chụp lại
            // và ghép PDF (cần trình duyệt ẩn). Lý do ERR cũ vẫn được giữ làm đầu thông báo nếu không làm được.
            stageDriveViewer(fileId, msg);
            return;
        }
        if (msg.isEmpty())
            msg = "Google Drive không trả về tệp (tệp bị xóa, riêng tư hoặc không cho tải).";
        else if (block == DriveBlock::SignIn)
            msg += m_opts.cookieFile.isEmpty()
                       ? " Nếu bạn là chủ sở hữu/người chỉnh sửa: chọn tệp cookies.txt (xuất từ trình duyệt đã đăng nhập đúng tài khoản Google đó) "
                         "ở tab Cài đặt rồi \"Kiểm tra lại\"."
                       : " (Đã dùng tệp cookies.txt của bạn nhưng tài khoản đó vẫn không có quyền tải, hoặc cookie đã hết hạn.)";
        finishError(msg);
    });
}

void LinkResolver::stageDriveViewer(const QString& fileId, const QString& blockMessage)
{
    QString hint = m_opts.cookieFile.isEmpty()
                       ? " Nếu bạn là chủ sở hữu/người chỉnh sửa: chọn tệp cookies.txt (xuất từ trình duyệt đã đăng nhập đúng tài khoản Google đó) "
                         "ở tab Cài đặt rồi \"Kiểm tra lại\"."
                       : " (Đã dùng tệp cookies.txt của bạn nhưng tài khoản đó vẫn không có quyền tải, hoặc cookie đã hết hạn.)";
    if (!m_opts.useBrowser)
    {
        finishError(blockMessage + hint + " Hoặc bật \"Dùng trình duyệt ẩn\" ở tab Cài đặt để thử lấy qua chế độ xem (chụp từng trang thành PDF).");
        return;
    }
    if (HeadlessBrowser::findBrowserExecutable().isEmpty())
    {
        finishError(blockMessage + hint + " (Không tìm thấy Microsoft Edge/Google Chrome để thử lấy qua chế độ xem.)");
        return;
    }
    emit progress("Chủ tệp tắt tải xuống - đang kiểm tra chế độ xem...");
    const QString viewUrl = driveViewUrl(fileId);
    fetch(viewUrl, QString(), [this, fileId, blockMessage, viewUrl](const Fetch& f) {
        if (!f.error.isEmpty())
        {
            finishError(blockMessage + " (Không mở được trang xem: " + f.error + ")");
            return;
        }
        if (f.finalUrl.contains("accounts.google.com") || f.status == 401 || f.status == 403)
        {
            finishError(blockMessage + " Tệp cũng không xem được khi chưa đăng nhập (chưa chia sẻ \"Bất kỳ ai có đường liên kết\").");
            return;
        }
        if (f.status >= 400)
        {
            finishError(blockMessage + QString(" (Trang xem trả HTTP %1 - tệp có thể đã bị xóa hoặc riêng tư.)").arg(f.status));
            return;
        }
        const LinkResolverCore::DriveViewerName vn = LinkResolverCore::driveViewerName(LinkResolverCore::htmlTitle(f.html));
        if (!vn.pageable)
        {
            finishError(blockMessage + QString(" Tệp .%1 không phải tài liệu xem theo trang nên không thể lấy qua chế độ xem.").arg(vn.extension));
            return;
        }
        QString base = FileDownloaderInternal::sanitizeFileName(vn.baseName);
        if (base.isEmpty())
            base = "drive_" + fileId.left(12);
        if (base.endsWith(".pdf", Qt::CaseInsensitive))
            base.chop(4);

        ResolvedMedia m;
        m.kind = ResolvedMedia::Kind::DriveViewer;
        m.url = viewUrl;
        m.title = base;
        m.fileName = base + ".pdf";
        m.mediaType = "Tài liệu";
        m.contentType = "application/pdf";
        m.note = "Google Drive (chế độ xem - chụp từng trang)";
        m_warning = "Chủ tệp đã tắt tải xuống: PDF được ghép từ ảnh từng trang ở chế độ xem (không chọn/tìm kiếm được chữ), cần Edge/Chrome, "
                    "tài liệu dài mất vài phút. Chỉ dùng cho tài liệu bạn có quyền sử dụng - không khuyến khích dùng cho mục đích vi phạm "
                    "pháp luật hoặc bản quyền.";
        finishOk({m});
    });
}

// ------------------------------------------------------------------------------------------------ Bậc 2: một yêu cầu
void LinkResolver::stageProbe()
{
    fetch(m_url, QString(), [this](const Fetch& f) {
        if (!f.error.isEmpty())
        {
            finishError("Không truy cập được địa chỉ này: " + f.error + ".");
            return;
        }
        if (f.isFile)
        {
            finishOk({fileItem(m_url, f, QString())});
            return;
        }
        m_pageStatus = f.status;
        m_finalUrl = f.finalUrl.isEmpty() ? m_url : f.finalUrl;
        m_html = f.html;
        if (f.status == 404 || f.status == 410)
        {
            finishError(QString("Không tìm thấy trang/tệp (HTTP %1).").arg(f.status));
            return;
        }
        m_challenge = LinkResolverCore::looksLikeChallengePage(m_html) || f.status == 403;
        m_loginWall = !m_challenge && LinkResolverCore::looksLikeLoginWall(m_html);
        stagePage();
    });
}

// ------------------------------------------------------------------------------------------------ Bậc 3-5: trang
void LinkResolver::stagePage()
{
    // Bài đăng ảnh (Facebook/TikTok) yt-dlp KHÔNG hỗ trợ (đã xác nhận: "Unsupported URL") - bỏ qua cho đỡ tốn thời gian.
    if (!m_opts.useYtDlp || LinkResolverCore::isPhotoPostUrl(workUrl()))
        stageStatic();
    else
        stageYtDlp();
}

void LinkResolver::stageYtDlp()
{
    QString missing;
    if (!YtDlpController::isBundleAvailable(&missing))
    {
        m_ytError = "Thiếu yt-dlp (" + missing + ").";
        m_ytUnsupported = true; // không phải lỗi của trang - cứ tiếp tục bậc sau
        stageStatic();
        return;
    }
    ++m_ytAttempts;
    emit progress(m_ytAttempts > 1 ? QString("Đang hỏi yt-dlp (lần %1)...").arg(m_ytAttempts) : QString("Đang hỏi yt-dlp..."));

    if (m_ytProcess)
        m_ytProcess->deleteLater();
    m_ytProcess = new QProcess(this);
    m_ytProcess->setProgram(YtDlpController::ytDlpExecutablePath());
    m_ytProcess->setArguments(LinkResolverCore::ytDlpAnalyzeArguments(m_url, m_opts.maxItems, m_opts.ytDlpExtraArgs));
    QProcess* proc = m_ytProcess;
    auto out = std::make_shared<QByteArray>();
    auto err = std::make_shared<QByteArray>();
    auto handled = std::make_shared<bool>(false);

    connect(proc, &QProcess::readyReadStandardOutput, this, [proc, out]() { *out += proc->readAllStandardOutput(); });
    connect(proc, &QProcess::readyReadStandardError, this, [proc, err]() { *err += proc->readAllStandardError(); });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, handled](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart || *handled || m_done || m_canceled)
            return;
        *handled = true;
        m_ytError = "Không khởi chạy được yt-dlp (" + proc->errorString() + ").";
        m_ytUnsupported = true;
        stageStatic();
    });
    connect(proc, &QProcess::finished, this, [this, out, err, handled](int exitCode, QProcess::ExitStatus status) {
        if (*handled || m_done || m_canceled)
            return;
        *handled = true;
        if (m_ytTimeout)
            m_ytTimeout->stop();
        if (status == QProcess::NormalExit && exitCode == 0 && !out->isEmpty())
        {
            const ResolveResult r = LinkResolverCore::parseYtDlpJson(*out, m_url, m_opts.maxItems);
            if (r.ok())
            {
                // Bài đăng ẢNH của TikTok: yt-dlp chỉ thấy nhạc nền - lấy ảnh thật bằng cách dựng trang /photo/.
                if (!r.photoPostUrl.isEmpty())
                {
                    m_workUrl = r.photoPostUrl;
                    m_html.clear();
                    m_finalUrl.clear();
                    m_iframes.clear();
                    m_challenge = false;
                    emit progress("Bài đăng ảnh TikTok - đang lấy ảnh...");
                    stageStatic();
                    return;
                }
                // Hồ sơ TikTok: yt-dlp liệt kê bài ảnh dưới dạng /video/... - phân loại bằng trình duyệt ẩn.
                if (r.isCollection && m_opts.useBrowser && m_opts.expandPosts && LinkResolverCore::siteOf(m_url) == LinkResolverCore::Site::TikTok)
                {
                    refineTikTokCollection(r);
                    return;
                }
                finishOk(r.items, r.isCollection, r.truncated);
                return;
            }
            m_ytError = r.error;
        }
        else
        {
            const QString raw = QString::fromUtf8(*err);
            m_ytError = LinkResolverCore::translateYtDlpError(raw, &m_ytUnsupported);
            // Hồ sơ TikTok/YouTube thỉnh thoảng lỗi "Unable to extract ..." rồi lượt gọi kế tiếp lại chạy được (đo thật:
            // cùng một lệnh, lần chạy được lần lỗi) - thử lại tối đa 3 lượt trước khi chuyển sang bậc sau.
            if (m_ytAttempts < 3 && !m_ytUnsupported && LinkResolverCore::isLikelyProfileOrCollectionUrl(m_url) &&
                raw.contains(QStringLiteral("Unable to extract"), Qt::CaseInsensitive))
            {
                QTimer::singleShot(1500 * m_ytAttempts, this, [this]() {
                    if (!m_done && !m_canceled)
                        stageYtDlp();
                });
                return;
            }
        }
        stageStatic();
    });

    if (!m_ytTimeout)
    {
        m_ytTimeout = new QTimer(this);
        m_ytTimeout->setSingleShot(true);
    }
    m_ytTimeout->disconnect(this);
    connect(m_ytTimeout, &QTimer::timeout, this, [this, proc, handled]() {
        if (*handled || m_done || m_canceled || proc->state() == QProcess::NotRunning)
            return;
        *handled = true;
        const qint64 pid = proc->processId();
        const std::vector<qint64> kids = pid > 0 ? WinProcessTree::findDescendants(pid) : std::vector<qint64>{};
        proc->disconnect(this);
        proc->kill();
        WinProcessTree::terminateProcessList(kids);
        m_ytError = "Hết thời gian chờ yt-dlp (trang phản hồi quá chậm).";
        m_ytUnsupported = false;
        stageStatic();
    });
    m_ytTimeout->start(m_opts.ytDlpTimeoutMs);
    proc->start();
}

void LinkResolver::stageStatic()
{
    if (m_done || m_canceled)
        return;
    emit progress("Đang quét mã nguồn trang...");
    QList<ResolvedMedia> items;
    if (!m_html.isEmpty() && !m_challenge)
    {
        const LinkResolverCore::DeepScan scan = LinkResolverCore::deepScanHtml(m_html, m_finalUrl.isEmpty() ? m_url : m_finalUrl);
        m_pageTitle = scan.title;
        m_iframes = scan.iframes;
        const bool photoPost = LinkResolverCore::isPhotoPostUrl(workUrl());
        QSet<QString> seen;
        for (const MediaLink& l : scan.media)
        {
            const bool primary = l.type == MediaLink::Type::Video || l.type == MediaLink::Type::Audio ||
                                 l.type == MediaLink::Type::Document;
            if (!primary && !(photoPost && l.type == MediaLink::Type::Image))
                continue;
            if (seen.contains(l.url))
                continue;
            seen.insert(l.url);
            ResolvedMedia m;
            m.url = l.url;
            m.referer = m_finalUrl.isEmpty() ? m_url : m_finalUrl;
            m.note = "mã nguồn trang";
            m.mediaType = LinkResolverCore::friendlyTypeLabel(QString(), l.url);
            if (LinkResolverCore::isManifestUrl(l.url))
            {
                m.kind = ResolvedMedia::Kind::YtDlp;
                m.mediaType = "Video";
                m.title = m_pageTitle;
                const QString base = FileDownloaderInternal::sanitizeFileName(m_pageTitle);
                m.fileName = base.isEmpty() ? QString() : base + ".mp4";
            }
            else
            {
                m.kind = ResolvedMedia::Kind::HttpFile;
                m.fileName = FileDownloaderInternal::fileNameFromUrl(l.url);
                m.title = m.fileName;
            }
            items << m;
        }
    }
    if (!items.isEmpty())
    {
        finishOk(items);
        return;
    }
    stageFrames(0);
}

void LinkResolver::stageFrames(int index)
{
    if (m_done || m_canceled)
        return;
    // Khung nhúng: trình phát yt-dlp biết (YouTube/Vimeo...) đưa thẳng cho yt-dlp; còn lại tải HTML của khung rồi quét.
    while (index < m_iframes.size() && index < 3)
    {
        const QString frame = m_iframes[index];
        if (isYtDlpEmbedHost(frame))
        {
            ResolvedMedia m;
            m.kind = ResolvedMedia::Kind::YtDlp;
            m.url = frame;
            m.title = m_pageTitle;
            m.fileName = FileDownloaderInternal::sanitizeFileName(m_pageTitle);
            m.mediaType = "Video";
            m.referer = m_finalUrl.isEmpty() ? m_url : m_finalUrl;
            m.note = "video nhúng trong trang";
            finishOk({m});
            return;
        }
        emit progress(QString("Đang quét khung nhúng (%1/%2)...").arg(index + 1).arg(qMin(3, int(m_iframes.size()))));
        fetch(frame, m_finalUrl.isEmpty() ? m_url : m_finalUrl, [this, index, frame](const Fetch& f) {
            if (!f.isFile && !f.html.isEmpty() && !LinkResolverCore::looksLikeChallengePage(f.html))
            {
                const LinkResolverCore::DeepScan scan = LinkResolverCore::deepScanHtml(f.html, f.finalUrl.isEmpty() ? frame : f.finalUrl);
                QList<ResolvedMedia> items;
                QSet<QString> seen;
                for (const MediaLink& l : scan.media)
                {
                    if (l.type != MediaLink::Type::Video && l.type != MediaLink::Type::Audio)
                        continue;
                    if (seen.contains(l.url))
                        continue;
                    seen.insert(l.url);
                    ResolvedMedia m;
                    m.url = l.url;
                    m.referer = frame; // CDN thường chỉ cho phát khi Referer là trang trình phát
                    m.note = "khung nhúng";
                    m.mediaType = "Video";
                    if (LinkResolverCore::isManifestUrl(l.url))
                    {
                        m.kind = ResolvedMedia::Kind::YtDlp;
                        const QString base = FileDownloaderInternal::sanitizeFileName(m_pageTitle);
                        m.title = m_pageTitle;
                        m.fileName = base.isEmpty() ? QString() : base + ".mp4";
                    }
                    else
                    {
                        m.fileName = FileDownloaderInternal::fileNameFromUrl(l.url);
                        m.title = m.fileName;
                    }
                    items << m;
                }
                if (!items.isEmpty())
                {
                    finishOk(items);
                    return;
                }
            }
            stageFrames(index + 1);
        });
        return;
    }
    stageBrowser();
}

void LinkResolver::stageBrowser()
{
    if (m_done || m_canceled)
        return;
    if (!m_opts.useBrowser)
    {
        finishError(QString());
        return;
    }
    emit progress("Đang dựng trang bằng trình duyệt ẩn (có thể mất ~20 giây)...");
    m_browserRefine = false;
    m_browserAttempt = 0;
    m_browserFirstError.clear();
    startBrowser(workUrl());
}

void LinkResolver::startBrowser(const QString& url)
{
    if (!m_browser)
        m_browser = new HeadlessBrowser(this);
    m_browser->disconnect(this);
    connect(m_browser, &HeadlessBrowser::finished, this, &LinkResolver::onBrowserFinished);
    m_browser->start(url, m_opts.browserTimeoutMs, m_browserAttempt > 0);
}

void LinkResolver::onBrowserFinished(bool ok, QString dom, QByteArray netLog, QString error)
{
    if (m_done || m_canceled)
        return;
    if (!ok)
    {
        // Lần đầu thất bại vì chế độ ẩn mới không chạy được (chính sách máy, phiên bản cũ...) thì thử chế độ ẩn cũ. Không thử
        // lại khi chẳng có trình duyệt, hết giờ (mạng chậm - thử lại chỉ chờ thêm) hay bị hủy.
        const bool retryable = m_browserAttempt == 0 && !error.contains(QStringLiteral("Không tìm thấy")) &&
                               !error.contains(QStringLiteral("quá thời gian")) && !error.contains(QStringLiteral("Đã hủy")) &&
                               !error.contains(QStringLiteral("Địa chỉ không hợp lệ"));
        if (retryable)
        {
            ++m_browserAttempt;
            m_browserFirstError = error;
            emit progress("Trình duyệt ẩn lỗi - thử lại bằng chế độ ẩn cũ...");
            startBrowser(m_browserRefine ? m_url : workUrl());
            return;
        }
        m_browserError = m_browserFirstError.isEmpty() ? error : QString("lần 1: %1 | lần 2: %2").arg(m_browserFirstError, error);
        if (m_browserRefine)
        {
            // Không phân loại được bài của hồ sơ TikTok: giữ danh sách yt-dlp, mọi mục chưa rõ là ảnh hay video.
            QList<ResolvedMedia> items = m_pendingCollection.items;
            for (ResolvedMedia& m : items)
                m.maybePhotoPost = true;
            m_warning = "Không dùng được trình duyệt ẩn để phân biệt bài ảnh/video của hồ sơ này (" + m_browserError +
                        ") - bài ảnh sẽ được kiểm tra lại lúc tải.";
            finishOk(items, true, m_pendingCollection.truncated);
            return;
        }
        finishError(QString());
        return;
    }
    if (m_browserRefine)
        onRefineDom(dom);
    else
        onPageDom(dom, netLog);
}

void LinkResolver::onPageDom(const QString& dom, const QByteArray& netLog)
{
    const QString pageUrl = m_finalUrl.isEmpty() ? workUrl() : m_finalUrl;
    const LinkResolverCore::DeepScan scan = LinkResolverCore::deepScanHtml(dom, pageUrl);
    if (m_pageTitle.isEmpty())
        m_pageTitle = scan.title;
    const bool challengeNow = LinkResolverCore::looksLikeChallengePage(dom);
    if (challengeNow)
        m_challenge = true;

    QList<ResolvedMedia> items;
    QSet<QString> seen;
    auto addMedia = [&](const QString& url, MediaLink::Type type, const QString& note) {
        if (seen.contains(url))
            return;
        seen.insert(url);
        ResolvedMedia m;
        m.url = url;
        m.referer = pageUrl;
        m.note = note;
        if (LinkResolverCore::isManifestUrl(url))
        {
            m.kind = ResolvedMedia::Kind::YtDlp;
            m.mediaType = "Video";
            m.title = m_pageTitle;
            const QString base = FileDownloaderInternal::sanitizeFileName(m_pageTitle);
            m.fileName = base.isEmpty() ? QString() : base + ".mp4";
        }
        else
        {
            m.kind = ResolvedMedia::Kind::HttpFile;
            m.fileName = FileDownloaderInternal::fileNameFromUrl(url);
            m.title = m.fileName;
            m.mediaType = type == MediaLink::Type::Image ? QString("Ảnh") : LinkResolverCore::friendlyTypeLabel(QString(), url);
        }
        items << m;
    };
    const bool photoPost = LinkResolverCore::isPhotoPostUrl(workUrl());
    const bool profile = LinkResolverCore::isLikelyProfileOrCollectionUrl(workUrl());
    // Yêu cầu mạng thật của trang (luồng .m3u8/.mp4 do trình phát gọi) - thứ DevTools > Network hiện ra.
    // Bài đăng ảnh thì video/âm thanh trong trang chỉ là phần giao diện/đề xuất - bỏ qua.
    if (!photoPost && !profile)
    {
        for (const QString& u : LinkResolverCore::mediaUrlsFromNetLog(netLog))
            addMedia(u, MediaLink::Type::Video, "trình duyệt ẩn (mạng)");
        // Video/âm thanh/tài liệu trong DOM đã dựng (bỏ tệp của giao diện trang).
        for (const MediaLink& l : scan.media)
            if ((l.type == MediaLink::Type::Video || l.type == MediaLink::Type::Audio || l.type == MediaLink::Type::Document) &&
                !LinkResolverCore::isJunkMediaUrl(l.url))
                addMedia(l.url, l.type, "trình duyệt ẩn (DOM)");
    }
    // Bài đăng ảnh, hồ sơ, hoặc không có video nào: lấy ảnh nội dung trong DOM.
    if (photoPost || profile || items.isEmpty())
        for (const MediaLink& l : LinkResolverCore::photosFromDom(dom, pageUrl))
            addMedia(l.url, MediaLink::Type::Image, "trình duyệt ẩn (ảnh)");

    if (profile)
    {
        m_warning = "Chỉ lấy được phần công khai nhìn thấy khi chưa đăng nhập (không phải toàn bộ bài đăng của hồ sơ). "
                    "Facebook/Instagram yêu cầu đăng nhập để xem đầy đủ.";
        if (!m_ytError.isEmpty() && !m_ytUnsupported)
            m_warning += " yt-dlp báo: " + m_ytError;

        // QUÉT TỪNG BÀI ĐĂNG: ảnh trên trang hồ sơ chỉ là bản thu nhỏ/cắt vuông; mỗi bài ảnh được dựng riêng để lấy ảnh đầy
        // đủ, bài video giao cho yt-dlp.
        if (m_opts.expandPosts)
        {
            const QList<LinkResolverCore::PostRef> posts = LinkResolverCore::postLinksFromDom(dom, pageUrl, m_opts.maxItems);
            if (!posts.isEmpty())
            {
                QList<Slot> plan;
                for (const LinkResolverCore::PostRef& post : posts)
                {
                    Slot sl;
                    if (post.photo)
                    {
                        sl.isPhoto = true;
                        sl.photoUrl = post.url;
                    }
                    else
                    {
                        sl.fixed.kind = ResolvedMedia::Kind::YtDlp;
                        sl.fixed.url = post.url;
                        sl.fixed.title = QString("Video %1").arg(post.id);
                        sl.fixed.fileName = FileDownloaderInternal::sanitizeFileName(sl.fixed.title);
                        sl.fixed.mediaType = "Video";
                        sl.fixed.referer = pageUrl;
                        sl.fixed.note = "bài đăng trong hồ sơ";
                    }
                    plan << sl;
                }
                startExpansion(plan, true, posts.size() >= m_opts.maxItems, items);
                return;
            }
        }
    }

    if (items.isEmpty())
    {
        if (!challengeNow && LinkResolverCore::looksLikeLoginWall(dom))
            m_loginWall = true;
        finishError(QString());
        return;
    }
    finishOk(items, profile);
}

// ------------------------------------------------------------------------------------------------ Hồ sơ TikTok
void LinkResolver::refineTikTokCollection(const ResolveResult& collection)
{
    m_pendingCollection = collection;
    m_browserRefine = true;
    m_browserAttempt = 0;
    m_browserFirstError.clear();
    emit progress("Đang phân biệt bài ảnh/video của hồ sơ bằng trình duyệt ẩn...");
    startBrowser(m_url);
}

void LinkResolver::onRefineDom(const QString& dom)
{
    // DOM của hồ sơ liệt kê bài ảnh dưới dạng /@user/photo/<id> và bài video /@user/video/<id> (đã đo thật: yt-dlp liệt kê
    // CẢ HAI loại dưới dạng /video/). Chỉ các bài hiện ở đầu trang được biết; bài còn lại đánh dấu "có thể là ảnh".
    QSet<QString> photoIds, videoIds;
    for (const LinkResolverCore::PostRef& post : LinkResolverCore::postLinksFromDom(dom, m_url, 300))
        (post.photo ? photoIds : videoIds).insert(post.id);

    QList<Slot> plan;
    bool anyPhoto = false;
    for (ResolvedMedia m : m_pendingCollection.items)
    {
        const QString id = LinkResolverCore::tiktokPostId(m.url);
        Slot sl;
        const QString photoUrl = photoIds.contains(id) ? LinkResolverCore::tiktokPhotoUrlFromVideoUrl(m.url) : QString();
        if (!photoUrl.isEmpty())
        {
            sl.isPhoto = true;
            sl.photoUrl = photoUrl;
            anyPhoto = true;
        }
        else
        {
            m.maybePhotoPost = !videoIds.contains(id);
            sl.fixed = m;
        }
        plan << sl;
    }
    QList<ResolvedMedia> fallback;
    for (const Slot& sl : std::as_const(plan))
        if (!sl.isPhoto)
            fallback << sl.fixed;
    if (!anyPhoto)
    {
        finishOk(fallback, true, m_pendingCollection.truncated);
        return;
    }
    startExpansion(plan, true, m_pendingCollection.truncated, fallback);
}

// ------------------------------------------------------------------------------------------------ Quét từng bài đăng
void LinkResolver::startExpansion(const QList<Slot>& plan, bool collection, bool truncated, const QList<ResolvedMedia>& fallback)
{
    m_slots = plan;
    m_expCollection = collection;
    m_expTruncated = truncated;
    m_expFallback = fallback;
    pumpSlots();
}

void LinkResolver::pumpSlots()
{
    if (m_done || m_canceled)
        return;
    int active = 0;
    for (const Slot& sl : std::as_const(m_slots))
        if (sl.isPhoto && sl.started && !sl.done)
            ++active;
    for (int i = 0; i < m_slots.size() && active < qMax(1, m_opts.maxParallelChildren); ++i)
    {
        Slot& sl = m_slots[i];
        if (!sl.isPhoto || sl.started)
            continue;
        sl.started = true;
        ++active;
        auto* child = new LinkResolver(this);
        Options o = m_opts;
        o.maxItems = 60;
        o.expandPosts = false; // bộ phân giải con xử lý MỘT bài, không đệ quy
        child->setOptions(o);
        m_children << child;
        connect(child, &LinkResolver::finished, this, [this, i, child](ResolveResult r) {
            m_children.removeAll(child);
            child->deleteLater();
            if (m_done || m_canceled)
                return;
            Slot& done = m_slots[i];
            done.done = true;
            if (r.ok())
                done.children = r.items;
            else
                done.error = r.error;
            pumpSlots();
        });
        child->resolve(sl.photoUrl);
    }

    int total = 0, finished = 0;
    for (const Slot& sl : std::as_const(m_slots))
        if (sl.isPhoto)
        {
            ++total;
            if (sl.done)
                ++finished;
        }
    if (finished < total)
    {
        emit progress(QString("Đang quét từng bài đăng ảnh (%1/%2)...").arg(finished).arg(total));
        return;
    }
    finishExpansion();
}

void LinkResolver::finishExpansion()
{
    QList<ResolvedMedia> items;
    QSet<QString> seen;
    int photoPosts = 0, photoOk = 0;
    QString firstError;
    for (const Slot& sl : std::as_const(m_slots))
    {
        if (!sl.isPhoto)
        {
            if (!seen.contains(sl.fixed.url))
            {
                seen.insert(sl.fixed.url);
                items << sl.fixed;
            }
            continue;
        }
        ++photoPosts;
        if (!sl.children.isEmpty())
            ++photoOk;
        else if (firstError.isEmpty())
            firstError = sl.error;
        for (ResolvedMedia m : sl.children)
        {
            if (seen.contains(m.url))
                continue;
            seen.insert(m.url);
            m.note = "bài đăng ảnh";
            items << m;
        }
    }
    if (items.isEmpty())
        items = m_expFallback; // không quét được bài nào: dùng những gì thấy ngay trên trang hồ sơ
    if (photoPosts > photoOk)
    {
        const QString note = QString("%1/%2 bài ảnh không lấy được ảnh%3.")
                                 .arg(photoPosts - photoOk)
                                 .arg(photoPosts)
                                 .arg(firstError.isEmpty() ? QString() : " (" + firstError.left(120) + ")");
        m_warning = m_warning.isEmpty() ? note : m_warning + " " + note;
    }
    if (items.isEmpty())
    {
        finishError(firstError);
        return;
    }
    finishOk(items, m_expCollection, m_expTruncated);
}
