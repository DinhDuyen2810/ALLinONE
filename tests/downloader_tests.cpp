// Kiểm thử lõi Downloader. KHÔNG cần mạng Internet/yt-dlp thật - kiểm thử lõi THUẦN (phân tích HTML mẫu
// dựng sẵn, dựng đối số dòng lệnh yt-dlp, phân tích JSON/dòng tiến độ mẫu, làm sạch tên tệp), cộng với
// FileDownloader/PageMediaScanner chạy THẬT nhưng chỉ nói chuyện với một máy chủ HTTP tí hon dựng ngay
// trong test trên 127.0.0.1 (xem MiniHttpServer) - không có gói tin nào ra khỏi máy.
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <cstdio>
#include <functional>
#include <memory>

#include "tools/downloader/engine/FileDownloader.h"
#include "tools/downloader/engine/PageMediaScanner.h"
#include "tools/downloader/engine/YtDlpController.h"
#include "tools/downloader/engine/YtDlpDownloadWorker.h"
#include "tools/downloader/engine/YtDlpInfoWorker.h"
#include "tools/downloader/model/DownloadItem.h"
#include "tools/downloader/model/MediaLink.h"
#include "tools/downloader/model/VideoInfo.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

/// Máy chủ HTTP/1.1 tối thiểu trên 127.0.0.1 (cổng ngẫu nhiên): mỗi kết nối đọc MỘT yêu cầu, gọi
/// `handler(phần đầu yêu cầu)` lấy nguyên văn các byte trả lời, ghi ra rồi đóng kết nối. Trả lời khai
/// Content-Length lớn hơn số byte thật sự gửi = giả lập "đứt kết nối giữa chừng".
struct MiniHttpServer
{
    QTcpServer server;
    QList<QByteArray> requests; ///< Phần đầu (request line + header) của từng yêu cầu đã nhận, theo thứ tự
    std::function<QByteArray(const QByteArray& head)> handler;

    MiniHttpServer()
    {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this]() {
            while (QTcpSocket* sock = server.nextPendingConnection())
            {
                auto buffer = std::make_shared<QByteArray>();
                QObject::connect(sock, &QTcpSocket::readyRead, sock, [this, sock, buffer]() {
                    buffer->append(sock->readAll());
                    const int end = buffer->indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    const QByteArray head = buffer->left(end);
                    buffer->clear();
                    requests << head;
                    sock->write(handler(head));
                    sock->disconnectFromHost(); // ghi hết phần đã xếp hàng rồi mới đóng
                });
                QObject::connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });
        server.listen(QHostAddress::LocalHost, 0);
    }

    QString url(const QString& path) const { return QString("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path); }

    /// Giá trị một header trong phần đầu yêu cầu (không phân biệt hoa thường), rỗng nếu không có.
    static QByteArray header(const QByteArray& head, const QByteArray& name)
    {
        for (const QByteArray& line : head.split('\n'))
        {
            const int colon = line.indexOf(':');
            if (colon > 0 && line.left(colon).trimmed().toLower() == name.toLower())
                return line.mid(colon + 1).trimmed();
        }
        return {};
    }
    static QByteArray path(const QByteArray& head) { return head.split(' ').value(1); }

    static QByteArray response(const QByteArray& statusLine, const QList<QByteArray>& headers, const QByteArray& body,
                               qint64 declaredLength = -1)
    {
        QByteArray out = "HTTP/1.1 " + statusLine + "\r\nConnection: close\r\n";
        for (const QByteArray& h : headers)
            out += h + "\r\n";
        out += "Content-Length: " + QByteArray::number(declaredLength >= 0 ? declaredLength : body.size()) + "\r\n\r\n";
        return out + body;
    }
};

/// Quay vòng lặp sự kiện tới khi `done()` đúng hoặc hết thời gian chờ.
static bool waitUntil(const std::function<bool()>& done, int timeoutMs = 15000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

static QByteArray readFileBytes(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ---- PageMediaScannerInternal::classifyByExtension ----
    {
        using T = MediaLink::Type;
        CHECK(PageMediaScannerInternal::classifyByExtension("https://x.com/a.JPG") == T::Image);
        CHECK(PageMediaScannerInternal::classifyByExtension("https://x.com/a.mp4") == T::Video);
        CHECK(PageMediaScannerInternal::classifyByExtension("https://x.com/a.mp3") == T::Audio);
        CHECK(PageMediaScannerInternal::classifyByExtension("https://x.com/a.pdf") == T::Document);
        CHECK(PageMediaScannerInternal::classifyByExtension("https://x.com/a.html") == T::Other);
        CHECK(PageMediaScannerInternal::classifyByExtension("https://x.com/noext") == T::Other);
    }

    // ---- PageMediaScannerInternal::extractMediaLinks: HTML mẫu dựng sẵn ----
    {
        const QString html = R"(
            <html><body>
                <img src="/images/photo1.jpg" alt="x">
                <img src="https://cdn.example.com/photo2.png">
                <img src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAAB">
                <video src="movie.mp4"></video>
                <audio><source src="clip.mp3"></audio>
                <video><source src="clip.webm" type="video/webm"></video>
                <a href="/docs/report.pdf">Báo cáo</a>
                <a href="https://other.com/data.zip">Dữ liệu</a>
                <a href="/trang-khac.html">Chỉ là link điều hướng, KHÔNG phải media</a>
                <img src="/images/photo1.jpg">
            </body></html>
        )";
        const auto links = PageMediaScannerInternal::extractMediaLinks(html, "https://example.com/trang/");

        // Trùng lặp (photo1.jpg xuất hiện 2 lần) phải bị loại bỏ
        int photo1Count = 0;
        for (const auto& l : links) if (l.url.contains("photo1.jpg")) ++photo1Count;
        CHECK(photo1Count == 1);

        // Link tương đối phải được phân giải thành tuyệt đối theo đúng URL gốc
        bool foundResolvedImg = false, foundAbsoluteImg = false, foundVideo = false, foundAudioMp3 = false,
             foundSourceWebm = false, foundPdf = false, foundZip = false, foundDataUri = false, foundNavLink = false;
        for (const auto& l : links)
        {
            if (l.url == "https://example.com/images/photo1.jpg") { foundResolvedImg = true; CHECK(l.type == MediaLink::Type::Image); }
            if (l.url == "https://cdn.example.com/photo2.png") foundAbsoluteImg = true;
            if (l.url.contains("movie.mp4")) { foundVideo = true; CHECK(l.type == MediaLink::Type::Video); }
            if (l.url.contains("clip.mp3")) { foundAudioMp3 = true; CHECK(l.type == MediaLink::Type::Audio); }
            if (l.url.contains("clip.webm")) { foundSourceWebm = true; CHECK(l.type == MediaLink::Type::Video); }
            if (l.url.contains("report.pdf")) { foundPdf = true; CHECK(l.type == MediaLink::Type::Document); }
            if (l.url.contains("data.zip")) foundZip = true;
            if (l.url.startsWith("data:")) foundDataUri = true;
            if (l.url.contains("trang-khac.html")) foundNavLink = true;
        }
        CHECK(foundResolvedImg);
        CHECK(foundAbsoluteImg);
        CHECK(foundVideo);
        CHECK(foundAudioMp3);
        CHECK(foundSourceWebm);
        CHECK(foundPdf);
        CHECK(foundZip);
        CHECK(!foundDataUri);  // ảnh nhúng base64 phải bị loại bỏ - không phải "link" tải được
        CHECK(!foundNavLink);  // link điều hướng thường (.html) không được tính là media/tài liệu
    }

    // ---- YtDlpDownloadWorkerInternal::buildArguments ----
    {
        const auto args = YtDlpDownloadWorkerInternal::buildArguments(
            "https://youtu.be/abc", "137+140", "D:/Tai ve", "D:/ALLinONE/yt-dlp");
        CHECK(args.contains("--no-playlist"));
        CHECK(args.contains("--newline"));
        CHECK(args.contains("--ffmpeg-location"));
        CHECK(args.contains("D:/ALLinONE/yt-dlp"));
        CHECK(args.contains("-f"));
        CHECK(args.contains("137+140"));
        CHECK(args.contains("--progress-template"));
        CHECK(args.last() == "https://youtu.be/abc"); // URL luôn ở cuối
        // ... và đứng NGAY SAU "--": một "URL" bắt đầu bằng "-" không bao giờ bị yt-dlp hiểu là tùy chọn
        CHECK(args.size() >= 2 && args[args.size() - 2] == "--");
        CHECK(args.count("--") == 1);
        CHECK(args.first() == "--ignore-config"); // không nạp tệp cấu hình yt-dlp bên ngoài (có thể chứa --exec)
        CHECK(args.contains("D:/Tai ve/%(title)s.%(ext)s"));

        // Không truyền formatId -> KHÔNG có cờ -f (để yt-dlp tự chọn mặc định video+âm thanh tốt nhất)
        const auto argsNoFormat = YtDlpDownloadWorkerInternal::buildArguments("https://x.com/v", "", "D:/x", "D:/yt-dlp");
        CHECK(!argsNoFormat.contains("-f"));

        // Hồi quy: "%" trong tên thư mục đích phải được nhân đôi - "-o" là MẪU của yt-dlp, không phải đường dẫn thô
        const auto argsPercent = YtDlpDownloadWorkerInternal::buildArguments("https://x.com/v", "", "D:/100% phim/%(id)s", "D:/yt-dlp");
        CHECK(argsPercent.contains("D:/100%% phim/%%(id)s/%(title)s.%(ext)s"));

        // "URL" thực chất là một tùy chọn: vẫn nằm sau "--" (và bị từ chối từ trước bởi isSupportedVideoUrl)
        const auto argsEvil = YtDlpDownloadWorkerInternal::buildArguments("--exec=calc.exe", "", "D:/x", "D:/yt-dlp");
        CHECK(argsEvil.indexOf("--exec=calc.exe") == argsEvil.indexOf("--") + 1);

        const auto infoArgs = YtDlpInfoWorkerInternal::buildArguments("https://youtu.be/abc");
        CHECK(infoArgs == QStringList({"--ignore-config", "--no-playlist", "--simulate", "-j", "--", "https://youtu.be/abc"}));
    }

    // ---- YtDlpController::isSupportedVideoUrl: chỉ http(s)://, không khoảng trắng/ký tự điều khiển ----
    {
        using YtDlpController::isSupportedVideoUrl;
        CHECK(isSupportedVideoUrl("https://www.youtube.com/watch?v=abc&list=xyz"));
        CHECK(isSupportedVideoUrl("HTTP://example.com/v"));
        for (const char* bad : {"", "--exec=calc.exe", "--config-locations=C:/x.conf", "-U", "ytsearch:abc", "file:///C:/a.mp4",
                                "ftp://example.com/v", "https://", "https://a.com/v --exec calc", "https://a.com/v\n--exec",
                                "javascript:alert(1)", "www.youtube.com/watch?v=abc"})
            CHECK(!isSupportedVideoUrl(bad));
    }

    // ---- YtDlpDownloadWorkerInternal::formatSelectorFor: định dạng CHỈ CÓ HÌNH phải ghép kèm âm thanh ----
    {
        VideoFormatInfo videoOnly;
        videoOnly.formatId = "137";
        videoOnly.hasVideo = true;
        CHECK(YtDlpDownloadWorkerInternal::formatSelectorFor(videoOnly) == "137+bestaudio/best");

        VideoFormatInfo muxed;
        muxed.formatId = "18";
        muxed.hasVideo = muxed.hasAudio = true;
        CHECK(YtDlpDownloadWorkerInternal::formatSelectorFor(muxed) == "18");

        VideoFormatInfo audioOnly;
        audioOnly.formatId = "140";
        audioOnly.hasAudio = true;
        CHECK(YtDlpDownloadWorkerInternal::formatSelectorFor(audioOnly) == "140");
        CHECK(YtDlpDownloadWorkerInternal::formatSelectorFor(VideoFormatInfo()).isEmpty());
    }

    // ---- YtDlpDownloadWorker: MỌI lần start() đều kết thúc bằng đúng một finished() - kể cả khi không
    // chạy được gì (URL không hợp lệ/không có yt-dlp.exe cạnh tệp test). KHÔNG chạy yt-dlp thật. ----
    {
        YtDlpDownloadWorker worker;
        int finishedCount = 0;
        bool lastOk = true;
        QString lastError;
        QObject::connect(&worker, &YtDlpDownloadWorker::finished, [&](bool ok, QString error) {
            ++finishedCount;
            lastOk = ok;
            lastError = error;
        });
        worker.start("--exec=calc.exe", "", QDir::tempPath());
        CHECK(finishedCount == 1);
        CHECK(!lastOk);
        CHECK(!lastError.isEmpty());
        CHECK(!worker.isRunning());
        CHECK(!worker.wasCanceled());
        worker.cancel(); // không có gì đang chạy - không được crash, không phát thêm tín hiệu
        CHECK(finishedCount == 1);
    }

    // ---- YtDlpDownloadWorkerInternal::parseProgressLine: dòng tiến độ THẬT dạng --progress-template ----
    {
        const auto p1 = YtDlpDownloadWorkerInternal::parseProgressLine("DLPROG|123456|987654|NA|45678|12");
        CHECK(p1.ok);
        CHECK(p1.downloadedBytes == 123456);
        CHECK(p1.totalBytes == 987654);
        CHECK(p1.speedBytesPerSec == 45678);
        CHECK(p1.etaSeconds == 12);

        // total_bytes là "NA" (chưa rõ chính xác) -> rơi về total_bytes_estimate
        const auto p2 = YtDlpDownloadWorkerInternal::parseProgressLine("DLPROG|1000|NA|50000|2000|25");
        CHECK(p2.ok);
        CHECK(p2.totalBytes == 50000);

        // speed/eta đều "NA" -> -1 (chưa rõ), KHÔNG được coi là 0
        const auto p3 = YtDlpDownloadWorkerInternal::parseProgressLine("DLPROG|1000|NA|NA|NA|NA");
        CHECK(p3.ok);
        CHECK(p3.totalBytes == -1);
        CHECK(p3.speedBytesPerSec == -1);
        CHECK(p3.etaSeconds == -1);

        // Dòng khác (không phải tiến độ, vd log thường của yt-dlp) -> ok=false, không crash
        CHECK(!YtDlpDownloadWorkerInternal::parseProgressLine("[youtube] Extracting URL: https://...").ok);
        CHECK(!YtDlpDownloadWorkerInternal::parseProgressLine("DLPROG|thieu-truong").ok);

        // yt-dlp trên Windows kết thúc dòng bằng "\r\n" - ký tự "\r" còn sót không được làm hỏng trường cuối (ETA)
        const auto p4 = YtDlpDownloadWorkerInternal::parseProgressLine("DLPROG|10|20|NA|30|7\r");
        CHECK(p4.ok);
        CHECK(p4.etaSeconds == 7);
    }

    // ---- YtDlpInfoWorkerInternal::parseInfoJson: JSON mẫu dựng theo đúng cấu trúc THẬT đã xác nhận ----
    {
        // MỘT DÒNG DUY NHẤT - đúng định dạng thật `yt-dlp --dump-json` trả về (đã xác nhận bằng cách
        // chạy thật trên 1 video YouTube thật: 86835 byte JSON gói gọn trên 1 dòng). parseInfoJson lấy
        // dòng CUỐI CÙNG khác rỗng của stdout (phòng trường hợp có dòng cảnh báo lạc vào) - JSON nhiều
        // dòng ở đây sẽ khiến nó chỉ đọc được "}" và lỗi, nên mẫu test PHẢI nén thành 1 dòng cho đúng.
        const QByteArray json =
            R"({"title": "Me at the zoo", "duration": 19, "thumbnail": "https://i.ytimg.com/vi/x/hqdefault.jpg", )"
            R"("formats": [)"
            R"({"format_id": "139", "ext": "m4a", "resolution": "audio only", "format_note": "low", )"
            R"("vcodec": "none", "acodec": "mp4a.40.5", "filesize": 117495}, )"
            R"({"format_id": "137", "ext": "mp4", "resolution": "1920x1080", "format_note": "1080p", )"
            R"("vcodec": "avc1.640028", "acodec": "none", "filesize_approx": 5242880, "fps": 30}, )"
            R"({"format_id": "no-video-no-audio-should-be-skipped-if-missing-id", "ext": "mhtml", )"
            R"("resolution": "0x0", "vcodec": "none", "acodec": "none"})"
            R"(]})";
        QString err;
        const VideoInfo info = YtDlpInfoWorkerInternal::parseInfoJson(json, &err);
        CHECK(err.isEmpty());
        CHECK(info.title == "Me at the zoo");
        CHECK(info.durationSeconds == 19);
        CHECK(info.durationLabel() == "0:19");
        CHECK(info.formats.size() == 3);
        if (info.formats.size() == 3)
        {
            CHECK(info.formats[0].hasAudio && !info.formats[0].hasVideo);
            CHECK(info.formats[0].approxSizeBytes == 117495);
            CHECK(info.formats[1].hasVideo && !info.formats[1].hasAudio);
            CHECK(info.formats[1].approxSizeBytes == 5242880); // rơi về filesize_approx khi filesize vắng mặt
            CHECK(info.formats[1].resolution == "1920x1080");
        }

        // JSON hỏng -> báo lỗi rõ ràng, không crash
        QString err2;
        const VideoInfo bad = YtDlpInfoWorkerInternal::parseInfoJson("khong phai json", &err2);
        CHECK(!err2.isEmpty());
        CHECK(bad.formats.isEmpty());

        // Hồi quy ĐÚNG kịch bản thật "lấy dòng cuối" được thiết kế cho: stdout đôi khi có cảnh báo lạc
        // vào TRƯỚC dòng JSON thật (đã thấy thật khi gọi yt-dlp thật - cảnh báo "No supported JavaScript
        // runtime..." in ra kèm JSON) - JSON thật vẫn phải đọc được dù không phải dòng đầu tiên.
        const QByteArray withWarning = "WARNING: [youtube] some warning text\n" + json;
        QString err3;
        const VideoInfo info3 = YtDlpInfoWorkerInternal::parseInfoJson(withWarning, &err3);
        CHECK(err3.isEmpty());
        CHECK(info3.title == "Me at the zoo");
    }

    // ---- VideoInfo::durationLabel: định dạng giờ:phút:giây ----
    {
        VideoInfo info;
        info.durationSeconds = 3725; // 1h 02m 05s
        CHECK(info.durationLabel() == "1:02:05");
    }

    // ---- DownloadItem/MediaLink: tiện ích hiển thị thuần ----
    {
        DownloadItem item;
        item.destPath = "D:/Tai ve/anh.jpg";
        CHECK(item.fileName() == "anh.jpg");
        CHECK(DownloadItem::statusLabel(DownloadItem::Status::Downloading) == "Đang tải");
        CHECK(DownloadItem::statusLabel(DownloadItem::Status::Completed) == "Hoàn tất");

        MediaLink link;
        link.url = "https://example.com/a/b/video.mp4?token=xyz#frag";
        CHECK(link.fileName() == "video.mp4");
        CHECK(MediaLink::typeLabel(MediaLink::Type::Video) == "Video");
    }

    // ---- PageMediaScannerInternal: thực thể HTML, srcset/poster, thuộc tính không nháy, lazy-load, scheme ----
    {
        using namespace PageMediaScannerInternal;
        CHECK(decodeHtmlEntities("a.jpg?w=1&amp;h=2&amp;t=x") == "a.jpg?w=1&h=2&t=x");
        CHECK(decodeHtmlEntities("&lt;&gt;&quot;&apos;&#38;&#x26;&#X41;") == "<>\"'&&A");
        CHECK(decodeHtmlEntities("&amp;amp;") == "&amp;");               // chỉ giải mã MỘT lượt
        CHECK(decodeHtmlEntities("a&b &khongco; &#0; &#xD800;") == "a&b &khongco; &#0; &#xD800;"); // không biết/không hợp lệ: giữ nguyên
        CHECK(decodeHtmlEntities("khong co gi") == "khong co gi");

        CHECK(isHtmlContentType(""));
        CHECK(isHtmlContentType("text/html; charset=utf-8"));
        CHECK(isHtmlContentType("application/xhtml+xml"));
        CHECK(!isHtmlContentType("application/octet-stream"));
        CHECK(!isHtmlContentType("video/mp4"));
        CHECK(!isHtmlContentType("application/zip"));

        const QString html = R"HTML(
            <img src="/a.jpg?w=1&amp;h=2">
            <img src=/khong-nhay.png alt=x>
            <img class="lazy" src="/placeholder.gif" data-src="/that.webp">
            <img srcset="/s-1x.jpg 1x, /s-2x.jpg 2x">
            <picture><source srcset="/p-480.avif 480w, /p-800.avif 800w"></picture>
            <video poster="/poster.jpg" src="/phim.mp4"></video>
            <img src="file:///C:/Windows/win.ini">
            <img src="javascript:alert(1)">
            <a href="ftp://example.com/tep.zip">ftp</a>
            <a href="mailto:a@example.com?subject=x.pdf">mail</a>
            <a href="/tai-lieu.PDF?v=1&amp;k=2">pdf</a>
            <article src="/khong-phai-the-a.zip" href="/khong-phai-the-a.pdf"></article>
        )HTML";
        const auto links = extractMediaLinks(html, "https://example.com/trang/");
        QStringList urls;
        for (const auto& l : links)
            urls << l.url;

        CHECK(urls.contains("https://example.com/a.jpg?w=1&h=2"));     // &amp; đã giải mã (trước đây URL sai)
        CHECK(urls.contains("https://example.com/khong-nhay.png"));    // giá trị không có dấu nháy
        CHECK(urls.contains("https://example.com/that.webp"));         // ảnh thật của lazy-load
        CHECK(urls.contains("https://example.com/s-1x.jpg") && urls.contains("https://example.com/s-2x.jpg"));
        CHECK(urls.contains("https://example.com/p-480.avif") && urls.contains("https://example.com/p-800.avif"));
        CHECK(urls.contains("https://example.com/poster.jpg"));
        CHECK(urls.contains("https://example.com/phim.mp4"));
        CHECK(urls.contains("https://example.com/tai-lieu.PDF?v=1&k=2"));
        for (const auto& l : links)
        {
            // Chỉ còn liên kết http/https - không file:, javascript:, ftp:, mailto:
            CHECK(l.url.startsWith("https://example.com/"));
            if (l.url.contains("poster.jpg")) CHECK(l.type == MediaLink::Type::Image);
            if (l.url.contains("phim.mp4")) CHECK(l.type == MediaLink::Type::Video);
            if (l.url.contains("tai-lieu.PDF")) CHECK(l.type == MediaLink::Type::Document);
        }
        CHECK(!urls.join(' ').contains("khong-phai-the-a")); // <article> không phải thẻ <a>
    }

    // ---- FileDownloaderInternal: URL, làm sạch tên tệp, chọn đường dẫn đích ----
    {
        using namespace FileDownloaderInternal;
        CHECK(isHttpUrl("https://example.com/a.zip"));
        CHECK(isHttpUrl("  HTTP://example.com/a.zip  "));
        for (const char* bad : {"", "file:///C:/Windows/win.ini", "ftp://example.com/a", "data:text/plain,abc", "javascript:alert(1)",
                                "C:/Users/a.txt", "example.com/a.zip", "https://", "qrc:/icons/a.png"})
            CHECK(!isHttpUrl(bad));

        CHECK(sanitizeFileName("anh dep.jpg") == "anh dep.jpg");
        CHECK(sanitizeFileName(QString::fromUtf8("\u1ea3nh \u0111\u1eb9p.jpg")) == QString::fromUtf8("\u1ea3nh \u0111\u1eb9p.jpg")); // tên có dấu giữ nguyên
        // Thành phần thư mục - cả "/" lẫn "\" - bị bỏ hẳn
        CHECK(sanitizeFileName("..\\..\\AppData\\Startup\\x.bat") == "x.bat");
        CHECK(sanitizeFileName("../../etc/passwd") == "passwd");
        CHECK(sanitizeFileName("C:\\Windows\\System32\\drivers\\etc\\hosts") == "hosts");
        // Ký tự cấm trên Windows (':' còn mở alternate data stream) và ký tự điều khiển
        CHECK(sanitizeFileName("a.jpg:stream") == "a.jpg_stream");
        CHECK(sanitizeFileName("a*b?c\"d<e>f|g.txt") == "a_b_c_d_e_f_g.txt");
        CHECK(sanitizeFileName(QString("a\tb\nc") + QChar(0) + "d.txt") == "abcd.txt");
        // Ký tự đảo chiều chữ (giả mạo phần mở rộng) và ký tự vô hình
        CHECK(sanitizeFileName(QString("anh") + QChar(0x202E) + "gpj.exe") == "anhgpj.exe");
        CHECK(sanitizeFileName(QString("a") + QChar(0x200B) + "b.txt") == "ab.txt");
        // Dấu chấm/khoảng trắng cuối tên; "." và ".." không phải tên tệp
        CHECK(sanitizeFileName("  tep.txt. . ") == "tep.txt");
        CHECK(sanitizeFileName(".").isEmpty());
        CHECK(sanitizeFileName("..").isEmpty());
        CHECK(sanitizeFileName("thu-muc/").isEmpty());
        CHECK(sanitizeFileName("").isEmpty());
        CHECK(sanitizeFileName(".htaccess") == ".htaccess");
        // Tên thiết bị dành riêng (kể cả có phần mở rộng, không phân biệt hoa thường)
        for (const char* reserved : {"CON", "con", "PRN", "AUX", "NUL", "nul.txt", "COM1", "com9.log", "LPT1", "LPT9.tar.gz"})
            CHECK(sanitizeFileName(reserved) == "_" + QString::fromLatin1(reserved));
        CHECK(sanitizeFileName("CONSOLE.txt") == "CONSOLE.txt"); // chỉ giống tiền tố - không phải tên dành riêng
        CHECK(sanitizeFileName("COM10.txt") == "COM10.txt");
        // Giới hạn độ dài, giữ phần mở rộng
        const QString longName = sanitizeFileName(QString(400, 'a') + ".mp4");
        CHECK(longName.size() == 150);
        CHECK(longName.endsWith(".mp4"));

        // Tên tệp từ URL: QUrl giải mã "%5C" thành "\" và "%2F" thành "/" - trước đây "..%5C..%5Cx.bat" thoát
        // được khỏi thư mục lưu
        CHECK(fileNameFromUrl("https://evil.example/x/..%5C..%5CAppData%5CStartup%5Ca.bat") == "a.bat");
        CHECK(fileNameFromUrl("https://evil.example/x/..%2F..%2Fb.bat") == "b.bat");
        CHECK(fileNameFromUrl("https://example.com/a/b/video.mp4?token=xyz#frag") == "video.mp4");
        CHECK(fileNameFromUrl("https://example.com/my%20file.jpg") == "my file.jpg");
        CHECK(fileNameFromUrl("https://example.com/a.jpg%3Astream") == "a.jpg_stream");
        CHECK(fileNameFromUrl("https://example.com/thu-muc/").isEmpty());
        CHECK(fileNameFromUrl("https://example.com/NUL") == "_NUL");

        // uniqueDestPath: luôn nằm NGAY trong thư mục lưu, tránh mọi đường dẫn "đã có người dùng"
        QStringList taken = {"D:/Luu/a.jpg", "D:/Luu/a (1).jpg"};
        auto isTaken = [&taken](const QString& p) { return taken.contains(p); };
        CHECK(uniqueDestPath("D:/Luu", "b.jpg", isTaken) == "D:/Luu/b.jpg");
        CHECK(uniqueDestPath("D:/Luu", "a.jpg", isTaken) == "D:/Luu/a (2).jpg");
        CHECK(uniqueDestPath("D:/Luu", "..\\..\\x.bat", isTaken) == "D:/Luu/x.bat");
        CHECK(uniqueDestPath("D:/Luu", "..", isTaken) == "D:/Luu/tai-ve");
        taken << "D:/Luu/khong-duoi";
        CHECK(uniqueDestPath("D:/Luu", "khong-duoi", isTaken) == "D:/Luu/khong-duoi (1)");
        taken << "D:/Luu/100%3.jpg"; // tên chứa "%3": không được bị hiểu là chỗ điền tham số
        CHECK(uniqueDestPath("D:/Luu", "100%3.jpg", isTaken) == "D:/Luu/100%3 (1).jpg");

        qint64 start = -1, total = -1;
        CHECK(parseContentRange("bytes 500-999/1000", &start, &total) && start == 500 && total == 1000);
        CHECK(parseContentRange(" Bytes 0-9/* ", &start, &total) && start == 0 && total == -1);
        CHECK(!parseContentRange("", &start, &total));
        CHECK(!parseContentRange("bytes */1000", &start, &total));
        CHECK(!parseContentRange("items 0-9/10", &start, &total));

        CHECK(pickResumeValidator("\"abc\"", "Wed, 21 Oct 2015 07:28:00 GMT") == "\"abc\"");
        CHECK(pickResumeValidator("W/\"abc\"", "Wed, 21 Oct 2015 07:28:00 GMT") == "Wed, 21 Oct 2015 07:28:00 GMT"); // ETag yếu không dùng được với If-Range
        CHECK(pickResumeValidator("W/\"abc\"", "").isEmpty());
        CHECK(pickResumeValidator("", "").isEmpty());
    }

    // ---- FileDownloader: chạy THẬT với máy chủ HTTP cục bộ trong test (127.0.0.1) ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString part = FileDownloaderInternal::kPartSuffix;

        QByteArray bodyV1(200 * 1024, '\0');
        for (int i = 0; i < bodyV1.size(); ++i)
            bodyV1[i] = static_cast<char>((i * 31 + 7) % 251);
        QByteArray bodyV2 = bodyV1;
        std::reverse(bodyV2.begin(), bodyV2.end());
        const qint64 half = bodyV1.size() / 2;

        MiniHttpServer http;
        CHECK(http.server.isListening());
        int cutCount = 0;       // số lần đã phục vụ /cut.bin
        int changedCount = 0;   // số lần đã phục vụ /changed.bin
        int noValCount = 0;     // số lần đã phục vụ /noval.bin
        http.handler = [&](const QByteArray& head) -> QByteArray {
            const QByteArray path = MiniHttpServer::path(head);
            const QByteArray range = MiniHttpServer::header(head, "Range");
            const QByteArray ifRange = MiniHttpServer::header(head, "If-Range");
            const QByteArray rangeHeader = "Content-Range: bytes " + QByteArray::number(half) + "-" +
                                           QByteArray::number(bodyV1.size() - 1) + "/" + QByteArray::number(bodyV1.size());
            if (path == "/full.bin")
                return MiniHttpServer::response("200 OK", {"ETag: \"v1\""}, bodyV1);
            if (path == "/missing.bin")
                return MiniHttpServer::response("404 Not Found", {"Content-Type: text/html"}, "<html>404 Not Found</html>");
            if (path == "/cut.bin")
            {
                // Lần 1: khai đủ độ dài nhưng chỉ gửi nửa đầu rồi đóng. Lần 2: phải nhận đúng Range + If-Range.
                if (++cutCount == 1)
                    return MiniHttpServer::response("200 OK", {"ETag: \"v1\""}, bodyV1.left(half), bodyV1.size());
                if (range == "bytes=" + QByteArray::number(half) + "-" && ifRange == "\"v1\"")
                    return MiniHttpServer::response("206 Partial Content", {"ETag: \"v1\"", rangeHeader}, bodyV1.mid(half));
                return MiniHttpServer::response("500 Internal Server Error", {}, "thieu Range/If-Range");
            }
            if (path == "/changed.bin")
            {
                // Lần 1: đứt giữa chừng ở bản v1. Lần 2: tệp trên máy chủ đã thành v2 - If-Range "v1" không
                // còn khớp nên máy chủ (đúng chuẩn) trả 200 kèm TOÀN BỘ bản v2.
                if (++changedCount == 1)
                    return MiniHttpServer::response("200 OK", {"ETag: \"v1\""}, bodyV1.left(half), bodyV1.size());
                return MiniHttpServer::response("200 OK", {"ETag: \"v2\""}, bodyV2);
            }
            if (path == "/noval.bin")
            {
                // Không có ETag/Last-Modified: lần thử lại KHÔNG được gửi Range (không có gì bảo đảm tệp chưa đổi).
                if (++noValCount == 1)
                    return MiniHttpServer::response("200 OK", {}, bodyV1.left(half), bodyV1.size());
                return MiniHttpServer::response("200 OK", {}, bodyV2);
            }
            if (path == "/badrange.bin")
            {
                // Máy chủ hỏng: nhận Range nhưng trả 206 bắt đầu từ byte 0 - ghép vào là hỏng tệp.
                if (range.isEmpty())
                    return MiniHttpServer::response("200 OK", {"ETag: \"v1\""}, bodyV1.left(half), bodyV1.size());
                return MiniHttpServer::response("206 Partial Content",
                                                {"ETag: \"v1\"", "Content-Range: bytes 0-" + QByteArray::number(bodyV1.size() - 1) +
                                                                      "/" + QByteArray::number(bodyV1.size())},
                                                bodyV1);
            }
            return MiniHttpServer::response("404 Not Found", {}, "");
        };

        FileDownloader downloader;
        QList<int> added;
        QHash<int, int> finishedCount;
        QHash<int, bool> finishedOk;
        QObject::connect(&downloader, &FileDownloader::itemAdded, [&](int id) { added << id; });
        QObject::connect(&downloader, &FileDownloader::itemFinished, [&](int id, bool ok) {
            ++finishedCount[id];
            finishedOk[id] = ok;
        });
        auto runOnce = [&](int id) {
            const int before = finishedCount.value(id);
            downloader.start(id);
            return waitUntil([&]() { return finishedCount.value(id) > before; });
        };

        // (1) Tải trọn vẹn: dữ liệu đi qua tệp .part, chỉ khi xong mới mang tên đích
        const QString fullDest = tmp.path() + "/full.bin";
        const int fullId = downloader.enqueue(http.url("/full.bin"), fullDest);
        CHECK(added == QList<int>({fullId}));              // hồi quy D1: mọi mục enqueue đều được báo ra giao diện
        CHECK(downloader.isDestPathInUse(fullDest));
        CHECK(downloader.isDestPathInUse(tmp.path() + "/FULL.BIN")); // hệ tệp Windows không phân biệt hoa thường
        CHECK(!downloader.isDestPathInUse(tmp.path() + "/khac.bin"));
        CHECK(!QFile::exists(fullDest));
        CHECK(runOnce(fullId));
        CHECK(finishedOk.value(fullId));
        CHECK(readFileBytes(fullDest) == bodyV1);
        CHECK(!QFile::exists(fullDest + part));
        CHECK(downloader.item(fullId)->status == DownloadItem::Status::Completed);
        CHECK(downloader.item(fullId)->receivedBytes == bodyV1.size());
        CHECK(downloader.item(fullId)->totalBytes == bodyV1.size());
        // "Hủy" một mục ĐÃ XONG không được xóa tệp đã tải (trước đây xóa)
        downloader.cancel(fullId);
        CHECK(downloader.item(fullId)->status == DownloadItem::Status::Completed);
        CHECK(readFileBytes(fullDest) == bodyV1);

        // Hồi quy D2: mục thứ hai trùng tên phải nhận đường dẫn KHÁC dù mục trước chưa tạo tệp nào trên đĩa
        {
            const QString dupDest = tmp.path() + "/trung.jpg";
            const int queuedId = downloader.enqueue(http.url("/khong-bao-gio-tai.jpg"), dupDest); // KHÔNG start
            CHECK(!QFile::exists(dupDest));
            const QString second = FileDownloaderInternal::uniqueDestPath(tmp.path(), "trung.jpg", [&](const QString& p) {
                return QFile::exists(p) || downloader.isDestPathInUse(p);
            });
            CHECK(second == tmp.path() + "/trung (1).jpg");
            downloader.cancel(queuedId);
            CHECK(downloader.item(queuedId)->status == DownloadItem::Status::Canceled);
        }

        // (2) Lỗi HTTP: không để lại tệp đích lẫn tệp .part chứa nội dung trang lỗi
        const QString missingDest = tmp.path() + "/missing.bin";
        const int missingId = downloader.enqueue(http.url("/missing.bin"), missingDest);
        CHECK(runOnce(missingId));
        CHECK(!finishedOk.value(missingId, true));
        CHECK(!QFile::exists(missingDest));
        CHECK(!QFile::exists(missingDest + part));
        CHECK(downloader.item(missingId)->status == DownloadItem::Status::Failed);
        CHECK(!downloader.item(missingId)->error.isEmpty());

        // (3) Đứt giữa chừng rồi TIẾP TỤC: phần đã tải nằm trong .part (tên đích chưa hề xuất hiện), lần
        // sau gửi Range + If-Range và ghép ra đúng tệp gốc
        const QString cutDest = tmp.path() + "/cut.bin";
        const int cutId = downloader.enqueue(http.url("/cut.bin"), cutDest);
        CHECK(runOnce(cutId));
        CHECK(!finishedOk.value(cutId, true));
        CHECK(!QFile::exists(cutDest));                         // trước đây: tệp cụt mang luôn tên đích
        CHECK(QFileInfo(cutDest + part).size() == half);
        CHECK(readFileBytes(cutDest + part) == bodyV1.left(half));
        CHECK(runOnce(cutId));
        CHECK(finishedOk.value(cutId));
        CHECK(readFileBytes(cutDest) == bodyV1);
        CHECK(!QFile::exists(cutDest + part));
        CHECK(cutCount == 2);
        CHECK(MiniHttpServer::header(http.requests.last(), "Range") == "bytes=" + QByteArray::number(half) + "-");
        CHECK(MiniHttpServer::header(http.requests.last(), "If-Range") == "\"v1\"");

        // (4) Tệp trên máy chủ ĐỔI giữa hai lần: phải ra đúng bản mới, không phải nửa cũ ghép nửa mới
        const QString changedDest = tmp.path() + "/changed.bin";
        const int changedId = downloader.enqueue(http.url("/changed.bin"), changedDest);
        CHECK(runOnce(changedId));
        CHECK(!finishedOk.value(changedId, true));
        CHECK(runOnce(changedId));
        CHECK(finishedOk.value(changedId));
        CHECK(MiniHttpServer::header(http.requests.last(), "If-Range") == "\"v1\"");
        CHECK(readFileBytes(changedDest) == bodyV2);
        CHECK(downloader.item(changedId)->receivedBytes == bodyV2.size());

        // (5) Máy chủ không cho ETag/Last-Modified: không tiếp tục được - xóa .part, lần sau tải lại từ đầu
        const QString noValDest = tmp.path() + "/noval.bin";
        const int noValId = downloader.enqueue(http.url("/noval.bin"), noValDest);
        CHECK(runOnce(noValId));
        CHECK(!finishedOk.value(noValId, true));
        CHECK(!QFile::exists(noValDest + part));
        CHECK(runOnce(noValId));
        CHECK(finishedOk.value(noValId));
        CHECK(MiniHttpServer::header(http.requests.last(), "Range").isEmpty());
        CHECK(readFileBytes(noValDest) == bodyV2);

        // (6) Máy chủ trả 206 nhưng SAI đoạn: báo lỗi và bỏ phần đã tải, không ghép bừa rồi báo "Hoàn tất"
        const QString badDest = tmp.path() + "/badrange.bin";
        const int badId = downloader.enqueue(http.url("/badrange.bin"), badDest);
        CHECK(runOnce(badId));
        CHECK(!finishedOk.value(badId, true));
        CHECK(QFile::exists(badDest + part));
        CHECK(runOnce(badId));
        CHECK(!finishedOk.value(badId, true));
        CHECK(!QFile::exists(badDest));
        CHECK(!QFile::exists(badDest + part));

        // (7) Chỉ nhận http/https: không "tải" được một tệp cục bộ qua file://, không tạo tệp nào
        const QString localDest = tmp.path() + "/local.ini";
        const int requestsBefore = http.requests.size();
        const int localId = downloader.enqueue("file:///C:/Windows/win.ini", localDest);
        downloader.start(localId);
        CHECK(finishedCount.value(localId) == 1); // báo lỗi ngay, không cần chờ mạng
        CHECK(!finishedOk.value(localId, true));
        CHECK(!QFile::exists(localDest));
        CHECK(!QFile::exists(localDest + part));
        CHECK(http.requests.size() == requestsBefore);

        // Mỗi lượt chạy phát ĐÚNG MỘT itemFinished
        CHECK(finishedCount.value(fullId) == 1);
        CHECK(finishedCount.value(cutId) == 2);
    }

    // ---- PageMediaScanner::scan(): chạy THẬT với máy chủ HTTP cục bộ - luôn kết thúc bằng đúng một tín hiệu ----
    {
        MiniHttpServer http;
        CHECK(http.server.isListening());
        QByteArray hugePage = "<html><img src=\"/dau-trang.jpg\">";
        hugePage += QByteArray(static_cast<int>(PageMediaScannerInternal::kMaxPageBytes) + 1024 * 1024, 'x');
        hugePage += "<img src=\"/cuoi-trang.jpg\"></html>";
        http.handler = [&](const QByteArray& head) -> QByteArray {
            const QByteArray path = MiniHttpServer::path(head);
            if (path == "/trang.html")
                return MiniHttpServer::response("200 OK", {"Content-Type: text/html; charset=utf-8"},
                                                "<html><img src=\"anh/a.jpg?x=1&amp;y=2\"><a href=\"/tep.zip\">zip</a></html>");
            if (path == "/tep.zip")
                return MiniHttpServer::response("200 OK", {"Content-Type: application/zip"}, QByteArray(4096, 'z'));
            if (path == "/khong-lo.html")
                return MiniHttpServer::response("200 OK", {"Content-Type: text/html"}, hugePage);
            return MiniHttpServer::response("404 Not Found", {"Content-Type: text/html"}, "<html>404</html>");
        };

        PageMediaScanner scanner;
        int resultCount = 0, errorCount = 0;
        QList<MediaLink> lastLinks;
        QString lastError;
        QObject::connect(&scanner, &PageMediaScanner::result, [&](QList<MediaLink> links) {
            ++resultCount;
            lastLinks = links;
        });
        QObject::connect(&scanner, &PageMediaScanner::errorOccurred, [&](QString message) {
            ++errorCount;
            lastError = message;
        });
        auto scanAndWait = [&](const QString& url) {
            const int before = resultCount + errorCount;
            scanner.scan(url);
            return waitUntil([&]() { return resultCount + errorCount > before; });
        };

        // Trang HTML bình thường
        CHECK(scanAndWait(http.url("/trang.html")));
        CHECK(resultCount == 1 && errorCount == 0);
        QStringList urls;
        for (const auto& l : lastLinks)
            urls << l.url;
        CHECK(urls.contains(http.url("/anh/a.jpg?x=1&y=2")));
        CHECK(urls.contains(http.url("/tep.zip")));

        // Địa chỉ là một TỆP chứ không phải trang: báo lỗi rõ ràng thay vì "không tìm thấy gì"
        CHECK(scanAndWait(http.url("/tep.zip")));
        CHECK(resultCount == 1 && errorCount == 1);
        CHECK(lastError.contains("application/zip"));

        // Lỗi HTTP
        CHECK(scanAndWait(http.url("/khong-co")));
        CHECK(resultCount == 1 && errorCount == 2);

        // Scheme khác http/https: từ chối NGAY (nút "Quét" không bị kẹt khóa), không có yêu cầu mạng nào
        const int requestsBefore = http.requests.size();
        scanner.scan("file:///C:/Windows/win.ini");
        CHECK(resultCount == 1 && errorCount == 3);
        scanner.scan("khong phai url");
        CHECK(errorCount == 4);
        CHECK(http.requests.size() == requestsBefore);

        // Trang lớn hơn giới hạn: dừng đọc ở kMaxPageBytes, vẫn phân tích phần đã có
        CHECK(scanAndWait(http.url("/khong-lo.html")));
        CHECK(resultCount == 2 && errorCount == 4);
        urls.clear();
        for (const auto& l : lastLinks)
            urls << l.url;
        CHECK(urls.contains(http.url("/dau-trang.jpg")));
        CHECK(!urls.contains(http.url("/cuoi-trang.jpg")));

        // Quét xong vẫn quét tiếp được (không kẹt ở trạng thái "đang có yêu cầu")
        CHECK(scanAndWait(http.url("/trang.html")));
        CHECK(resultCount == 3);
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: khong co mang Internet/yt-dlp that trong test nay - YtDlpInfoWorker/YtDlpDownloadWorker\n");
    std::printf("(chay yt-dlp.exe that) can duoc tu kiem tra tay qua giao dien.\n");
    return g_fail == 0 ? 0 : 1;
}
