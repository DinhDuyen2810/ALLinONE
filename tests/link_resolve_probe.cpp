// Công cụ KIỂM TRA TAY (không phải test tự động - gọi mạng thật, yt-dlp và Edge/Chrome ẩn thật):
// chạy ĐÚNG bộ phân giải link của Downloader (LinkResolver) trên một địa chỉ rồi in từng mục tìm được - dùng khi
// người dùng báo "dán link X mà ra toàn ảnh / không ra video" để thấy app thật sự trả gì và ở bậc nào.
//
//   link_resolve_probe <url> [--no-browser] [--max N] [--dom <tệp>] [--download <thư mục> [--audio|--music]]
//   --dom <tệp>: chỉ dựng trang bằng trình duyệt ẩn rồi lưu DOM vào <tệp> (xem trang có những link gì).
//   --download <thư mục>: TẢI THẬT link MỘT video qua đúng hàng đợi của app vào <thư mục> (do người kiểm chọn);
//     --audio = "Chỉ tải âm thanh (mp3)", --music = "Nhạc nền gốc TikTok (mp3)", không có = video.
//
// Cần thư mục yt-dlp\ (yt-dlp.exe + ffmpeg) cạnh tệp chạy, giống OneForAll.exe (chép từ OneForAll_Release\yt-dlp).
// Không có --download thì chỉ ĐỌC trang - không tải tệp nào về máy.
#include "tools/downloader/DownloadHistoryStore.h"
#include "tools/downloader/DownloaderSettings.h"
#include "tools/downloader/engine/DownloadQueue.h"
#include "tools/downloader/engine/FileDownloader.h"
#include "tools/downloader/engine/HeadlessBrowser.h"
#include "tools/downloader/engine/LinkResolver.h"
#include "core/AppPaths.h"
#include "TestDataDir.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>
#include <cstdio>
#include <functional>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestDataDir dataDir;
    AppPaths::setDataDirOverride(dataDir.path());

    QString url;
    QString domFile;
    QString downloadDir;
    QString mode; // "", "audio", "music"
    LinkResolver::Options opts;
    for (int i = 1; i < argc; ++i)
    {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--no-browser")
            opts.useBrowser = false;
        else if (a == "--max" && i + 1 < argc)
            opts.maxItems = QString::fromLocal8Bit(argv[++i]).toInt();
        else if (a == "--dom" && i + 1 < argc)
            domFile = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--download" && i + 1 < argc)
            downloadDir = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--audio")
            mode = "audio";
        else if (a == "--music")
            mode = "music";
        else
            url = a;
    }
    if (url.isEmpty())
    {
        std::printf("Cach dung: link_resolve_probe <url> [--no-browser] [--max N] [--dom <tep>] [--download <thu muc> [--audio|--music]]\n"
                    "  --dom <tep>: CHI dung trinh duyet an dung trang roi luu DOM vao <tep> (de xem trang co link gi)\n"
                    "  --download <thu muc>: TAI THAT link mot video qua hang doi cua app (--audio: chi am thanh mp3, --music: nhac nen TikTok)\n");
        return 2;
    }
    QElapsedTimer timer;
    timer.start();

    // --download <thư mục> [--audio|--music]: TẢI THẬT qua đúng hàng đợi của app (DownloadQueue + yt-dlp + ffmpeg) - chỉ cho
    // link MỘT video. Ghi vào <thư mục> do người kiểm tự chọn (không đụng thư mục tải của người dùng).
    if (!downloadDir.isEmpty())
    {
        DownloaderSettings settings(dataDir.filePath("downloader_settings.json"));
        settings.setDownloadFolder(downloadDir);
        DownloadHistoryStore history(dataDir.filePath("history.json"));
        FileDownloader fd;
        DownloadQueue queue(&settings, &history, &fd);
        int dlRc = 1;
        bool started = false;
        bool scheduled = false;
        std::function<void()> pick;
        QObject::connect(&queue, &DownloadQueue::itemUpdated, [&](int) {
            // Mục đầu được báo trước khi các mục còn lại của cùng kết quả được chèn - đợi hàng đợi yên rồi mới chọn.
            if (started || scheduled || queue.resolvingCount() > 0)
                return;
            scheduled = true;
            QTimer::singleShot(800, [&]() {
                scheduled = false;
                pick();
            });
        });
        pick = [&]() {
            if (started || queue.resolvingCount() > 0)
                return;
            // Phân giải xong: ưu tiên mục yt-dlp (video/âm thanh - kể cả "Nhạc nền bài ảnh"), không có thì mục OK đầu tiên.
            int id = -1;
            for (int cand : queue.allIds())
            {
                const QueueItem* c = queue.item(cand);
                if (!c || c->check != QueueItem::Check::Ok)
                    continue;
                if (id < 0 || (c->media.kind == ResolvedMedia::Kind::YtDlp && queue.item(id)->media.kind != ResolvedMedia::Kind::YtDlp))
                    id = cand;
            }
            const QueueItem* it = id >= 0 ? queue.item(id) : nullptr;
            if (!it)
            {
                const QList<int> all = queue.allIds();
                const QueueItem* e = all.isEmpty() ? nullptr : queue.item(all.first());
                if (e && e->check == QueueItem::Check::Err)
                {
                    std::printf("ERR: %s\n", e->checkNote.toUtf8().constData());
                    QCoreApplication::quit();
                }
                return;
            }
            started = true;
            std::printf("[%6.1fs] %lld muc trong hang doi, chon: %s\n", timer.elapsed() / 1000.0,
                        static_cast<long long>(queue.allIds().size()), it->displayName().toUtf8().constData());
            if (mode == "audio")
                queue.setItemFormat(id, DownloaderSettings::audioFormatSelector(), "Chỉ âm thanh (mp3)");
            else if (mode == "music")
                queue.setItemFormat(id, DownloaderSettings::tiktokMusicFormatSelector(), "Nhạc nền gốc TikTok (mp3)");
            std::printf("[%6.1fs] OK (%s) - bat dau tai, che do: %s\n", timer.elapsed() / 1000.0, it->media.mediaType.toUtf8().constData(),
                        mode.isEmpty() ? "video" : mode.toUtf8().constData());
            std::fflush(stdout);
            queue.startItem(id);
        };
        QObject::connect(&queue, &DownloadQueue::itemFinished, [&](int id, bool ok) {
            const QueueItem* it = queue.item(id);
            std::printf("[%6.1fs] %s: %s %s\n", timer.elapsed() / 1000.0, ok ? "XONG" : "LOI",
                        it ? it->destPath.toUtf8().constData() : "", it && !ok ? it->error.toUtf8().constData() : "");
            dlRc = ok ? 0 : 1;
            QCoreApplication::quit();
        });
        QTimer::singleShot(0, [&]() {
            if (queue.addSource(url) < 0)
            {
                std::printf("Khong them duoc link\n");
                QCoreApplication::quit();
            }
        });
        app.exec();
        queue.stopProcesses();
        return dlRc;
    }

    if (!domFile.isEmpty())
    {
        HeadlessBrowser browser;
        int domRc = 1;
        QObject::connect(&browser, &HeadlessBrowser::finished, [&](bool ok, QString dom, QByteArray, QString error) {
            QFile f(domFile);
            if (ok && f.open(QIODevice::WriteOnly))
            {
                f.write(dom.toUtf8());
                std::printf("Da luu DOM (%lld ky tu) vao %s\n", static_cast<long long>(dom.size()), domFile.toUtf8().constData());
                domRc = 0;
            }
            else
                std::printf("Loi: %s\n", error.toUtf8().constData());
            QCoreApplication::quit();
        });
        QTimer::singleShot(0, [&]() { browser.start(url); });
        app.exec();
        return domRc;
    }

    LinkResolver resolver;
    resolver.setOptions(opts);
    timer.restart();
    int rc = 1;
    QObject::connect(&resolver, &LinkResolver::progress, [&](QString m) {
        std::printf("[%6.1fs] %s\n", timer.elapsed() / 1000.0, m.toUtf8().constData());
        std::fflush(stdout);
    });
    QObject::connect(&resolver, &LinkResolver::finished, [&](ResolveResult r) {
        std::printf("\n== Ket qua sau %.1fs: %lld muc, collection=%d, truncated=%d\n", timer.elapsed() / 1000.0,
                    static_cast<long long>(r.items.size()), r.isCollection ? 1 : 0, r.truncated ? 1 : 0);
        if (!r.error.isEmpty())
            std::printf("LOI: %s\n", r.error.toUtf8().constData());
        if (!r.warning.isEmpty())
            std::printf("LUU Y: %s\n", r.warning.toUtf8().constData());
        int i = 0;
        for (const ResolvedMedia& m : r.items)
        {
            const char* kind = m.kind == ResolvedMedia::Kind::YtDlp ? "yt-dlp" : m.kind == ResolvedMedia::Kind::DriveViewer ? "drive" : "http";
            std::printf("%3d. [%s] %-8s %s%s\n     %s\n     nguon: %s\n", ++i, kind, m.mediaType.toUtf8().constData(),
                        m.title.left(70).toUtf8().constData(), m.maybePhotoPost ? "  (co the la bai anh)" : "",
                        m.url.left(160).toUtf8().constData(), m.note.toUtf8().constData());
        }
        rc = r.ok() ? 0 : 1;
        QCoreApplication::quit();
    });
    QTimer::singleShot(0, [&]() { resolver.resolve(url); });
    app.exec();
    return rc;
}
