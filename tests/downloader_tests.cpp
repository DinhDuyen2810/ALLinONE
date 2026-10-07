// Kiểm thử lõi Downloader. KHÔNG cần mạng/yt-dlp thật - chỉ kiểm thử lõi THUẦN (phân tích HTML mẫu dựng
// sẵn, dựng đối số dòng lệnh yt-dlp, phân tích JSON/dòng tiến độ mẫu) tách riêng khỏi phần gọi mạng/
// chạy yt-dlp.exe thật, giống mẫu đã dùng cho VpnController/DefenderController/AdbController.
#include <QCoreApplication>
#include <cstdio>

#include "tools/downloader/engine/PageMediaScanner.h"
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

        // Không truyền formatId -> KHÔNG có cờ -f (để yt-dlp tự chọn mặc định video+âm thanh tốt nhất)
        const auto argsNoFormat = YtDlpDownloadWorkerInternal::buildArguments("https://x.com/v", "", "D:/x", "D:/yt-dlp");
        CHECK(!argsNoFormat.contains("-f"));
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

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: khong co mang/yt-dlp that trong test nay - FileDownloader (tai HTTP that),\n");
    std::printf("PageMediaScanner::scan() (tai trang that), YtDlpInfoWorker/YtDlpDownloadWorker (chay\n");
    std::printf("yt-dlp.exe that) can duoc tu kiem tra tay/qua UI test rieng.\n");
    return g_fail == 0 ? 0 : 1;
}
