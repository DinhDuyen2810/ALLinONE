// Kiểm thử "khói" cho giao diện Downloader: dựng cửa sổ, không crash, đủ 3 tab. CÓ tải THẬT một tệp nhỏ
// công khai, ổn định (logo Google, ~5KB) qua FileDownloader để xác nhận plumbing HTTP thật hoạt động -
// giống mẫu PublicIpChecker/SpeedTestRunner đã dùng thật trong module VPN/WiFi. KHÔNG tự động chạy
// yt-dlp.exe thật (cần mạng + có thể chậm, chỉ nên tự kiểm tra tay qua UI) và KHÔNG tự động quét trang
// web thật trong test này (PageMediaScanner đã có test lõi riêng bằng HTML mẫu dựng sẵn).
#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

#include "core/AppPaths.h"
#include "tools/downloader/DownloaderWindow.h"
#include "tools/downloader/engine/FileDownloader.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // Cửa sổ ghi log khi mở - ép dữ liệu vào thư mục tạm để test không ghi vào log thật của người dùng.
    QTemporaryDir dataDir;
    AppPaths::setDataDirOverride(dataDir.path());

    DownloaderWindow win;
    win.setAttribute(Qt::WA_DontShowOnScreen, true);
    win.show();
    app.processEvents();
    CHECK(true); // tới được đây tức là dựng cửa sổ không crash

    auto* tabs = win.findChild<QTabWidget*>();
    CHECK(tabs != nullptr && tabs->count() == 3);

    const auto buttons = win.findChildren<QPushButton*>();
    CHECK(buttons.size() >= 5);

    const auto labels = win.findChildren<QLabel*>();
    CHECK(labels.size() >= 3);

    // Hồi quy lỗi THẬT: mục đưa vào FileDownloader dùng chung KHÔNG qua tab "Tải trực tiếp" (đúng cách tab
    // "Quét trang web" làm) trước đây vẫn tải nhưng không có hàng nào trong bảng hàng đợi - không thấy tiến
    // độ/lỗi, không dừng/hủy được. KHÔNG start() ở đây nên không có yêu cầu mạng nào.
    {
        auto* shared = win.findChild<FileDownloader*>();
        CHECK(shared != nullptr);
        QTableWidget* queueTable = nullptr;
        for (auto* table : win.findChildren<QTableWidget*>())
            if (table->columnCount() == 5) queueTable = table; // bảng hàng đợi (bảng kết quả quét có 3 cột)
        CHECK(queueTable != nullptr);
        if (shared && queueTable)
        {
            CHECK(queueTable->rowCount() == 0);
            const int id = shared->enqueue("https://example.invalid/anh.jpg", QDir::tempPath() + "/oneforall_downloader_test/tu-tab-quet-trang.jpg");
            CHECK(queueTable->rowCount() == 1);
            CHECK(queueTable->item(0, 0) && queueTable->item(0, 0)->text() == "tu-tab-quet-trang.jpg");
            CHECK(queueTable->item(0, 4) && queueTable->item(0, 4)->text() == "Đang chờ");
            shared->cancel(id);
            CHECK(queueTable->item(0, 4) && queueTable->item(0, 4)->text() == "Đã hủy");
            CHECK(queueTable->rowCount() == 1);
        }
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
