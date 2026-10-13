// Kiểm thử cho giao diện Downloader: dựng cửa sổ, không crash, đủ 5 tab, hàng đợi hợp nhất (thêm -> OK/ERR -> chọn -> tải),
// quét trang -> hàng đợi, lịch sử, cài đặt. CÓ tải THẬT một tệp nhỏ
// công khai, ổn định (logo Google, ~5KB) qua FileDownloader để xác nhận plumbing HTTP thật hoạt động -
// giống mẫu PublicIpChecker/SpeedTestRunner đã dùng thật trong module VPN/WiFi. KHÔNG tự động chạy
// yt-dlp.exe thật (cần mạng + có thể chậm, chỉ nên tự kiểm tra tay qua UI) và KHÔNG tự động quét trang
// web thật trong test này (PageMediaScanner đã có test lõi riêng bằng HTML mẫu dựng sẵn).
#include <QApplication>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include "MiniHttpServer.h"
#include "TestDataDir.h"
#include <QTimer>
#include <cstdio>

#include "core/AppPaths.h"
#include "tools/downloader/DirectDownloadTab.h"
#include "tools/downloader/DownloadHistoryStore.h"
#include "tools/downloader/DownloaderSettings.h"
#include "tools/downloader/DownloaderWindow.h"
#include "tools/downloader/PageScanTab.h"
#include "tools/downloader/engine/DownloadQueue.h"
#include "tools/downloader/engine/FileDownloader.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

/// Quay vòng lặp sự kiện tới khi `done()` đúng hoặc hết thời gian chờ.
static bool waitUntil(const std::function<bool()>& done, int timeoutMs = 20000)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return done();
}

static QPushButton* findButton(QWidget* root, const QString& text)
{
    for (auto* b : root->findChildren<QPushButton*>())
        if (b->text() == text || b->text().startsWith(text + " ("))  // nút có thể kèm số đếm "(n)"
            return b;
    return nullptr;
}

/// Bảng có đúng `cols` cột và tiêu đề cột 1 như mô tả (tìm bảng nào trong cửa sổ).
static QTableWidget* findTable(QWidget* root, int cols, const QString& header1)
{
    for (auto* table : root->findChildren<QTableWidget*>())
        if (table->columnCount() == cols && table->horizontalHeaderItem(1) && table->horizontalHeaderItem(1)->text() == header1)
            return table;
    return nullptr;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // Cửa sổ ghi log khi mở - ép dữ liệu vào thư mục tạm để test không ghi vào log thật của người dùng.
    TestDataDir dataDir;
    AppPaths::setDataDirOverride(dataDir.path());

    DownloaderWindow win;
    win.setAttribute(Qt::WA_DontShowOnScreen, true);
    win.show();
    app.processEvents();
    CHECK(true); // tới được đây tức là dựng cửa sổ không crash

    auto* tabs = win.findChild<QTabWidget*>();
    CHECK(tabs != nullptr && tabs->count() == 5);
    if (tabs && tabs->count() == 5)
    {
        CHECK(tabs->tabText(3).contains("Lịch sử"));
        CHECK(tabs->tabText(4).contains("Cài đặt"));
    }

    const auto buttons = win.findChildren<QPushButton*>();
    CHECK(buttons.size() >= 5);

    const auto labels = win.findChildren<QLabel*>();
    CHECK(labels.size() >= 3);

    // Hộp thoại thông tin (modal) do các tab bật lên sẽ chặn test - tự đóng ngay khi xuất hiện.
    QTimer dismissModals;
    QObject::connect(&dismissModals, &QTimer::timeout, [] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
            box->accept();
    });
    dismissModals.start(40);

    DownloaderSettings* settings = win.settings();
    DownloadQueue* queue = win.queue();
    DownloadHistoryStore* history = win.history();
    CHECK(settings && queue && history);

    // Thư mục tải + trình duyệt ẩn: chỉ dùng thư mục tạm của test; KHÔNG bật Edge/Chrome ẩn trong test.
    const QString dlRoot = dataDir.path() + "/dl_ui_test";
    QDir().mkpath(dlRoot);
    settings->setDownloadFolder(dlRoot);
    settings->setUseBrowserFallback(false);

    QTableWidget* queueTable = findTable(&win, 9, "Tên / Tiêu đề");
    CHECK(queueTable != nullptr);
    auto* directTab = win.findChild<DirectDownloadTab*>();
    CHECK(directTab != nullptr);

    // Hồi quy lỗi THẬT: mục đưa vào FileDownloader dùng chung KHÔNG qua tab "Tải trực tiếp" từng tải mà không có hàng nào
    // trong bảng hàng đợi - không thấy tiến độ/lỗi, không dừng/hủy được. KHÔNG start() ở đây nên không có yêu cầu mạng.
    {
        auto* shared = win.findChild<FileDownloader*>();
        CHECK(shared != nullptr);
        if (shared && queueTable)
        {
            CHECK(queueTable->rowCount() == 0);
            const int id = shared->enqueue("https://example.invalid/anh.jpg", QDir::tempPath() + "/oneforall_downloader_test/tu-tab-quet-trang.jpg");
            CHECK(queueTable->rowCount() == 1);
            CHECK(queueTable->item(0, 1) && queueTable->item(0, 1)->text() == "tu-tab-quet-trang.jpg");
            CHECK(queueTable->item(0, 4) && queueTable->item(0, 4)->text() == "OK"); // mục phản chiếu: đã biết link tệp
            CHECK(queueTable->item(0, 7) && queueTable->item(0, 7)->text() == "Đang chờ");
            shared->cancel(id);
            CHECK(queueTable->item(0, 7) && queueTable->item(0, 7)->text() == "Đã hủy");
            CHECK(queueTable->rowCount() == 1);
            queue->remove(queue->allIds());
            CHECK(queueTable->rowCount() == 0);
        }
    }

    // ---- Quy trình mới: dán link -> vào hàng đợi (CHƯA tải) -> OK/ERR -> chọn -> tải ----
    if (queueTable && directTab)
    {
        MiniHttpServer http;
        const QByteArray fileBody(5000, 'A');
        http.handler = [&](const QByteArray& head) -> QByteArray {
            const QByteArray path = MiniHttpServer::path(head);
            if (path == "/file.bin")
                return MiniHttpServer::response("200 OK",
                                                {"Content-Type: application/octet-stream",
                                                 "Content-Disposition: attachment; filename=\"du-lieu.bin\""},
                                                fileBody);
            if (path == "/page.html")
                return MiniHttpServer::response(
                    "200 OK", {"Content-Type: text/html; charset=utf-8"},
                    "<html><body><img src=\"/anh-a.png\"><a href=\"/tai-lieu.pdf\">pdf</a></body></html>");
            if (path == "/anh-a.png")
                return MiniHttpServer::response("200 OK", {"Content-Type: image/png"}, QByteArray(300, 'P'));
            if (path == "/tai-lieu.pdf")
                return MiniHttpServer::response("200 OK", {"Content-Type: application/pdf"}, QByteArray(400, 'D'));
            return MiniHttpServer::response("404 Not Found", {"Content-Type: text/plain"}, "khong co");
        };

        directTab->enqueueText(http.url("/file.bin") + "\n" + http.url("/missing.bin") + "\nkhong-phai-link");
        CHECK(queueTable->rowCount() == 2); // dòng "khong-phai-link" bị bỏ qua
        // Vừa thêm: CHƯA tải gì - thư mục đích chưa có tệp nào.
        CHECK(QDir(dlRoot).entryList(QDir::Files).isEmpty());

        const bool checked = waitUntil([&] {
            if (queueTable->rowCount() != 2)
                return false;
            for (int r = 0; r < 2; ++r)
            {
                const QString t = queueTable->item(r, 4) ? queueTable->item(r, 4)->text() : QString();
                if (t != "OK" && t != "ERR")
                    return false;
            }
            return true;
        });
        CHECK(checked);
        int okRow = -1, errRow = -1;
        for (int r = 0; r < queueTable->rowCount(); ++r)
        {
            const QString t = queueTable->item(r, 4)->text();
            if (t == "OK") okRow = r;
            if (t == "ERR") errRow = r;
        }
        CHECK(okRow >= 0 && errRow >= 0);
        if (okRow >= 0 && errRow >= 0)
        {
            CHECK(queueTable->item(okRow, 1)->text() == "du-lieu.bin"); // tên lấy từ Content-Disposition
            CHECK(queueTable->item(errRow, 7)->text().startsWith("✖")); // lý do hiện ở cột trạng thái
            CHECK(queueTable->cellWidget(okRow, 8) != nullptr && queueTable->cellWidget(errRow, 8) != nullptr); // nút 🌐 mỗi dòng
            CHECK(QDir(dlRoot).entryList(QDir::Files).isEmpty()); // kiểm tra link KHÔNG được tải tệp về

            // Chọn CẢ dòng ERR lẫn OK rồi tải: chỉ dòng OK được tải.
            queueTable->item(okRow, 0)->setCheckState(Qt::Checked);
            queueTable->item(errRow, 0)->setCheckState(Qt::Checked);
            QPushButton* dlBtn = findButton(directTab, "⬇ Tải các mục đã chọn");
            CHECK(dlBtn != nullptr);
            if (dlBtn)
                dlBtn->click();
            const bool done = waitUntil([&] { return queueTable->item(okRow, 7) && queueTable->item(okRow, 7)->text() == "Hoàn tất"; });
            CHECK(done);
            const QString saved = dlRoot + "/du-lieu.bin";
            CHECK(QFileInfo::exists(saved));
            CHECK(QFileInfo(saved).size() == 5000);
            CHECK(queueTable->item(errRow, 4)->text() == "ERR"); // dòng lỗi vẫn là ERR, không bị tải
            CHECK(QDir(dlRoot).entryList(QDir::Files).size() == 1);

            // Lịch sử có mục "Hoàn tất" cho tệp vừa tải (mục đầu danh sách = mới nhất). Mục "Đã hủy" của dòng phản chiếu ở
            // đầu bài test cũng được ghi - đúng thiết kế: mọi lần tải kết thúc (xong/lỗi/hủy) đều vào lịch sử.
            CHECK(history->entries().size() == 2);
            if (!history->entries().isEmpty())
            {
                const auto& e = history->entries().first();
                CHECK(e.status == "Hoàn tất");
                CHECK(e.fileName == "du-lieu.bin");
                CHECK(QFileInfo(e.path) == QFileInfo(saved));
            }
            if (history->entries().size() >= 2)
                CHECK(history->entries().at(1).status == "Đã hủy");
            auto* historyTable = findTable(&win, 5, "Tên");
            CHECK(historyTable != nullptr && historyTable->rowCount() == history->entries().size());
        }

        // ---- Tab "Quét trang web": quét trang HTML mẫu -> thêm vào hàng đợi (chưa tải) ----
        queue->remove(queue->allIds());
        CHECK(queueTable->rowCount() == 0);
        auto* scanTab = win.findChild<PageScanTab*>();
        CHECK(scanTab != nullptr);
        if (scanTab)
        {
            auto* urlEdit = scanTab->findChild<QLineEdit*>();
            QPushButton* scanBtn = findButton(scanTab, "🔍 Quét trang");
            CHECK(urlEdit && scanBtn);
            if (urlEdit && scanBtn)
            {
                urlEdit->setText(http.url("/page.html"));
                scanBtn->click();
                QTableWidget* scanTable = findTable(scanTab, 4, "Loại");
                CHECK(scanTable != nullptr);
                const bool scanned = waitUntil([&] { return scanTable && scanTable->rowCount() >= 2; });
                CHECK(scanned);
                if (scanTable && scanned)
                {
                    CHECK(scanTable->cellWidget(0, 3) != nullptr); // nút 🌐 mở link trên trình duyệt
                    findButton(scanTab, "Chọn tất cả")->click();
                    findButton(scanTab, "+ Thêm các mục đã chọn vào hàng đợi")->click();
                    CHECK(queueTable->rowCount() == scanTable->rowCount());
                    bool allOk = true;
                    for (int r = 0; r < queueTable->rowCount(); ++r)
                        allOk = allOk && queueTable->item(r, 4)->text() == "OK" && queueTable->item(r, 7)->text() == "Chưa tải";
                    CHECK(allOk); // đã biết là tải được nên OK ngay, và CHƯA tải
                    CHECK(QDir(dlRoot).entryList(QDir::Files).size() == 1); // vẫn chỉ có tệp lần trước
                }
            }
        }
        queue->remove(queue->allIds());
    }

    // ---- Cài đặt dùng chung: đổi thư mục tải ở một nơi là các tab đều thấy ----
    {
        const QString newFolder = dataDir.path() + "/thu-muc-khac";
        QDir().mkpath(newFolder);
        settings->setDownloadFolder(newFolder);
        int shown = 0;
        for (auto* l : win.findChildren<QLabel*>())
            if (l->text() == QDir::toNativeSeparators(newFolder)) // nhãn hiện đường dẫn kiểu Windows (\)
                ++shown;
        CHECK(shown >= 4); // tab Tải trực tiếp, Quét trang, Video nền tảng, Cài đặt
        CHECK(settings->downloadFolder() == newFolder);
    }

    // Tải THẬT một tệp nhỏ công khai, ổn định - xác nhận FileDownloader hoạt động thật trên máy build.
    const QString destDir = QDir::tempPath() + "/oneforall_downloader_test";
    QDir().mkpath(destDir);
    const QString destPath = destDir + "/test-logo.png";
    QFile::remove(destPath); // dọn vết lần chạy trước nếu có

    FileDownloader downloader;
    bool finishedOk = false, gotFinishedSignal = false;
    QObject::connect(&downloader, &FileDownloader::itemFinished, [&](int, bool ok) {
        finishedOk = ok;
        gotFinishedSignal = true;
    });

    const int id = downloader.enqueue("https://www.google.com/images/branding/googlelogo/1x/googlelogo_color_272x92dp.png", destPath);
    downloader.start(id);

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&downloader, &FileDownloader::itemFinished, &loop, &QEventLoop::quit);
    timeoutTimer.start(15000);
    loop.exec();

    std::printf("Tai that: gotFinishedSignal=%d finishedOk=%d tonTaiTepThat=%d\n", gotFinishedSignal, finishedOk,
               QFileInfo::exists(destPath));
    CHECK(gotFinishedSignal); // không được rơi vào timeout 15s - xác nhận mạng/plumbing hoạt động
    if (gotFinishedSignal)
    {
        CHECK(finishedOk);
        CHECK(QFileInfo::exists(destPath));
        CHECK(QFileInfo(destPath).size() > 0);
    }
    QDir(destDir).removeRecursively(); // dọn dẹp tệp test thật sau khi xong

    // Hồi quy lỗi THẬT phát hiện khi tự kiểm tra tay (nhiều tải song song): một URL trả lỗi HTTP thật
    // (404) từng khiến nội dung TRANG LỖI (HTML "Not Found") bị ghi nhầm vào tệp đích - người dùng thấy
    // trạng thái "Thất bại" nhưng vẫn có một tệp rác nằm lại. Xác nhận lại bằng URL THẬT chắc chắn 404
    // (miền thật, đường dẫn chắc chắn không tồn tại) - đã sửa: không còn tệp nào được tạo ra khi lỗi.
    {
        const QString errDestPath = destDir + "/should-not-exist.bin";
        QDir().mkpath(destDir);
        QFile::remove(errDestPath);

        FileDownloader errDownloader;
        bool errGotFinished = false, errOk = true; // errOk mặc định true để xác nhận nó TỰ chuyển false
        QObject::connect(&errDownloader, &FileDownloader::itemFinished, [&](int, bool ok) {
            errOk = ok;
            errGotFinished = true;
        });
        const int errId = errDownloader.enqueue(
            "https://www.google.com/duong-dan-chac-chan-khong-ton-tai-oneforall-test-404", errDestPath);
        errDownloader.start(errId);

        QEventLoop errLoop;
        QTimer errTimeoutTimer;
        errTimeoutTimer.setSingleShot(true);
        QObject::connect(&errTimeoutTimer, &QTimer::timeout, &errLoop, &QEventLoop::quit);
        QObject::connect(&errDownloader, &FileDownloader::itemFinished, &errLoop, &QEventLoop::quit);
        errTimeoutTimer.start(15000);
        errLoop.exec();

        std::printf("Hoi quy loi HTTP that: gotFinished=%d ok=%d tonTaiTepRac=%d\n", errGotFinished, errOk,
                   QFileInfo::exists(errDestPath));
        CHECK(errGotFinished);
        if (errGotFinished)
        {
            CHECK(!errOk); // URL chắc chắn không tồn tại - phải báo thất bại thật
            CHECK(!QFileInfo::exists(errDestPath)); // KHÔNG được để lại tệp rác (nội dung trang lỗi)
            CHECK(!QFileInfo::exists(errDestPath + ".part")); // ... kể cả dưới dạng tệp tải dở
        }
        QDir(destDir).removeRecursively();
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: khong tu dong chay yt-dlp.exe that (quet trang/tai video nen tang) trong test\n");
    std::printf("nay - can tu kiem tra tay qua giao dien.\n");
    return g_fail == 0 ? 0 : 1;
}
