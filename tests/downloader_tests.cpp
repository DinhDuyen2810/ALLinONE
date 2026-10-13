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

#include "tools/downloader/engine/CookieFile.h"
#include "tools/downloader/engine/FileDownloader.h"
#include "tools/downloader/DownloadHistoryStore.h"
#include "tools/downloader/DownloaderSettings.h"
#include "tools/downloader/engine/DownloadQueue.h"
#include "tools/downloader/engine/HeadlessBrowser.h"
#include "tools/downloader/engine/LinkResolver.h"
#include "tools/downloader/engine/LinkResolverCore.h"
#include "tools/downloader/engine/PageMediaScanner.h"
#include "tools/downloader/engine/YtDlpController.h"
#include "tools/downloader/engine/YtDlpDownloadWorker.h"
#include "tools/downloader/engine/YtDlpInfoWorker.h"
#include "tools/downloader/model/DownloadItem.h"
#include "tools/downloader/model/MediaLink.h"
#include "tools/downloader/model/VideoInfo.h"

#include "MiniHttpServer.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

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

    // ---- Hồi quy các lỗi tìm thấy khi stress test (xem tests/secdl_stress_tests.cpp) ----
    {
        using namespace FileDownloaderInternal;
        // Cắt ngắn tên quá dài từng TẠO RA tên thiết bị dành riêng.
        CHECK(sanitizeFileName("NUL" + QString(150, ' ') + "x") == "_NUL");
        CHECK(sanitizeFileName("con" + QString(150, ' ') + "x.txt") == "_con.txt");
        // COM/LPT kèm chữ số mũ (¹ ² ³) cũng là tên dành riêng.
        CHECK(sanitizeFileName(QString("COM") + QChar(0xB9)) == QString("_COM") + QChar(0xB9));
        CHECK(sanitizeFileName(QString("lpt") + QChar(0xB3) + ".txt").startsWith('_'));
        // "nan"/"inf"/số ngoài tầm/số âm = chưa rõ (-1), không phải rác.
        const auto p = YtDlpDownloadWorkerInternal::parseProgressLine("DLPROG|nan|inf|1e999|-5|9223372036854775808");
        CHECK(p.ok && p.downloadedBytes == -1 && p.totalBytes == -1 && p.speedBytesPerSec == -1 && p.etaSeconds == -1);
        QString err;
        const VideoInfo huge = YtDlpInfoWorkerInternal::parseInfoJson(
            R"({"title":"x","duration":1e300,"formats":[{"format_id":"1","filesize":1e300}]})", &err);
        CHECK(err.isEmpty() && huge.formats.size() == 1 && huge.formats[0].approxSizeBytes == -1 && huge.durationSeconds == 0);
    }

    // ====================================================================================================
    // LinkResolverCore - phần THUẦN của bộ phân giải link. Dữ liệu mẫu LẤY TỪ DỮ LIỆU THẬT thu thập khi thử
    // các link người dùng gửi (trang phim motchill, trang Drive "không được phép tải", ảnh Facebook/TikTok
    // do Edge dựng, JSON của yt-dlp, net-log của trình duyệt).
    // ====================================================================================================
    {
        using namespace LinkResolverCore;

        // ---- Nhập liệu: tách nhiều link từ đoạn văn dán vào (đúng 6 link người dùng đã gửi + chữ thừa) ----
        const QString pasted =
            "https://www.facebook.com/ctsvdhbkhn/videos/1068033449194736\n"
            "https://www.facebook.com/qynhmaiiiz\r\n"
            "  https://motchillt.one/phim/harry-potter-va-ten-tu-nhan-nguc-azkaban/tap-full-sv-0  \n"
            "https://www.tiktok.com/@kiue.oahn/photo/7683433150571121940\n"
            "https://www.studocu.vn/vn/document/university-of-transportation-hcmc/railway/giao-trinh-giai-tich-1/35467985?sid=6c5a5326-b09f-42ad-92b8-31908e5823df1791906882\n"
            "https://drive.google.com/file/d/1ObltgwgTX8QHw7S36wFToyNJNUdosyKn/view\n"
            "và các trang khác nữa\n"
            "https://www.facebook.com/qynhmaiiiz\n"; // trùng -> bỏ
        const QStringList urls = extractUrls(pasted);
        CHECK(urls.size() == 6);
        if (urls.size() == 6)
        {
            CHECK(urls[0].endsWith("/videos/1068033449194736"));
            CHECK(urls[2].startsWith("https://motchillt.one/phim/"));
            CHECK(urls[4].contains("?sid=6c5a5326"));
            CHECK(urls[5].contains("drive.google.com"));
        }
        CHECK(extractUrls("(xem https://a.com/x), rồi www.b.com/y; c.org/z.").size() == 3);
        CHECK(extractUrls("(xem https://a.com/x), rồi www.b.com/y; c.org/z.")[0] == "https://a.com/x");
        CHECK(extractUrls("abc.jpg hello world file.txt").isEmpty()); // tên tệp KHÔNG phải link
        CHECK(extractUrls("javascript:alert(1) file:///c:/x ftp://a/b").isEmpty());
        CHECK(extractUrls("").isEmpty());

        // ---- Google Drive / Docs ----
        QString id;
        CHECK(parseDriveFileId("https://drive.google.com/file/d/1ObltgwgTX8QHw7S36wFToyNJNUdosyKn/view", &id) &&
              id == "1ObltgwgTX8QHw7S36wFToyNJNUdosyKn");
        CHECK(parseDriveFileId("https://drive.google.com/open?id=1ObltgwgTX8QHw7S36wFToyNJNUdosyKn", &id) && id.size() == 33);
        CHECK(parseDriveFileId("https://drive.google.com/uc?export=download&id=1ObltgwgTX8QHw7S36wFToyNJNUdosyKn", &id));
        CHECK(parseDriveFileId("https://drive.google.com/u/0/file/d/1ObltgwgTX8QHw7S36wFToyNJNUdosyKn/edit", &id));
        CHECK(!parseDriveFileId("https://drive.google.com/drive/folders/1ObltgwgTX8QHw7S36wFToyNJNUdosyKn", &id)); // thư mục
        CHECK(!parseDriveFileId("https://example.com/file/d/1ObltgwgTX8QHw7S36wFToyNJNUdosyKn", &id)); // host lạ
        CHECK(driveDirectUrl("ABC").contains("drive.usercontent.google.com/download?id=ABC") && driveDirectUrl("ABC").contains("confirm=t"));
        QString exportUrl, ext;
        CHECK(googleDocsExportUrl("https://docs.google.com/document/d/1ObltgwgTX8QHw7S36wFToyNJNUdosyKn/edit", &exportUrl, &ext) &&
              ext == "pdf" && exportUrl.endsWith("/export?format=pdf"));
        CHECK(googleDocsExportUrl("https://docs.google.com/spreadsheets/d/1ObltgwgTX8QHw7S36wFToyNJNUdosyKn/edit#gid=0", &exportUrl, &ext) && ext == "xlsx");
        CHECK(googleDocsExportUrl("https://docs.google.com/presentation/d/1ObltgwgTX8QHw7S36wFToyNJNUdosyKn/edit", &exportUrl, &ext) && ext == "pptx");
        CHECK(!googleDocsExportUrl("https://docs.google.com/forms/d/1ObltgwgTX8QHw7S36wFToyNJNUdosyKn", &exportUrl, &ext));

        // Trang THẬT Drive trả khi chủ tệp không cho tải (đã thu qua GET): phải nhận ra là "không được phép".
        const QString driveNoPerm =
            "<!DOCTYPE html><html><head><title>Google Drive - Can&#39;t download file</title></head><body><div class=\"uc-main\">"
            "<div id=\"uc-text\"><p class=\"uc-error-caption\">Sorry, the owner hasn&#39;t given you permission to download this file.</p>"
            "<p class=\"uc-error-subcaption\">Only the owner and editors can download this file.</p></div></div></body></html>";
        CHECK(classifyDriveHtml(driveNoPerm) == DriveBlock::NoPermission);
        CHECK(!driveBlockMessage(DriveBlock::NoPermission).isEmpty() && driveBlockMessage(DriveBlock::None).isEmpty());
        CHECK(classifyDriveHtml("<p>Quota exceeded for this file</p>") == DriveBlock::Quota);
        CHECK(classifyDriveHtml("<html><body>ok file</body></html>") == DriveBlock::None);
        const QString driveConfirm =
            "<html><body><form id=\"download-form\" action=\"https://drive.usercontent.google.com/download\" method=\"get\">"
            "<input type=\"hidden\" name=\"id\" value=\"1ABC\"><input type=\"hidden\" name=\"export\" value=\"download\">"
            "<input type=\"hidden\" name=\"confirm\" value=\"t\"><input type=\"hidden\" name=\"uuid\" value=\"u-123\">"
            "<input type=\"submit\" value=\"Download anyway\"></form></body></html>";
        CHECK(classifyDriveHtml(driveConfirm) == DriveBlock::NeedsConfirm);
        const QString confirmUrl = driveConfirmUrl(driveConfirm, "https://drive.google.com/uc?id=1ABC");
        CHECK(confirmUrl.startsWith("https://drive.usercontent.google.com/download?") && confirmUrl.contains("id=1ABC") &&
              confirmUrl.contains("confirm=t") && confirmUrl.contains("uuid=u-123") && !confirmUrl.contains("submit"));
        CHECK(driveConfirmUrl("<html>không có form</html>", "https://x.com/").isEmpty());

        // ---- Loại link ----
        CHECK(siteOf("https://www.facebook.com/x") == Site::Facebook && siteOf("https://fb.watch/abc") == Site::Facebook);
        CHECK(siteOf("https://www.tiktok.com/@a") == Site::TikTok && siteOf("https://youtu.be/abc") == Site::YouTube);
        CHECK(siteOf("https://drive.google.com/file/d/x") == Site::GoogleDrive && siteOf("https://notfacebook.com/") == Site::Generic);
        CHECK(isLikelyProfileOrCollectionUrl("https://www.tiktok.com/@kiue.oahn"));
        CHECK(!isLikelyProfileOrCollectionUrl("https://www.tiktok.com/@kiue.oahn/photo/7683433150571121940"));
        CHECK(!isLikelyProfileOrCollectionUrl("https://www.tiktok.com/@kiue.oahn/video/7696088359294291220"));
        CHECK(isLikelyProfileOrCollectionUrl("https://www.youtube.com/@YouTube/videos"));
        CHECK(isLikelyProfileOrCollectionUrl("https://www.youtube.com/channel/UC123456"));
        CHECK(isLikelyProfileOrCollectionUrl("https://www.youtube.com/playlist?list=PL123"));
        CHECK(!isLikelyProfileOrCollectionUrl("https://www.youtube.com/watch?v=abc&list=PL123")); // video trong danh sách
        CHECK(isLikelyProfileOrCollectionUrl("https://www.instagram.com/instagram/"));
        CHECK(!isLikelyProfileOrCollectionUrl("https://www.instagram.com/p/ABC123/") && !isLikelyProfileOrCollectionUrl("https://www.instagram.com/reel/ABC/"));
        CHECK(isLikelyProfileOrCollectionUrl("https://www.facebook.com/qynhmaiiiz"));
        CHECK(isLikelyProfileOrCollectionUrl("https://www.facebook.com/qynhmaiiiz/videos"));
        CHECK(isLikelyProfileOrCollectionUrl("https://www.facebook.com/profile.php?id=100012345"));
        CHECK(!isLikelyProfileOrCollectionUrl("https://www.facebook.com/ctsvdhbkhn/videos/1068033449194736")); // một video
        CHECK(!isLikelyProfileOrCollectionUrl("https://www.facebook.com/photo/?fbid=1935618970974748&set=pcb.1935625414307437"));
        CHECK(!isLikelyProfileOrCollectionUrl("https://www.facebook.com/watch?v=123"));
        CHECK(!isLikelyProfileOrCollectionUrl("https://example.com/qynhmaiiiz"));
        CHECK(isPhotoPostUrl("https://www.facebook.com/photo/?fbid=1935618970974748&set=pcb.1935625414307437"));
        CHECK(isPhotoPostUrl("https://www.facebook.com/someone/photos/a.123/456/"));
        CHECK(isPhotoPostUrl("https://www.tiktok.com/@kiue.oahn/photo/7683433150571121940"));
        CHECK(!isPhotoPostUrl("https://www.facebook.com/ctsvdhbkhn/videos/1068033449194736") && !isPhotoPostUrl("https://example.com/photo/1"));
        CHECK(isManifestUrl("https://s2.phim1280.tv/20240216/pIR2UX2P/index.m3u8?x=1") && isManifestUrl("https://a/b/manifest.mpd"));
        CHECK(!isManifestUrl("https://a/b/video.mp4"));

        // ---- deepScanHtml: trang phim THẬT - link m3u8 chỉ nằm trong mã script/iframe, thẻ HTML không có ----
        const QString movieHtml =
            "<html><head><title>Harry Potter - Xem phim</title><meta property=\"og:image\" content=\"https://img.example.com/poster.jpg\">"
            "<script>var player = {\"sources\":[{\"file\":\"https:\\/\\/s2.phim1280.tv\\/20240216\\/pIR2UX2P\\/index.m3u8\",\"type\":\"hls\"}],"
            "\"tracks\":[{\"file\":\"//sub.example.com/vi.vtt\"}]};</script></head><body>"
            "<div id=\"main\"><iframe src=\"https://player.phimapi.com/player/?url=https://s2.phim1280.tv/20240216/pIR2UX2P/index.m3u8\"></iframe>"
            "<iframe src=\"https://www.googletagmanager.com/ns.html?id=GTM-X\"></iframe><iframe src=\"about:blank\"></iframe>"
            "<a href=\"/phim/khac\">phim khác</a><img src=\"/thumb/1.jpg\"></div></body></html>";
        const DeepScan ds = deepScanHtml(movieHtml, "https://motchillt.one/phim/harry-potter/tap-full-sv-0");
        int m3u8Count = 0;
        bool sawPoster = false, sawThumb = false;
        for (const MediaLink& l : ds.media)
        {
            if (l.url == "https://s2.phim1280.tv/20240216/pIR2UX2P/index.m3u8" && l.type == MediaLink::Type::Video) ++m3u8Count;
            if (l.url == "https://img.example.com/poster.jpg") sawPoster = true;
            if (l.url == "https://motchillt.one/thumb/1.jpg") sawThumb = true;
        }
        CHECK(m3u8Count == 1); // thấy ĐÚNG MỘT lần (iframe lồng url=... cũng trỏ cùng link, không nhân đôi)
        CHECK(sawPoster && sawThumb);
        CHECK(ds.title == "Harry Potter - Xem phim");
        CHECK(ds.iframes.size() == 1 && ds.iframes[0].startsWith("https://player.phimapi.com/player/")); // bỏ GTM + about:blank
        // URL mã hóa phần trăm lồng trong tham số, và đường dẫn tương đối trong chuỗi script.
        const DeepScan ds2 = deepScanHtml(
            "<script>load('https://p.example.com/embed?src=https%3A%2F%2Fcdn.example.com%2Fv%2Fmaster.m3u8%3Ftoken%3Dabc');"
            "var rel=\"/hls/index.m3u8\"; var doc='https://files.example.com/g/bai-giang.pdf?dl=1';</script>",
            "https://site.example.com/xem/1");
        QStringList urls2;
        for (const MediaLink& l : ds2.media) urls2 << l.url;
        CHECK(urls2.contains("https://cdn.example.com/v/master.m3u8?token=abc"));
        CHECK(urls2.contains("https://site.example.com/hls/index.m3u8"));
        CHECK(urls2.contains("https://files.example.com/g/bai-giang.pdf?dl=1"));
        CHECK(deepScanHtml("<html><body>chỉ có chữ</body></html>", "https://x.com/").media.isEmpty());
        // Trang rác/nhị phân không làm crash và không sinh link.
        CHECK(deepScanHtml(QString(5000, QChar(0xFFFD)), "https://x.com/").media.isEmpty());

        CHECK(looksLikeChallengePage("<html><head><title>Just a moment...</title></head><body>cloudflare</body></html>"));
        CHECK(looksLikeChallengePage("<script src=\"/cdn-cgi/challenge-platform/h/b/orchestrate\"></script>"));
        CHECK(!looksLikeChallengePage("<html><head><title>Giáo trình</title></head></html>"));
        CHECK(looksLikeLoginWall("<html><head><title>Log in to Facebook</title></head></html>"));
        CHECK(looksLikeLoginWall("<form><input type=\"password\" name=\"p\"></form>"));
        CHECK(!looksLikeLoginWall("<html><head><title>Bài viết</title></head><body>nội dung</body></html>"));
        CHECK(metaContent("<meta property=\"og:title\" content=\"A &amp; B\">", "og:title") == "A & B");
        CHECK(metaContent("<meta content=\"x\" name=\"description\">", "description") == "x");
        CHECK(metaContent("<meta property=\"og:title\" content=\"A\">", "og:image").isEmpty());

        // ---- Net-log của trình duyệt: manifest xếp TRƯỚC, bỏ đoạn .ts/.m4s ----
        const QByteArray netLog =
            R"({"events":[{"params":{"url":"https://motchillt.one/phim/x"}},)"
            R"({"params":{"url":"https://s2.phim1280.tv/20240216/pIR2UX2P/3000kb/hls/4sqlp1HL.ts"}},)"
            R"({"params":{"url":"https://cdn.example.com/a/video.mp4"}},)"
            R"({"params":{"url":"https://s2.phim1280.tv/20240216/pIR2UX2P/index.m3u8"}},)"
            R"({"params":{"url":"https://s2.phim1280.tv/20240216/pIR2UX2P/index.m3u8"}},)"
            R"({"params":{"url":"https://cdn.example.com/seg/1.m4s"}},)"
            R"({"params":{"url":"https://cdn.example.com/a/init.mp4"}},)"
            R"({"params":{"url":"https:\/\/cdn.example.com\/escaped\/audio.mp3"}}]})";
        const QStringList nl = mediaUrlsFromNetLog(netLog);
        CHECK(nl.size() == 3);
        if (nl.size() == 3)
        {
            CHECK(nl[0] == "https://s2.phim1280.tv/20240216/pIR2UX2P/index.m3u8"); // manifest đầu tiên, không trùng
            CHECK(nl[1] == "https://cdn.example.com/a/video.mp4");
            CHECK(nl[2] == "https://cdn.example.com/escaped/audio.mp3");
        }
        // Tệp media của GIAO DIỆN trang (video nền màn hình đăng nhập TikTok - rác THẬT gặp khi thử) bị loại.
        CHECK(isJunkMediaUrl("https://sf16-website-login.neutral.ttwstatic.com/obj/tiktok_web_login_static/tiktok/webapp/main/webapp-desktop/playback1.mp4"));
        CHECK(isJunkMediaUrl("https://static.example.com/a/b.mp4") && isJunkMediaUrl("https://x.com/static/intro.mp4"));
        CHECK(!isJunkMediaUrl("https://s2.phim1280.tv/20240216/pIR2UX2P/index.m3u8") && !isJunkMediaUrl("https://cdn.example.com/a/video.mp4"));
        CHECK(mediaUrlsFromNetLog(R"({"u":{"url":"https://sf16-website-login.neutral.ttwstatic.com/obj/x/playback1.mp4"}})").isEmpty());
        CHECK(mediaUrlsFromNetLog("không phải json").isEmpty() && mediaUrlsFromNetLog("").isEmpty());
        CHECK(mediaUrlsFromNetLog(netLog, 1).size() == 1);

        // ---- Ảnh trong DOM do trình duyệt dựng (Facebook / TikTok): chỉ giữ ảnh NỘI DUNG ----
        const QString fbDom =
            "<img class=\"x16dsc37\" src=\"data:image/svg+xml,%3Csvg%3E\">"
            "<img data-visualcompletion=\"media-vc-image\" alt=\"May be an image\" src=\"https://scontent.fhan14-1.fna.fbcdn.net/v/t39.30808-6/825279897_1935618984308080_7553907963909847904_n.jpg?stp=dst-jpg_tt6&amp;cstp=mx2048x1365&amp;ctp=s2048x1365\">"
            "<img src=\"https://scontent.fhan2-5.fna.fbcdn.net/v/t39.30808-1/486156815_n.jpg?stp=cp0_dst-jpg_tt6&amp;cstp=mx100x100\">"
            "<img src=\"https://static.xx.fbcdn.net/rsrc.php/v4/y5/r/icon.png\">";
        const QList<MediaLink> fbPhotos = photosFromDom(fbDom, "https://www.facebook.com/photo/?fbid=1");
        CHECK(fbPhotos.size() == 1);
        if (fbPhotos.size() == 1)
            CHECK(fbPhotos[0].url.contains("825279897_1935618984308080") && fbPhotos[0].url.contains("&cstp=mx2048x1365") && fbPhotos[0].type == MediaLink::Type::Image);
        const QString ttDom =
            "<img src=\"https://p19-common-sign.tiktokcdn.com/tos-alisg-i-photomode-sg/ba22b371f95a496d959371e9b9f3a284~tplv-photomode-image.jpeg?dr=14555&amp;x-expires=1\">"
            "<img src=\"https://p16-common-sign.tiktokcdn.com/tos-alisg-i-photomode-sg/f98b95220e8545299647ceddcc155b72~tplv-photomode-image.jpeg?dr=14555\">"
            "<img src=\"https://p19-common-sign.tiktokcdn.com/tos-alisg-i-photomode-sg/ba22b371f95a496d959371e9b9f3a284~tplv-photomode-image.jpeg?dr=999\">" // cùng ảnh, tham số khác
            "<img src=\"https://p16-common-sign.tiktokcdn.com/tos-alisg-avt-0068/592a031e~tplv-tiktokx-cropcenter:100:100.jpeg?dr=14579\">";
        const QList<MediaLink> ttPhotos = photosFromDom(ttDom, "https://www.tiktok.com/@kiue.oahn/photo/7683433150571121940");
        CHECK(ttPhotos.size() == 2); // hai ảnh bài đăng; bỏ avatar cắt giữa và ảnh trùng
        const QList<MediaLink> generic = photosFromDom(
            "<img src=\"/a/chart.png\"><img src=\"/img/logo.png\"><img src=\"/u/avatar-1.jpg\"><img srcset=\"/p/s.jpg 1x, /p/big.jpg 2x\">",
            "https://news.example.com/bai");
        CHECK(generic.size() == 2 && generic[0].url == "https://news.example.com/a/chart.png" && generic[1].url == "https://news.example.com/p/big.jpg");
        CHECK(photosFromDom(ttDom, "https://www.tiktok.com/x", 1).size() == 1);

        // ---- yt-dlp: đối số + JSON + lỗi ----
        const QStringList an = ytDlpAnalyzeArguments("https://www.facebook.com/x/videos/1", 50, {"--cookies-from-browser", "firefox"});
        CHECK(an.contains("--dump-single-json") && an.contains("--flat-playlist") && an.contains("--no-playlist"));
        CHECK(an.indexOf("--") > an.indexOf("--cookies-from-browser") && an.last() == "https://www.facebook.com/x/videos/1" && an[an.size() - 2] == "--");
        CHECK(an.contains("--ignore-config") && an[an.indexOf("--playlist-end") + 1] == "50");
        const QStringList an2 = ytDlpAnalyzeArguments("https://www.tiktok.com/@kiue.oahn", 20, {});
        CHECK(!an2.contains("--no-playlist")); // hồ sơ: cần liệt kê nhiều mục
        CHECK(ytDlpAnalyzeArguments("https://x.com/-evil", 5, {}).last() == "https://x.com/-evil");

        // Video đơn (rút gọn từ JSON THẬT của một video Facebook).
        const QByteArray fbJson = R"({"_type":"video","extractor_key":"Facebook","title":"Lễ kỷ niệm | CTSV","ext":"mp4",
            "webpage_url":"https://www.facebook.com/ctsvdhbkhn/videos/1068033449194736","vcodec":"avc1","duration":62.5,
            "formats":[{"format_id":"sd","filesize_approx":5000000},{"format_id":"hd","filesize_approx":12345678}]})";
        const ResolveResult fbRes = parseYtDlpJson(fbJson, "https://fb.watch/abc", 100);
        CHECK(fbRes.ok() && fbRes.items.size() == 1 && !fbRes.isCollection);
        if (fbRes.items.size() == 1)
        {
            const ResolvedMedia& m = fbRes.items[0];
            CHECK(m.kind == ResolvedMedia::Kind::YtDlp && m.url == "https://www.facebook.com/ctsvdhbkhn/videos/1068033449194736");
            CHECK(m.mediaType == "Video" && m.size == 12345678 && m.note == "yt-dlp (Facebook)" && m.referer == "https://fb.watch/abc");
            CHECK(m.fileName.endsWith(".mp4") && !m.fileName.contains('|') && m.title == "Lễ kỷ niệm | CTSV");
        }
        // Hồ sơ/danh sách phát (rút gọn từ JSON THẬT của hồ sơ TikTok), cắt theo giới hạn.
        QByteArray plJson = R"({"_type":"playlist","extractor_key":"TikTokUser","title":"kiue.oahn","entries":[)";
        for (int i = 0; i < 5; ++i)
            plJson += QByteArray(i ? "," : "") + R"({"url":"https://www.tiktok.com/@kiue.oahn/video/769608835929429122)" + QByteArray::number(i) + R"(","title":"#xuhuong )" + QByteArray::number(i) + R"("})";
        plJson += "]}";
        const ResolveResult plRes = parseYtDlpJson(plJson, "https://www.tiktok.com/@kiue.oahn", 100);
        CHECK(plRes.ok() && plRes.isCollection && plRes.items.size() == 5 && !plRes.truncated);
        const ResolveResult plCut = parseYtDlpJson(plJson, "https://www.tiktok.com/@kiue.oahn", 3);
        CHECK(plCut.ok() && plCut.items.size() == 3 && plCut.truncated);
        // Mục YouTube chỉ có id + ie_key; mục có url không hợp lệ bị bỏ.
        const ResolveResult ytRes = parseYtDlpJson(
            R"({"_type":"playlist","entries":[{"id":"71ya_JC_IbM","ie_key":"Youtube","title":"A"},{"url":"javascript:x","title":"B"},{"url":"file:///c:/x"}]})",
            "https://www.youtube.com/@YouTube/videos", 10);
        CHECK(ytRes.ok() && ytRes.items.size() == 1 && ytRes.items[0].url == "https://www.youtube.com/watch?v=71ya_JC_IbM");
        CHECK(!parseYtDlpJson(R"({"_type":"playlist","entries":[]})", "https://x.com/", 10).ok());
        CHECK(!parseYtDlpJson("không phải json", "https://x.com/", 10).ok() && !parseYtDlpJson("", "https://x.com/", 10).error.isEmpty());
        CHECK(!parseYtDlpJson("[1,2]", "https://x.com/", 10).ok());
        // Âm thanh: vcodec = none.
        const ResolveResult au = parseYtDlpJson(R"({"title":"Bài hát","ext":"m4a","vcodec":"none","webpage_url":"https://x.com/a"})", "https://x.com/a", 10);
        CHECK(au.ok() && au.items[0].mediaType == "Âm thanh");

        bool uns = false;
        CHECK(translateYtDlpError("ERROR: Unsupported URL: https://www.facebook.com/photo/?fbid=1", &uns).contains("không hỗ trợ") && uns);
        CHECK(translateYtDlpError("ERROR: [generic] 35467985: Unable to download webpage: HTTP Error 403: Forbidden", &uns).contains("403") && !uns);
        // Lỗi không có mẫu riêng: trả NGUYÊN dòng ERROR cuối (không nuốt thông tin) và không coi là "không hỗ trợ".
        CHECK(translateYtDlpError("ERROR: [GoogleDrive] x: Unable to download JSON metadata: HTTP Error 400: Bad Request", &uns).contains("HTTP Error 400") && !uns);
        CHECK(translateYtDlpError("ERROR: [instagram:user] instagram: Unable to extract data; please report this issue", &uns).contains("đăng nhập"));
        CHECK(translateYtDlpError("ERROR: [youtube] abc: Video unavailable", &uns).contains("không còn khả dụng"));
        CHECK(translateYtDlpError("ERROR: Login required to view this", &uns).contains("Cài đặt"));
        CHECK(translateYtDlpError("ERROR: HTTP Error 404: Not Found", &uns).contains("404"));
        CHECK(!translateYtDlpError("", &uns).isEmpty() && !translateYtDlpError("lỗi lạ hoàn toàn", &uns).isEmpty());

        // ---- Tên tệp từ Content-Disposition + nhãn loại ----
        CHECK(fileNameFromContentDisposition("attachment; filename=\"bai giang.pdf\"") == "bai giang.pdf");
        CHECK(fileNameFromContentDisposition("attachment; filename*=UTF-8''Gi%C3%A1o%20tr%C3%ACnh.pdf") == "Giáo trình.pdf");
        CHECK(fileNameFromContentDisposition("attachment; filename=plain.zip; size=1") == "plain.zip");
        CHECK(fileNameFromContentDisposition("attachment; filename=\"..\\\\..\\\\evil.bat\"") == "evil.bat"); // làm sạch đường dẫn
        CHECK(fileNameFromContentDisposition("inline").isEmpty());
        CHECK(friendlyTypeLabel("image/jpeg", "https://x/a") == "Ảnh" && friendlyTypeLabel("video/mp4", "") == "Video");
        CHECK(friendlyTypeLabel("application/vnd.apple.mpegurl", "") == "Video" && friendlyTypeLabel("audio/mpeg", "") == "Âm thanh");
        CHECK(friendlyTypeLabel("application/pdf", "") == "Tài liệu" && friendlyTypeLabel("application/octet-stream", "https://x/a.mp3") == "Âm thanh");
        CHECK(friendlyTypeLabel("application/octet-stream", "https://x/a.bin") == "Tệp" && friendlyTypeLabel("", "https://x/i.m3u8") == "Video");
    }

    // ====================================================================================================
    // LinkResolver (bất đồng bộ) + HeadlessBrowser (đối số) - chỉ nói chuyện với MiniHttpServer trên 127.0.0.1.
    // yt-dlp và trình duyệt ẩn TẮT trong test này (không phụ thuộc exe ngoài); phần đó phải tự kiểm tra tay.
    // ====================================================================================================
    {
        MiniHttpServer srv;
        QList<QByteArray> heads;
        srv.handler = [&](const QByteArray& head) -> QByteArray {
            heads << head;
            const QByteArray path = MiniHttpServer::path(head);
            const QByteArray base = "http://127.0.0.1:" + QByteArray::number(srv.server.serverPort());
            const QList<QByteArray> html = {"Content-Type: text/html; charset=utf-8"};
            if (path == "/img.png")
                return MiniHttpServer::response("200 OK", {"Content-Type: image/png"}, QByteArray(1000, 'x'));
            if (path == "/dl?id=1")
                return MiniHttpServer::response("200 OK", {"Content-Type: application/octet-stream",
                                                            "Content-Disposition: attachment; filename=\"bao cao.pdf\""},
                                                QByteArray(500, 'y'));
            if (path == "/noext")
                return MiniHttpServer::response("200 OK", {"Content-Type: image/jpeg"}, QByteArray(10, 'z'));
            if (path == "/missing")
                return MiniHttpServer::response("404 Not Found", html, "<html>khong co</html>");
            if (path == "/page-hls")
                return MiniHttpServer::response("200 OK", html,
                    "<html><head><title>Phim hay</title></head><body><script>var p={\"file\":\"" + base + "\\/hls\\/index.m3u8\"};</script></body></html>");
            if (path == "/page-frame")
                return MiniHttpServer::response("200 OK", html, "<html><head><title>Co khung</title></head><body><iframe src=\"/frame\"></iframe></body></html>");
            if (path == "/frame")
                return MiniHttpServer::response("200 OK", html, "<html><script>play('" + base + "/v/video.mp4')</script></html>");
            if (path == "/empty")
                return MiniHttpServer::response("200 OK", html, "<html><body>chi co chu</body></html>");
            if (path == "/cf")
                return MiniHttpServer::response("403 Forbidden", html, "<html><head><title>Just a moment...</title></head><body>cloudflare</body></html>");
            if (path == "/doc-page")
                return MiniHttpServer::response("200 OK", html, "<html><body><a href=\"/files/giao-trinh.pdf\">tai</a></body></html>");
            return MiniHttpServer::response("404 Not Found", html, "x");
        };

        auto resolveSync = [&](const QString& url, int* finishedCount = nullptr) {
            LinkResolver r;
            LinkResolver::Options o;
            o.useYtDlp = false;
            o.useBrowser = false;
            r.setOptions(o);
            ResolveResult out;
            int count = 0;
            QObject::connect(&r, &LinkResolver::finished, [&](ResolveResult res) { out = res; ++count; });
            r.resolve(url);
            waitUntil([&] { return count > 0; }, 20000);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50); // chờ thêm xem có tín hiệu thứ hai không
            if (finishedCount) *finishedCount = count;
            return out;
        };

        int cnt = 0;
        // Tệp trực tiếp: tên từ URL, kích thước từ Content-Length, nhãn loại từ Content-Type.
        ResolveResult r = resolveSync(srv.url("/img.png"), &cnt);
        CHECK(cnt == 1 && r.ok() && r.items.size() == 1);
        if (r.ok())
        {
            CHECK(r.items[0].kind == ResolvedMedia::Kind::HttpFile && r.items[0].fileName == "img.png" && r.items[0].size == 1000);
            CHECK(r.items[0].mediaType == "Ảnh" && r.items[0].url == srv.url("/img.png") && r.items[0].note == "tệp trực tiếp");
        }
        // Yêu cầu dùng UA giống trình duyệt (nhiều trang chặn UA lạ).
        CHECK(!heads.isEmpty() && MiniHttpServer::header(heads.first(), "User-Agent").contains("Mozilla/5.0"));
        // Content-Disposition quyết định tên tệp.
        r = resolveSync(srv.url("/dl?id=1"));
        CHECK(r.ok() && r.items[0].fileName == "bao cao.pdf" && r.items[0].size == 500 && r.items[0].mediaType == "Tài liệu");
        // URL không có đuôi: suy đuôi từ Content-Type.
        r = resolveSync(srv.url("/noext"));
        CHECK(r.ok() && r.items[0].fileName.endsWith(".jpg"));
        // 404 -> ERR ngay, nêu rõ mã.
        r = resolveSync(srv.url("/missing"));
        CHECK(!r.ok() && r.error.contains("404") && r.items.isEmpty());
        // Link .m3u8 chỉ nằm trong chuỗi script (thẻ HTML không có) -> bắt được, giao cho yt-dlp, tên theo tiêu đề trang.
        r = resolveSync(srv.url("/page-hls"));
        CHECK(r.ok() && r.items.size() == 1);
        if (r.ok() && r.items.size() == 1)
        {
            CHECK(r.items[0].kind == ResolvedMedia::Kind::YtDlp && r.items[0].url == srv.url("/hls/index.m3u8"));
            CHECK(r.items[0].mediaType == "Video" && r.items[0].fileName == "Phim hay.mp4" && r.items[0].referer == srv.url("/page-hls"));
            CHECK(r.items[0].note == "mã nguồn trang");
        }
        // Chỉ có iframe: tải tiếp khung, tìm thấy video; Referer gửi đi = trang cha, referer của mục = trang khung.
        heads.clear();
        r = resolveSync(srv.url("/page-frame"));
        CHECK(r.ok() && r.items.size() == 1);
        if (r.ok() && r.items.size() == 1)
        {
            CHECK(r.items[0].url == srv.url("/v/video.mp4") && r.items[0].kind == ResolvedMedia::Kind::HttpFile);
            CHECK(r.items[0].referer == srv.url("/frame") && r.items[0].note == "khung nhúng");
        }
        bool frameReqHadReferer = false;
        for (const QByteArray& h : heads)
            if (MiniHttpServer::path(h) == "/frame" && MiniHttpServer::header(h, "Referer") == srv.url("/page-frame").toUtf8())
                frameReqHadReferer = true;
        CHECK(frameReqHadReferer);
        // Liên kết tài liệu <a href=...pdf> trong trang.
        r = resolveSync(srv.url("/doc-page"));
        CHECK(r.ok() && r.items.size() == 1 && r.items[0].url == srv.url("/files/giao-trinh.pdf") && r.items[0].mediaType == "Tài liệu");
        // Trang không có gì: ERR có thông điệp hữu ích (đã thử gì).
        r = resolveSync(srv.url("/empty"));
        CHECK(!r.ok() && r.error.contains("Không tìm thấy") && r.error.contains("quét mã nguồn"));
        // Trang chặn kiểu Cloudflare: nói rõ lý do thay vì "không có gì".
        r = resolveSync(srv.url("/cf"));
        CHECK(!r.ok() && r.error.contains("Cloudflare"));
        // URL không hợp lệ: ERR bất đồng bộ, đúng một tín hiệu.
        for (const char* bad : {"", "ftp://x.com/a", "file:///c:/windows/win.ini", "javascript:alert(1)", "http://"})
        {
            r = resolveSync(bad, &cnt);
            CHECK(cnt == 1 && !r.ok() && r.error.contains("không hợp lệ"));
        }
        // Máy chủ không tồn tại (cổng đóng): ERR, không treo.
        r = resolveSync("http://127.0.0.1:1/x", &cnt);
        CHECK(cnt == 1 && !r.ok() && r.error.contains("Không truy cập được"));

        // Hủy giữa chừng: đúng một tín hiệu "Đã hủy.", không có tín hiệu thứ hai khi yêu cầu cũ về muộn.
        {
            LinkResolver rr;
            LinkResolver::Options o;
            o.useYtDlp = false;
            o.useBrowser = false;
            rr.setOptions(o);
            int c = 0;
            ResolveResult res;
            QObject::connect(&rr, &LinkResolver::finished, [&](ResolveResult x) { res = x; ++c; });
            rr.resolve(srv.url("/img.png"));
            rr.cancel();
            waitUntil([] { return false; }, 150);
            CHECK(c == 1 && !res.ok() && res.error == "Đã hủy." && !rr.isRunning());
            rr.cancel(); // hủy lại khi đã xong: không phát thêm
            CHECK(c == 1);
        }

        // Trình duyệt ẩn: đối số (không chạy Edge thật trong test).
        const QStringList ba = HeadlessBrowserInternal::buildArguments("https://x.com/a", "C:/tmp/profile", "C:/tmp/net.json");
        CHECK(ba.contains("--headless=new") && ba.contains("--dump-dom") && ba.contains("--user-data-dir=C:/tmp/profile"));
        CHECK(ba.contains("--log-net-log=C:/tmp/net.json") && ba.last() == "https://x.com/a");
        CHECK(ba.contains("--autoplay-policy=no-user-gesture-required") && ba.contains("--disable-extensions"));
        CHECK(!HeadlessBrowserInternal::candidatePaths().isEmpty());
        (void)HeadlessBrowser::findBrowserExecutable(); // không được crash dù máy không có trình duyệt
    }

    // ====================================================================================================
    // DownloaderSettings + DownloadHistoryStore + DownloadQueue (hàng đợi hợp nhất: thêm -> kiểm tra OK/ERR ->
    // chọn -> tải). Chỉ nói chuyện với MiniHttpServer; yt-dlp/Edge không chạy (mục kiểm tra là tệp trực tiếp/404).
    // ====================================================================================================
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString settingsFile = tmp.filePath("settings.json");

        // ---- DownloaderSettings ----
        {
            DownloaderSettings s(settingsFile);
            CHECK(!s.downloadFolder().isEmpty() && s.maxConcurrent() == 3 && s.profileMaxItems() == 100 && s.useBrowserFallback());
            CHECK(s.cookieMode() == DownloaderSettings::CookieMode::None && s.ytDlpExtraArguments().isEmpty());
            int changes = 0;
            QObject::connect(&s, &DownloaderSettings::changed, [&] { ++changes; });
            s.setDownloadFolder(tmp.filePath("Tai ve"));
            s.setMaxConcurrent(100);
            s.setProfileMaxItems(0);
            s.setUseBrowserFallback(false);
            CHECK(s.maxConcurrent() == 8 && s.profileMaxItems() == 1 && !s.useBrowserFallback() && changes == 4);
            s.setMaxConcurrent(8); // không đổi -> không phát tín hiệu
            CHECK(changes == 4);
            s.setMaxConcurrent(-5);
            CHECK(s.maxConcurrent() == 1);
            s.setDownloadFolder("   "); // rỗng -> về mặc định
            CHECK(s.downloadFolder() == DownloaderSettings::defaultDownloadFolder());
            s.setDownloadFolder(tmp.filePath("Tai ve"));
            // Cookie: trình duyệt hợp lệ / lạ -> firefox / tệp chưa có / tệp có thật.
            s.setCookies(DownloaderSettings::CookieMode::Browser, "edge", QString());
            CHECK(s.ytDlpExtraArguments() == QStringList({"--cookies-from-browser", "edge"}));
            s.setCookies(DownloaderSettings::CookieMode::Browser, "--exec=calc", QString()); // không bao giờ lọt vào dòng lệnh
            CHECK(s.cookieBrowser() == "firefox" && s.ytDlpExtraArguments() == QStringList({"--cookies-from-browser", "firefox"}));
            s.setCookies(DownloaderSettings::CookieMode::File, "firefox", tmp.filePath("khong-co.txt"));
            CHECK(s.ytDlpExtraArguments().isEmpty());
            QFile cf(tmp.filePath("cookies.txt"));
            CHECK(cf.open(QIODevice::WriteOnly) && cf.write("# Netscape HTTP Cookie File\n") > 0);
            cf.close();
            s.setCookies(DownloaderSettings::CookieMode::File, "firefox", tmp.filePath("cookies.txt"));
            CHECK(s.ytDlpExtraArguments() == QStringList({"--cookies", tmp.filePath("cookies.txt")}));
        }
        {
            DownloaderSettings again(settingsFile); // đọc lại từ đĩa: mọi thứ còn nguyên
            CHECK(again.downloadFolder() == tmp.filePath("Tai ve") && again.maxConcurrent() == 1 && again.profileMaxItems() == 1);
            CHECK(!again.useBrowserFallback() && again.cookieMode() == DownloaderSettings::CookieMode::File);
            CHECK(again.cookieFile() == tmp.filePath("cookies.txt"));
        }
        {
            // Tệp hỏng/sai kiểu: không crash, về mặc định, kẹp phạm vi.
            QFile bad(tmp.filePath("bad.json"));
            CHECK(bad.open(QIODevice::WriteOnly) && bad.write("{ khong phai json") > 0);
            bad.close();
            DownloaderSettings s(tmp.filePath("bad.json"));
            CHECK(s.maxConcurrent() == 3 && s.profileMaxItems() == 100 && s.cookieMode() == DownloaderSettings::CookieMode::None);
            QJsonObject o;
            o["downloadFolder"] = 12345;
            o["maxConcurrent"] = "nhieu";
            o["profileMaxItems"] = 99999;
            o["cookieMode"] = "lung tung";
            o["cookieBrowser"] = "không-biết";
            s.fromJson(o);
            CHECK(s.downloadFolder() == DownloaderSettings::defaultDownloadFolder() && s.maxConcurrent() == 3 && s.profileMaxItems() == 1000);
            CHECK(s.cookieMode() == DownloaderSettings::CookieMode::None && s.cookieBrowser() == "firefox");
        }

        // ---- DownloadHistoryStore ----
        {
            const QString hp = tmp.filePath("history.json");
            DownloadHistoryStore h(hp);
            CHECK(h.entries().isEmpty());
            DownloadHistoryEntry e;
            e.title = "A";
            e.status = "Hoàn tất";
            e.path = "C:/x/a.png";
            e.size = 123;
            h.add(e);
            e.title = "B";
            e.status = "Thất bại";
            e.error = "lỗi";
            h.add(e);
            CHECK(h.entries().size() == 2 && h.entries()[0].title == "B" && h.entries()[1].title == "A"); // mới nhất đứng đầu
            CHECK(h.entries()[0].time.isValid());
            DownloadHistoryStore reload(hp);
            CHECK(reload.entries().size() == 2 && reload.entries()[1].path == "C:/x/a.png" && reload.entries()[1].size == 123);
            CHECK(reload.entries()[0].status == "Thất bại" && reload.entries()[0].error == "lỗi");
            reload.removeAt(0);
            reload.removeAt(99); // ngoài phạm vi: không làm gì
            CHECK(reload.entries().size() == 1 && DownloadHistoryStore(hp).entries().size() == 1);
            reload.clear();
            CHECK(reload.entries().isEmpty() && DownloadHistoryStore(hp).entries().isEmpty());
            // Giới hạn số mục.
            DownloadHistoryStore big(tmp.filePath("big.json"));
            for (int i = 0; i < DownloadHistoryStore::kMaxEntries + 5; ++i)
            {
                DownloadHistoryEntry x;
                x.title = QString::number(i);
                big.add(x);
            }
            CHECK(big.entries().size() == DownloadHistoryStore::kMaxEntries && big.entries()[0].title == QString::number(DownloadHistoryStore::kMaxEntries + 4));
            // Tệp hỏng được đổi tên .bak, lịch sử bắt đầu rỗng.
            QFile bad(tmp.filePath("h-bad.json"));
            CHECK(bad.open(QIODevice::WriteOnly) && bad.write("[1,2, hong") > 0);
            bad.close();
            DownloadHistoryStore hb(tmp.filePath("h-bad.json"));
            CHECK(hb.entries().isEmpty() && QFileInfo::exists(tmp.filePath("h-bad.json.bak")) && !QFileInfo::exists(tmp.filePath("h-bad.json")));
        }

        // ---- DownloadQueue ----
        MiniHttpServer srv;
        QList<QByteArray> heads;
        srv.handler = [&](const QByteArray& head) -> QByteArray {
            heads << head;
            const QByteArray path = MiniHttpServer::path(head);
            if (path.startsWith("/img.png")) // cả "/img.png?v=2" (cùng tên tệp, link khác)
                return MiniHttpServer::response("200 OK", {"Content-Type: image/png"}, QByteArray(1000, 'x'));
            if (path == "/doc.pdf")
                return MiniHttpServer::response("200 OK", {"Content-Type: application/pdf"}, QByteArray(300, 'p'));
            if (path.startsWith("/n"))
                return MiniHttpServer::response("200 OK", {"Content-Type: image/png"}, QByteArray(50, 'n'));
            return MiniHttpServer::response("404 Not Found", {"Content-Type: text/html"}, "<html>khong co</html>");
        };
        auto requestsFor = [&](const QByteArray& path) {
            int n = 0;
            for (const QByteArray& h : heads)
                if (MiniHttpServer::path(h) == path) ++n;
            return n;
        };

        const QString dlFolder = tmp.filePath("downloads");
        DownloaderSettings qs(tmp.filePath("q-settings.json"));
        qs.setDownloadFolder(dlFolder);
        qs.setUseBrowserFallback(false);
        DownloadHistoryStore qh(tmp.filePath("q-history.json"));
        FileDownloader qd;
        DownloadQueue q(&qs, &qh, &qd);
        QStringList messages;
        QObject::connect(&q, &DownloadQueue::message, [&](QString m) { messages << m; });
        QList<int> addedIds, removedIds, finishedIds;
        QObject::connect(&q, &DownloadQueue::itemAdded, [&](int id) { addedIds << id; });
        QObject::connect(&q, &DownloadQueue::itemRemoved, [&](int id) { removedIds << id; });
        QObject::connect(&q, &DownloadQueue::itemFinished, [&](int id, bool) { finishedIds << id; });
        auto settled = [&](int id) {
            const QueueItem* it = q.item(id);
            return it && it->check != QueueItem::Check::Waiting && it->check != QueueItem::Check::Checking;
        };

        // 1) Thêm link: CHƯA tải gì (chỉ một yêu cầu kiểm tra), kết quả OK kèm thông tin tệp.
        const int okId = q.addSource(srv.url("/img.png"));
        CHECK(okId > 0 && addedIds.contains(okId));
        CHECK(q.item(okId) && q.item(okId)->state == QueueItem::State::Idle);
        CHECK(waitUntil([&] { return settled(okId); }));
        waitUntil([] { return false; }, 300); // chờ xem có yêu cầu tải thứ hai lén xảy ra không
        CHECK(q.item(okId)->check == QueueItem::Check::Ok && q.item(okId)->media.fileName == "img.png" && q.item(okId)->total == 1000);
        CHECK(q.item(okId)->state == QueueItem::State::Idle && QueueItem::stateLabel(q.item(okId)->state) == "Chưa tải");
        CHECK(requestsFor("/img.png") == 1);                 // đúng MỘT yêu cầu: kiểm tra, chưa tải
        CHECK(!QFileInfo::exists(dlFolder + "/img.png"));    // chưa có tệp nào trên đĩa
        CHECK(QueueItem::checkLabel(QueueItem::Check::Ok) == "OK" && QueueItem::checkLabel(QueueItem::Check::Err) == "ERR");

        // 2) Link không ra tệp -> ERR kèm lý do; link trùng / không hợp lệ bị từ chối.
        const int errId = q.addSource(srv.url("/missing"));
        CHECK(waitUntil([&] { return settled(errId); }));
        CHECK(q.item(errId)->check == QueueItem::Check::Err && q.item(errId)->checkNote.contains("404"));
        CHECK(q.item(errId)->linkToOpen() == srv.url("/missing")); // vẫn mở được link trên trình duyệt
        messages.clear();
        CHECK(q.addSource(srv.url("/img.png")) == -1 && !messages.isEmpty() && messages.last().contains("đã có"));
        CHECK(q.addSource("ftp://x.com/a") == -1 && q.addSource("") == -1 && q.addSource("javascript:alert(1)") == -1);
        CHECK(q.allIds().size() == 2);

        // 3) Chọn rồi mới tải: chỉ mục OK được tải, ERR bị bỏ qua và báo rõ.
        messages.clear();
        CHECK(q.startItems({okId, errId}) == 1);
        CHECK(!messages.isEmpty() && messages.last().contains("1 mục ERR"));
        CHECK(waitUntil([&] { return q.item(okId)->state == QueueItem::State::Completed; }));
        CHECK(QFileInfo::exists(dlFolder + "/img.png") && QFileInfo(dlFolder + "/img.png").size() == 1000);
        CHECK(requestsFor("/img.png") == 2);                 // lần 2 = tải thật, chỉ sau khi người dùng chọn tải
        CHECK(q.item(errId)->state == QueueItem::State::Idle && !QFileInfo::exists(dlFolder + "/missing"));
        CHECK(finishedIds.contains(okId) && q.item(okId)->destPath == dlFolder + "/img.png");
        CHECK(qh.entries().size() == 1 && qh.entries()[0].status == "Hoàn tất" && qh.entries()[0].path == dlFolder + "/img.png");
        CHECK(qh.entries()[0].size == 1000 && qh.entries()[0].kind == "http" && qh.entries()[0].sourceUrl == srv.url("/img.png"));
        CHECK(!q.startItem(okId)); // đã hoàn tất: không tải lại

        // 4) Hai link cùng tên tệp -> tên không trùng (không ghi đè).
        ResolvedMedia dup;
        dup.url = srv.url("/img.png?v=2");
        dup.fileName = "img.png";
        dup.mediaType = "Ảnh";
        const int dupId = q.addResolved(dup);
        CHECK(dupId > 0 && q.item(dupId)->check == QueueItem::Check::Ok && q.item(dupId)->state == QueueItem::State::Idle);
        CHECK(q.addResolved(dup) == -1); // cùng link tệp -> không thêm lần nữa
        CHECK(q.startItems({dupId}) == 1);
        CHECK(waitUntil([&] { return q.item(dupId)->state == QueueItem::State::Completed; }));
        CHECK(QFileInfo::exists(dlFolder + "/img (1).png") && QFileInfo::exists(dlFolder + "/img.png"));

        // 5) Referer + User-Agent trình duyệt khi mục có trang nguồn.
        heads.clear();
        ResolvedMedia ref;
        ref.url = srv.url("/doc.pdf");
        ref.fileName = "bai giang.pdf";
        ref.referer = "http://trang-nguon.example/xem/1";
        const int refId = q.addResolved(ref, "http://trang-nguon.example/xem/1");
        q.startItems({refId});
        CHECK(waitUntil([&] { return q.item(refId)->state == QueueItem::State::Completed; }));
        CHECK(!heads.isEmpty() && MiniHttpServer::header(heads.first(), "Referer") == "http://trang-nguon.example/xem/1");
        CHECK(MiniHttpServer::header(heads.first(), "User-Agent").contains("Mozilla/5.0"));
        CHECK(QFileInfo::exists(dlFolder + "/bai giang.pdf"));

        // 6) Mục do nơi khác enqueue thẳng vào FileDownloader cũng hiện trong hàng đợi (phản chiếu), hủy được.
        const int before = q.allIds().size();
        const int fid = qd.enqueue(srv.url("/img.png?foreign"), dlFolder + "/ngoai.png");
        CHECK(q.allIds().size() == before + 1);
        const int foreignId = q.allIds().last();
        CHECK(q.item(foreignId) && q.item(foreignId)->foreign && q.item(foreignId)->httpId == fid &&
              q.item(foreignId)->check == QueueItem::Check::Ok && q.item(foreignId)->state == QueueItem::State::Queued);
        CHECK(q.item(foreignId)->displayName() == "ngoai.png");
        q.cancel(foreignId);
        CHECK(q.item(foreignId)->state == QueueItem::State::Canceled);

        // 7) Hủy khi đang kiểm tra: ERR "Đã hủy"; kiểm tra lại được.
        const int cId = q.addSource(srv.url("/n-cancel"));
        q.cancel(cId);
        CHECK(waitUntil([&] { return settled(cId); }));
        CHECK(q.item(cId)->check == QueueItem::Check::Err && q.item(cId)->checkNote.contains("hủy"));
        q.recheck(cId);
        CHECK(q.item(cId)->check == QueueItem::Check::Waiting || q.item(cId)->check == QueueItem::Check::Checking);
        CHECK(waitUntil([&] { return settled(cId); }));
        CHECK(q.item(cId)->check == QueueItem::Check::Ok); // /n-cancel trả 200 image/png

        // 8) Nhiều link cùng lúc: tối đa 2 bộ phân giải chạy song song, tất cả đều xong.
        QList<int> many;
        for (int i = 0; i < 6; ++i)
            many << q.addSource(srv.url(QString("/n%1").arg(i)));
        int maxChecking = 0;
        waitUntil([&] {
            int checking = 0;
            for (int id : many)
                if (q.item(id)->check == QueueItem::Check::Checking) ++checking;
            maxChecking = qMax(maxChecking, checking);
            for (int id : many)
                if (!settled(id)) return false;
            return true;
        });
        CHECK(maxChecking >= 1 && maxChecking <= 2);
        bool allOk = true;
        for (int id : many) allOk = allOk && q.item(id)->check == QueueItem::Check::Ok;
        CHECK(allOk && q.resolvingCount() == 0);

        // 9) Xóa: mục hoàn tất; mục lỗi; mục không tồn tại bị bỏ qua.
        removedIds.clear();
        CHECK(q.removeFinished(false) == 3 && removedIds.contains(okId) && removedIds.contains(dupId) && removedIds.contains(refId));
        CHECK(!q.item(okId) && q.item(errId));
        CHECK(q.removeFinished(true) >= 1 && !q.item(errId) && q.item(many[0])); // ERR + hủy bị dọn, mục OK còn nguyên
        q.remove({many[0], 99999});
        CHECK(!q.item(many[0]) && q.item(many[1]));
        // Xóa khi đang kiểm tra: không treo, không phát tín hiệu cho mục đã xóa.
        const int rmId = q.addSource(srv.url("/n-remove"));
        q.remove({rmId});
        waitUntil([] { return false; }, 200);
        CHECK(!q.item(rmId));
    }


    // ---- Bài đăng trong DOM trang hồ sơ (dữ liệu dựng từ liên kết THẬT lấy ở hồ sơ Facebook/TikTok công khai) ----
    {
        using namespace LinkResolverCore;
        const QString fbDom = R"HTML(
            <a href="/qynhmaiiiz">profile</a>
            <a href="https://www.facebook.com/photo/?fbid=2065644177665315&amp;set=a.100132200883199">p1</a>
            <a href="/photo/?fbid=2187503285479403&set=pb.100026592570107.-2207520000" role="link">p2</a>
            <a href="/photo/?fbid=2187503285479403&amp;set=a.1">trùng p2</a>
            <a href="/qynhmaiiiz/videos/1068033449194736/?__tn__=x">video</a>
            <a href="/reel/777/">reel</a>
            <a href="/watch/?v=555&t=3">watch</a>
            <a href="/qynhmaiiiz/posts/pfbid02abc">bài</a>
            <a href="https://example.com/photo/?fbid=1">trang khác</a>
            <a href="#">neo</a>
            <a href="javascript:void(0)">js</a>
            <a href="/login/device-based/regular/login/?next=x">đăng nhập</a>)HTML";
        const QList<PostRef> fb = postLinksFromDom(fbDom, "https://www.facebook.com/qynhmaiiiz", 80);
        CHECK(fb.size() == 6);
        if (fb.size() == 6)
        {
            CHECK(fb[0].photo && fb[0].id == "2065644177665315" && fb[0].url == "https://www.facebook.com/photo/?fbid=2065644177665315");
            CHECK(fb[1].photo && fb[1].id == "2187503285479403"); // bản trùng bị bỏ
            CHECK(!fb[2].photo && fb[2].id == "1068033449194736" && fb[2].url == "https://www.facebook.com/qynhmaiiiz/videos/1068033449194736");
            CHECK(!fb[3].photo && fb[3].url == "https://www.facebook.com/reel/777");
            CHECK(!fb[4].photo && fb[4].url == "https://www.facebook.com/watch/?v=555");
            CHECK(fb[5].photo && fb[5].id == "pfbid02abc" && fb[5].url == "https://www.facebook.com/qynhmaiiiz/posts/pfbid02abc");
        }
        CHECK(postLinksFromDom(fbDom, "https://www.facebook.com/qynhmaiiiz", 3).size() == 3); // giới hạn số bài

        const QString ttDom = R"(
            <a href="https://www.tiktok.com/@kiue.oahn/photo/7683433150571121940">ảnh</a>
            <a href="https://www.tiktok.com/@kiue.oahn/video/7696060155879820545?lang=vi">video</a>
            <a href="https://www.tiktok.com/@nguoikhac/video/111">của người khác</a>
            <a href="https://www.tiktok.com/@KIUE.oahn/photo/7690098215537904916">hoa thường</a>
            <a href="/@kiue.oahn/photo/7683433150571121940">trùng</a>)";
        const QList<PostRef> tt = postLinksFromDom(ttDom, "https://www.tiktok.com/@kiue.oahn", 80);
        CHECK(tt.size() == 3);
        if (tt.size() == 3)
        {
            CHECK(tt[0].photo && tt[0].id == "7683433150571121940");
            CHECK(!tt[1].photo && tt[1].id == "7696060155879820545" && tt[1].url == "https://www.tiktok.com/@kiue.oahn/video/7696060155879820545");
            CHECK(tt[2].photo && tt[2].id == "7690098215537904916"); // tên người dùng so khớp không phân biệt hoa thường
        }

        const QString igDom = R"(<a href="/p/CxYz_1-2/">a</a><a href="/someone/reel/ABC123/">b</a><a href="/explore/">c</a><a href="/accounts/login/">d</a>)";
        const QList<PostRef> ig = postLinksFromDom(igDom, "https://www.instagram.com/someone/", 80);
        CHECK(ig.size() == 2 && !ig[0].photo && ig[0].url == "https://www.instagram.com/p/CxYz_1-2/" && ig[1].id == "ABC123");

        // Trang không phải mạng xã hội hỗ trợ: không trả gì
        CHECK(postLinksFromDom(fbDom, "https://example.com/trang", 80).isEmpty());
        CHECK(postLinksFromDom("", "https://www.tiktok.com/@kiue.oahn", 80).isEmpty());

        CHECK(tiktokPostId("https://www.tiktok.com/@kiue.oahn/video/7683433150571121940?x=1") == "7683433150571121940");
        CHECK(tiktokPostId("https://www.tiktok.com/@kiue.oahn/photo/55") == "55");
        CHECK(tiktokPostId("https://www.tiktok.com/@kiue.oahn").isEmpty());
        CHECK(tiktokPostId("https://www.youtube.com/watch?v=1").isEmpty());
        CHECK(tiktokPhotoUrlFromVideoUrl("https://www.tiktok.com/@kiue.oahn/video/7683433150571121940?lang=vi") ==
              "https://www.tiktok.com/@kiue.oahn/photo/7683433150571121940");
        CHECK(tiktokPhotoUrlFromVideoUrl("https://www.tiktok.com/@kiue.oahn/photo/7683433150571121940").isEmpty());
        CHECK(tiktokPhotoUrlFromVideoUrl("https://example.com/@a/video/1").isEmpty());

        // yt-dlp trả bài ảnh TikTok chỉ có nhạc (định dạng duy nhất là mp3, vcodec none) - đúng dữ liệu đã đo thật
        const QByteArray photoJson = R"({"id":"7683433150571121940","title":"#xuhuong","extractor_key":"TikTok","ext":"mp3",
            "vcodec":"none","acodec":"mp3","webpage_url":"https://www.tiktok.com/@kiue.oahn/video/7683433150571121940",
            "formats":[{"format_id":"audio","ext":"mp3","vcodec":"none","acodec":"mp3"}]})";
        const ResolveResult pr = parseYtDlpJson(photoJson, "https://www.tiktok.com/@kiue.oahn/video/7683433150571121940", 10);
        CHECK(pr.ok() && pr.photoPostUrl == "https://www.tiktok.com/@kiue.oahn/photo/7683433150571121940");
        const QByteArray videoJson = R"({"id":"1","title":"v","extractor_key":"TikTok","ext":"mp4","vcodec":"h264","acodec":"aac",
            "webpage_url":"https://www.tiktok.com/@kiue.oahn/video/1","formats":[{"format_id":"v","ext":"mp4","vcodec":"h264"}]})";
        CHECK(parseYtDlpJson(videoJson, "https://www.tiktok.com/@kiue.oahn/video/1", 10).photoPostUrl.isEmpty());
        const QByteArray ytAudioJson = R"({"id":"x","title":"nhạc","extractor_key":"Youtube","ext":"m4a","vcodec":"none",
            "webpage_url":"https://www.youtube.com/watch?v=x","formats":[{"format_id":"140","ext":"m4a","vcodec":"none"}]})";
        CHECK(parseYtDlpJson(ytAudioJson, "https://www.youtube.com/watch?v=x", 10).photoPostUrl.isEmpty()); // chỉ riêng TikTok
    }

    // ---- Tệp cookies.txt (Netscape): đọc đúng + gửi đúng cookie tới đúng miền/đường dẫn ----
    {
        const QByteArray text =
            "# Netscape HTTP Cookie File\n"
            ".example.com\tTRUE\t/\tTRUE\t4102444800\tsid\tabc\n"
            "#HttpOnly_.example.com\tTRUE\t/app\tFALSE\t0\ttok\txyz\n"
            "host.test\tFALSE\t/\tFALSE\t1\told\thet-han\n"
            "\n"
            "dong-hong-khong-du-cot\n"
            "# chú thích\n";
        const QList<QNetworkCookie> cs = CookieFile::parse(text);
        CHECK(cs.size() == 2); // bỏ cookie hết hạn và dòng hỏng
        if (cs.size() == 2)
        {
            CHECK(cs[0].name() == "sid" && cs[0].value() == "abc" && cs[0].domain() == ".example.com" && cs[0].isSecure());
            CHECK(cs[0].path() == "/" && !cs[0].isSessionCookie());
            CHECK(cs[1].name() == "tok" && cs[1].isHttpOnly() && cs[1].path() == "/app" && cs[1].isSessionCookie());
        }
        // Miền chỉ riêng máy chủ: không có dấu chấm đầu; miền gồm miền con: tự thêm dấu chấm đầu
        const QList<QNetworkCookie> d = CookieFile::parse("host.only\tFALSE\t/\tFALSE\t0\ta\t1\nsub.dom\tTRUE\t/\tFALSE\t0\tb\t2\n");
        CHECK(d.size() == 2 && d[0].domain() == "host.only" && d[1].domain() == ".sub.dom");
        CHECK(CookieFile::parse("").isEmpty());
        CHECK(CookieFile::load(QDir::tempPath() + "/khong-ton-tai-cookie.txt").isEmpty());
        QString err;
        CookieFile::load(QDir::tempPath() + "/khong-ton-tai-cookie.txt", &err);
        CHECK(!err.isEmpty());

        // Gửi cookie thật qua FileDownloader tới máy chủ cục bộ
        QTemporaryDir tmp;
        MiniHttpServer http;
        http.handler = [&](const QByteArray&) { return MiniHttpServer::response("200 OK", {"Content-Type: application/octet-stream"}, "du-lieu"); };
        const QString cookiePath = tmp.path() + "/cookies.txt";
        {
            QFile f(cookiePath);
            f.open(QIODevice::WriteOnly);
            f.write("127.0.0.1\tFALSE\t/\tFALSE\t0\tsid\tabc123\n"
                    "127.0.0.1\tFALSE\t/private\tFALSE\t0\tpv\t1\n"
                    "127.0.0.1\tFALSE\t/\tTRUE\t0\tsec\t1\n"       // chỉ https - KHÔNG được gửi qua http
                    "khac.example\tFALSE\t/\tFALSE\t0\tkhac\t1\n"); // miền khác - KHÔNG được gửi
        }
        FileDownloader dl;
        dl.setCookieFile(cookiePath);
        int finished = 0;
        QObject::connect(&dl, &FileDownloader::itemFinished, [&](int, bool) { ++finished; });
        const int a = dl.enqueue(http.url("/file"), tmp.path() + "/a.bin");
        dl.start(a);
        CHECK(waitUntil([&] { return finished >= 1; }));
        const QByteArray cookieA = http.requests.isEmpty() ? QByteArray() : MiniHttpServer::header(http.requests.last(), "Cookie");
        CHECK(cookieA.contains("sid=abc123"));
        CHECK(!cookieA.contains("pv=") && !cookieA.contains("sec=") && !cookieA.contains("khac="));
        const int b = dl.enqueue(http.url("/private/f"), tmp.path() + "/b.bin");
        dl.start(b);
        CHECK(waitUntil([&] { return finished >= 2; }));
        const QByteArray cookieB = MiniHttpServer::header(http.requests.last(), "Cookie");
        CHECK(cookieB.contains("sid=abc123") && cookieB.contains("pv=1"));

        // Bỏ tệp cookie: không còn gửi cookie nào
        dl.setCookieFile(QString());
        const int c2 = dl.enqueue(http.url("/file2"), tmp.path() + "/c.bin");
        dl.start(c2);
        CHECK(waitUntil([&] { return finished >= 3; }));
        CHECK(MiniHttpServer::header(http.requests.last(), "Cookie").isEmpty());

        // Cài đặt: chỉ chế độ "tệp" mới có tệp cookie cho yêu cầu HTTP
        DownloaderSettings st(tmp.path() + "/st.json");
        CHECK(st.httpCookieFile().isEmpty());
        st.setCookies(DownloaderSettings::CookieMode::File, "firefox", cookiePath);
        CHECK(st.httpCookieFile() == cookiePath);
        st.setCookies(DownloaderSettings::CookieMode::File, "firefox", tmp.path() + "/khong-co.txt");
        CHECK(st.httpCookieFile().isEmpty());
        st.setCookies(DownloaderSettings::CookieMode::Browser, "firefox", cookiePath);
        CHECK(st.httpCookieFile().isEmpty()); // cookie "từ trình duyệt" chỉ yt-dlp đọc được
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: khong co mang Internet/yt-dlp that trong test nay - YtDlpInfoWorker/YtDlpDownloadWorker\n");
    std::printf("(chay yt-dlp.exe that) can duoc tu kiem tra tay qua giao dien.\n");
    return g_fail == 0 ? 0 : 1;
}
