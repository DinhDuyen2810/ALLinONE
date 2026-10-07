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
#include <QTimer>
#include <cstdio>

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

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: khong tu dong chay yt-dlp.exe that (quet trang/tai video nen tang) trong test\n");
    std::printf("nay - can tu kiem tra tay qua giao dien.\n");
    return g_fail == 0 ? 0 : 1;
}
