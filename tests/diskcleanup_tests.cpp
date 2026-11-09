// Kiểm thử lõi Disk Cleanup. KHÔNG đụng vào thư mục hệ thống thật (Temp/Windows/SoftwareDistribution)
// - CategoryRegistry/CleanupScanner được test bằng môi trường giả trỏ vào QTemporaryDir. Phần
// RecycleBinOps CÓ chạy thật nhưng chỉ trên các tệp do chính test tạo ra trong thư mục tạm của nó
// (an toàn, có thể khôi phục từ Thùng rác) - không bao giờ gọi RecycleBinOps::empty() (sẽ xóa vĩnh
// viễn TOÀN BỘ Thùng rác thật của người dùng, kể cả các mục không liên quan tới test).
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTemporaryDir>
#include <QProcess>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <cstdio>
#include <limits>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include "core/AppPaths.h"

#include "tools/diskcleanup/engine/CategoryRegistry.h"
#include "tools/diskcleanup/engine/CleanupExecutor.h"
#include "tools/diskcleanup/engine/CleanupScanner.h"
#include "tools/diskcleanup/engine/DiskLayout.h"
#include "tools/diskcleanup/engine/DiskSpaceInfo.h"
#include "tools/diskcleanup/engine/DuplicateFinder.h"
#include "tools/diskcleanup/engine/FolderTreeScanner.h"
#include "tools/diskcleanup/engine/FsSafety.h"
#include "tools/diskcleanup/engine/LargeFileScanner.h"
#include "tools/diskcleanup/engine/PartitionManager.h"
#include "tools/diskcleanup/engine/ProcessMonitor.h"
#include "tools/diskcleanup/engine/RecycleBinOps.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

template <typename Func>
static bool waitUntil(Func predicate, int timeoutMs = 5000)
{
    if (predicate())
        return true;
    QEventLoop loop;
    QTimer giveUp;
    giveUp.setSingleShot(true);
    QObject::connect(&giveUp, &QTimer::timeout, &loop, &QEventLoop::quit);
    QTimer poll;
    QObject::connect(&poll, &QTimer::timeout, [&]() { if (predicate()) loop.quit(); });
    poll.start(10);
    giveUp.start(timeoutMs);
    loop.exec();
    return predicate();
}

static void writeFile(const QString& path, qint64 sizeBytes)
{
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    if (sizeBytes > 0)
        f.write(QByteArray(static_cast<int>(sizeBytes), 'x'));
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    // ---- CategoryRegistry: môi trường giả, không đụng hệ thống thật ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());

        QDir(tmp.path()).mkpath("Temp");
        QDir(tmp.path()).mkpath("Windows/Temp");
        QDir(tmp.path()).mkpath("Windows/SoftwareDistribution/Download");
        QDir(tmp.path()).mkpath("Local/Microsoft/Windows/Explorer");
        QDir(tmp.path()).mkpath("Roaming/Mozilla/Firefox/Profiles/abc123.default-release/cache2");
        QDir(tmp.path()).mkpath("Roaming/Mozilla/Firefox/Profiles/xyz789.default/cache2");
        writeFile(tmp.path() + "/Local/Microsoft/Windows/Explorer/thumbcache_256.db", 100);

        CleanupEnvironment env;
        env.tempDir = tmp.path() + "/Temp";
        env.windowsDir = tmp.path() + "/Windows";
        env.localAppData = tmp.path() + "/Local";
        env.roamingAppData = tmp.path() + "/Roaming";

        const QList<CleanupCategory> cats = CategoryRegistry::buildCategories(env);
        CHECK(!cats.isEmpty());

        auto find = [&](CleanupCategoryId id) -> const CleanupCategory* {
            for (const auto& c : cats)
                if (c.id == id)
                    return &c;
            return nullptr;
        };

        const CleanupCategory* userTemp = find(CleanupCategoryId::UserTemp);
        CHECK(userTemp && userTemp->rootPaths.size() == 1);
        CHECK(userTemp && userTemp->rootPaths.first() == env.tempDir);

        const CleanupCategory* winUpdate = find(CleanupCategoryId::WindowsUpdateCache);
        CHECK(winUpdate && winUpdate->rootPaths.size() == 1);

        // Hạng mục không tồn tại trên "máy giả" này (vd: WER, Prefetch chưa tạo thư mục) -> rootPaths rỗng
        const CleanupCategory* wer = find(CleanupCategoryId::WindowsErrorReports);
        CHECK(wer && wer->rootPaths.isEmpty());

        // Firefox: phải liệt kê ĐÚNG 2 profile giả đã tạo
        const CleanupCategory* ff = find(CleanupCategoryId::BrowserCacheFirefox);
        CHECK(ff && ff->rootPaths.size() == 2);
        if (ff)
        {
            CHECK(ff->rootPaths.contains(tmp.path() + "/Roaming/Mozilla/Firefox/Profiles/abc123.default-release/cache2"));
            CHECK(ff->rootPaths.contains(tmp.path() + "/Roaming/Mozilla/Firefox/Profiles/xyz789.default/cache2"));
        }

        // Windows.old: rootPaths rỗng vì thư mục giả không tồn tại
        const CleanupCategory* wo = find(CleanupCategoryId::WindowsOld);
        CHECK(wo && wo->rootPaths.isEmpty());
        CHECK(wo && wo->risk == CleanupRisk::High && !wo->keepRootFolder);

        CHECK(CategoryRegistry::riskName(CleanupRisk::Safe) == "An toàn");
        CHECK(CategoryRegistry::riskName(CleanupRisk::High) == "Rủi ro cao");
        CHECK(CategoryRegistry::categoryIdKey(CleanupCategoryId::UserTemp) == "UserTemp");
        CHECK(CategoryRegistry::categoryIdKey(CleanupCategoryId::WindowsOld) == "WindowsOld");

        // CleanupEnvironment::current() đọc biến môi trường THẬT - chỉ kiểm tra không crash và có giá trị
        const CleanupEnvironment real = CleanupEnvironment::current();
        CHECK(!real.tempDir.isEmpty());
        CHECK(!real.windowsDir.isEmpty());
    }

    // ---- CleanupScanner: quét thư mục giả với tệp kích thước đã biết ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        QDir root(tmp.path());
        root.mkpath("CatA");
        root.mkpath("CatB/subdir");
        root.mkpath("WholeFolder/nested");

        writeFile(tmp.path() + "/CatA/a1.tmp", 1000);
        writeFile(tmp.path() + "/CatA/a2.tmp", 2000);
        writeFile(tmp.path() + "/CatB/b1.log", 500);
        writeFile(tmp.path() + "/CatB/subdir/b2.log", 1500); // nằm trong thư mục con của CatB -> CatB/subdir được tính là 1 mục (thư mục)
        writeFile(tmp.path() + "/WholeFolder/nested/w1.dat", 3000);
        writeFile(tmp.path() + "/WholeFolder/nested/w2.dat", 4000);

        CleanupCategory catA;
        catA.id = CleanupCategoryId::UserTemp;
        catA.name = "CatA test";
        catA.risk = CleanupRisk::Safe;
        catA.rootPaths = {tmp.path() + "/CatA"};
        catA.keepRootFolder = true;

        CleanupCategory catB;
        catB.id = CleanupCategoryId::SystemLogs;
        catB.name = "CatB test";
        catB.risk = CleanupRisk::Caution;
        catB.rootPaths = {tmp.path() + "/CatB"};
        catB.keepRootFolder = true;

        CleanupCategory catWhole;
        catWhole.id = CleanupCategoryId::WindowsOld;
        catWhole.name = "WholeFolder test";
        catWhole.risk = CleanupRisk::High;
        catWhole.rootPaths = {tmp.path() + "/WholeFolder"};
        catWhole.keepRootFolder = false; // toàn bộ thư mục là 1 mục duy nhất

        CleanupCategory catMissing;
        catMissing.id = CleanupCategoryId::Prefetch;
        catMissing.name = "Missing test";
        catMissing.rootPaths = {tmp.path() + "/KhongTonTai"};
        catMissing.keepRootFolder = true;

        CleanupScanner scanner;
        scanner.setCategories({catA, catB, catWhole, catMissing});

        QList<CleanupItem> items;
        QObject::connect(&scanner, &CleanupScanner::itemFound, [&](CleanupItem i) { items.push_back(i); });
        bool finished = false;
        qint64 finalBytes = 0;
        int finalCount = 0;
        QObject::connect(&scanner, &CleanupScanner::scanFinished, [&](qint64 bytes, int count) {
            finished = true;
            finalBytes = bytes;
            finalCount = count;
        });

        scanner.start();
        CHECK(waitUntil([&] { return finished; }));
        scanner.wait();

        // CatA: 2 tệp riêng lẻ (1000 + 2000 = 3000 byte)
        // CatB: 1 tệp (b1.log, 500) + 1 thư mục con (subdir, chứa b2.log 1500) = 2 mục, 2000 byte
        // WholeFolder: 1 mục duy nhất (thư mục), 3000+4000 = 7000 byte
        // Missing: 0 mục
        CHECK(items.size() == 5); // 2 (CatA) + 2 (CatB) + 1 (WholeFolder)
        CHECK(finalCount == 5);
        CHECK(finalBytes == 3000 + 2000 + 7000);

        qint64 catASum = 0, catBSum = 0, wholeSum = 0;
        for (const auto& it : items)
        {
            if (it.categoryId == CleanupCategoryId::UserTemp) catASum += it.sizeBytes;
            else if (it.categoryId == CleanupCategoryId::SystemLogs) catBSum += it.sizeBytes;
            else if (it.categoryId == CleanupCategoryId::WindowsOld) wholeSum += it.sizeBytes;
        }
        CHECK(catASum == 3000);
        CHECK(catBSum == 2000);
        CHECK(wholeSum == 7000);

        // Mặc định chọn sẵn: Safe=true, Caution/High=false
        for (const auto& it : items)
        {
            if (it.categoryId == CleanupCategoryId::UserTemp) CHECK(it.selected == true);
            if (it.categoryId == CleanupCategoryId::SystemLogs) CHECK(it.selected == false);
            if (it.categoryId == CleanupCategoryId::WindowsOld) CHECK(it.selected == false);
        }

        // Dừng giữa chừng không crash, phát scanStopped thay vì scanFinished
        CleanupScanner scanner2;
        CleanupCategory bigCat = catWhole;
        scanner2.setCategories({bigCat, catA, catB});
        bool stopped = false;
        QObject::connect(&scanner2, &CleanupScanner::scanStopped, [&] { stopped = true; });
        // startScan() đặt lại cờ dừng TRƯỚC khi luồng chạy - requestStop() gọi ngay sau đó KHÔNG được mất
        // (trước đây run() tự xóa cờ ở dòng đầu, lượt quét có thể chạy tới hết dù đã yêu cầu dừng).
        scanner2.startScan();
        scanner2.requestStop();
        CHECK(waitUntil([&] { return stopped || !scanner2.isRunning(); }));
        scanner2.wait();
        CHECK(stopped);
    }

    // ---- CleanupScanner: lối tắt (.lnk) KHÔNG được tính theo kích thước thư mục đích ----
    // Hồi quy cho lỗi thật phát hiện khi quét tay trên máy thật: Qt coi .lnk như symlink và TỰ ĐỘNG
    // đi theo nó (isDir()/entryList() phản ánh thư mục ĐÍCH) - hạng mục "Recent Items" toàn lối tắt,
    // nên một lối tắt trỏ tới thư mục Tải xuống/cả ổ đĩa từng bị cộng nhầm hàng chục GB vào kích thước.
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        QDir root(tmp.path());
        root.mkpath("target");
        // "Thư mục đích" của lối tắt cố tình để LỚN HƠN NHIỀU chính tệp .lnk, để lỗi (nếu tái xuất hiện)
        // chắc chắn làm lệch tổng kích thước một cách rõ ràng, không thể nhầm lẫn với sai số nhỏ.
        writeFile(tmp.path() + "/target/big.bin", 50000);
        writeFile(tmp.path() + "/plain.tmp", 777); // tệp thường, không phải lối tắt, để đối chứng

        const QString linkPath = tmp.path() + "/shortcut.lnk";
        const bool linked = QFile::link(tmp.path() + "/target", linkPath);
        CHECK(linked);

        if (linked)
        {
            CleanupCategory catLink;
            catLink.id = CleanupCategoryId::RecentItems;
            catLink.name = "Link test";
            catLink.risk = CleanupRisk::Caution;
            catLink.rootPaths = {tmp.path()};
            catLink.keepRootFolder = true;

            CleanupScanner scanner;
            scanner.setCategories({catLink});

            QList<CleanupItem> items;
            QObject::connect(&scanner, &CleanupScanner::itemFound, [&](CleanupItem i) { items.push_back(i); });
            bool finished = false;
            qint64 finalBytes = 0;
            QObject::connect(&scanner, &CleanupScanner::scanFinished, [&](qint64 bytes, int) {
                finished = true;
                finalBytes = bytes;
            });
            scanner.start();
            CHECK(waitUntil([&] { return finished; }));
            scanner.wait();

            // 3 mục ở cấp gốc: shortcut.lnk, plain.tmp, target/ (thư mục thật) - KHÔNG được cộng dồn
            // kích thước của target/ hai lần (một lần qua chính nó, một lần qua lối tắt trỏ tới nó -
            // lỗi gốc từng khiến finalBytes xấp xỉ 50000+777+50000, gần gấp đôi giá trị đúng bên dưới).
            // Chính tệp .lnk chỉ nặng vài trăm-vài nghìn byte nên tổng đúng xấp xỉ, không chính xác
            // tuyệt đối, 50000 (target) + 777 (plain.tmp).
            CHECK(items.size() == 3);
            CHECK(finalBytes >= 50000 + 777);
            CHECK(finalBytes < 50000 + 777 + 10000); // không bị cộng nhầm thêm ~50000 của lối tắt

            bool foundLink = false;
            for (const auto& it : items)
            {
                if (it.path == QDir(linkPath).absolutePath() || QFileInfo(it.path).fileName() == "shortcut.lnk")
                {
                    foundLink = true;
                    // Lỗi gốc: sizeBytes từng = 50000 (kích thước target/) thay vì kích thước thật của .lnk
                    CHECK(it.sizeBytes < 10000); // lối tắt Windows chỉ vài trăm-vài nghìn byte, không bao giờ tới 50000
                    CHECK(it.isDirectory == false); // chính .lnk luôn là một TỆP, không phải thư mục
                }
            }
            CHECK(foundLink);
        }
    }

    // ---- RecycleBinOps: chuyển tệp THẬT (do test tự tạo) vào Thùng rác - an toàn, có thể khôi phục ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString f1 = tmp.path() + "/delete_me_1.tmp";
        const QString f2 = tmp.path() + "/delete_me_2.tmp";
        writeFile(f1, 10);
        writeFile(f2, 20);
        CHECK(QFile::exists(f1) && QFile::exists(f2));

        QString err;
        CHECK(RecycleBinOps::moveToRecycleBin({f1, f2}, &err));
        CHECK(err.isEmpty());
        CHECK(!QFile::exists(f1));
        CHECK(!QFile::exists(f2));

        // Đường dẫn không tồn tại: không được crash (SHFileOperationW có thể báo lỗi hoặc bỏ qua tùy hệ thống)
        QString err2;
        RecycleBinOps::moveToRecycleBin({tmp.path() + "/khong_ton_tai.tmp"}, &err2);
        CHECK(true); // tới được đây tức là không crash

        // queryStatus() chỉ đọc, không sửa gì - phải chạy được không crash
        const RecycleBinOps::Status status = RecycleBinOps::queryStatus();
        CHECK(status.itemCount >= 0 && status.totalBytes >= 0);

        // Danh sách rỗng -> coi như thành công, không gọi Shell API
        QString err3;
        CHECK(RecycleBinOps::moveToRecycleBin({}, &err3));

        // CHỦ Ý KHÔNG gọi RecycleBinOps::empty() ở đây - sẽ xóa vĩnh viễn toàn bộ Thùng rác thật.
    }

    // ---- CleanupExecutor: xóa thật (qua Thùng rác) trên QThread riêng ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString f1 = tmp.path() + "/exec_test_1.tmp";
        const QString f2 = tmp.path() + "/exec_test_2.tmp";
        writeFile(f1, 123);
        writeFile(f2, 456);

        CleanupExecutor executor;
        executor.setItems({f1, f2}, {123, 456});

        bool done = false;
        bool success = false;
        qint64 freed = 0;
        int count = 0;
        QObject::connect(&executor, &CleanupExecutor::executionFinished,
                         [&](bool ok, QString, qint64 bytes, int cnt) {
                             done = true;
                             success = ok;
                             freed = bytes;
                             count = cnt;
                         });
        executor.start();
        CHECK(waitUntil([&] { return done; }));
        executor.wait();

        CHECK(success);
        CHECK(freed == 123 + 456);
        CHECK(count == 2);
        CHECK(!QFile::exists(f1) && !QFile::exists(f2));
    }

    // ---- CleanupExecutor: hồi quy lỗi THẬT (mã 0x2) - tệp tự biến mất giữa lúc quét và lúc xóa ----
    // Đây CHÍNH LÀ tình huống người dùng gặp: "Tệp tạm" đổi liên tục, một tệp đã bị Windows/ứng dụng
    // khác xóa trước khi bấm "Dọn dẹp" - trước đây SHFileOperationW báo lỗi ERROR_FILE_NOT_FOUND cho
    // CẢ LÔ, khiến toàn bộ thao tác (kể cả các tệp khác vẫn xóa được) bị báo là THẤT BẠI với mã lỗi hex
    // khó hiểu. Giờ phải lọc trước + kiểm tra lại sau, báo cáo số liệu THẬT, vẫn coi là thành công.
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString real1 = tmp.path() + "/race_real_1.tmp";
        const QString real2 = tmp.path() + "/race_real_2.tmp";
        const QString vanished = tmp.path() + "/race_vanished.tmp"; // KHÔNG tạo tệp này - mô phỏng đã tự mất
        writeFile(real1, 100);
        writeFile(real2, 200);
        CHECK(QFile::exists(real1) && QFile::exists(real2));
        CHECK(!QFile::exists(vanished));

        CleanupExecutor executor;
        // 'vanished' nằm giữa danh sách, kích thước 999 - trước đây sẽ khiến executor báo freed=0 dù
        // real1/real2 hoàn toàn xóa được.
        executor.setItems({real1, vanished, real2}, {100, 999, 200});

        bool done = false;
        bool success = false;
        QString note;
        qint64 freed = -1;
        int count = -1;
        QObject::connect(&executor, &CleanupExecutor::executionFinished,
                         [&](bool ok, QString n, qint64 bytes, int cnt) {
                             done = true;
                             success = ok;
                             note = n;
                             freed = bytes;
                             count = cnt;
                         });
        executor.start();
        CHECK(waitUntil([&] { return done; }));
        executor.wait();

        CHECK(success); // KHÔNG được coi là thất bại chỉ vì 1 mục đã tự mất từ trước
        CHECK(freed == 100 + 999 + 200); // tính cả phần "đã tự mất" (999) vào tổng giải phóng - mục tiêu đã đạt
        CHECK(count == 3); // cả 3 mục đều coi là đã xử lý xong (2 xóa thật + 1 đã tự mất từ trước)
        CHECK(!QFile::exists(real1) && !QFile::exists(real2)); // vẫn xóa thật 2 tệp còn lại, không bị bỏ qua
        CHECK(note.isEmpty()); // tệp tự mất từ trước không cần ghi chú gì - coi là bình thường, không phải lỗi
    }

    // ---- DiskSpaceInfo: đọc thông tin ổ đĩa THẬT (chỉ đọc, an toàn) ----
    {
        const QList<DriveSpaceInfo> drives = DiskSpaceInfo::listDrives();
        CHECK(!drives.isEmpty()); // máy Windows nào cũng có ít nhất 1 ổ
        for (const auto& d : drives)
        {
            CHECK(!d.rootPath.isEmpty());
            CHECK(d.totalBytes > 0);
            CHECK(d.freeBytes >= 0);
            CHECK(d.freeBytes <= d.totalBytes);
            CHECK(d.usedBytes() == d.totalBytes - d.freeBytes);
            CHECK(d.usedRatio() >= 0.0 && d.usedRatio() <= 1.0);
        }
    }

    // ---- LargeFileScanner: tìm tệp lớn trong thư mục giả ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        QDir(tmp.path()).mkpath("sub/deep");
        writeFile(tmp.path() + "/small.bin", 100);
        writeFile(tmp.path() + "/big1.bin", 5000);
        writeFile(tmp.path() + "/sub/big2.bin", 8000);
        writeFile(tmp.path() + "/sub/deep/big3.bin", 6000);
        writeFile(tmp.path() + "/sub/deep/biggest.bin", 9000);

        LargeFileScanner scanner;
        scanner.setRootPath(tmp.path());
        scanner.setMinSizeBytes(4000); // loại small.bin (100 byte)
        scanner.setMaxResults(200);

        QList<LargeFileEntry> results;
        bool finished = false;
        QObject::connect(&scanner, &LargeFileScanner::scanFinished, [&](QList<LargeFileEntry> r) {
            results = r;
            finished = true;
        });
        scanner.start();
        CHECK(waitUntil([&] { return finished; }));
        scanner.wait();

        CHECK(results.size() == 4); // big1, big2, big3, biggest (không có small)
        // Phải sắp xếp giảm dần theo kích thước
        for (int i = 1; i < results.size(); ++i)
            CHECK(results[i - 1].sizeBytes >= results[i].sizeBytes);
        CHECK(results.first().sizeBytes == 9000); // biggest.bin đứng đầu
        CHECK(results.first().path.endsWith("biggest.bin"));
        for (const auto& r : results)
            CHECK(r.lastModified.isValid());

        // Giới hạn kết quả (maxResults) hoạt động đúng
        LargeFileScanner scanner2;
        scanner2.setRootPath(tmp.path());
        scanner2.setMinSizeBytes(4000);
        scanner2.setMaxResults(2);
        QList<LargeFileEntry> results2;
        bool finished2 = false;
        QObject::connect(&scanner2, &LargeFileScanner::scanFinished, [&](QList<LargeFileEntry> r) {
            results2 = r;
            finished2 = true;
        });
        scanner2.start();
        CHECK(waitUntil([&] { return finished2; }));
        scanner2.wait();
        CHECK(results2.size() == 2);
        CHECK(results2[0].sizeBytes == 9000 && results2[1].sizeBytes == 8000); // 2 tệp lớn nhất

        // Thư mục không tồn tại -> không crash, trả về rỗng
        LargeFileScanner scanner3;
        scanner3.setRootPath(tmp.path() + "/khong_ton_tai");
        scanner3.setMinSizeBytes(1);
        QList<LargeFileEntry> results3;
        bool finished3 = false;
        QObject::connect(&scanner3, &LargeFileScanner::scanFinished, [&](QList<LargeFileEntry> r) {
            results3 = r;
            finished3 = true;
        });
        scanner3.start();
        CHECK(waitUntil([&] { return finished3; }));
        scanner3.wait();
        CHECK(results3.isEmpty());
    }

    // ---- DuplicateFinder: tìm tệp trùng lặp nội dung trong thư mục giả ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        QDir(tmp.path()).mkpath("sub");

        // Nhóm trùng lặp THẬT: 3 tệp cùng nội dung (writeFile() ghi toàn ký tự 'x') ở thư mục khác nhau
        writeFile(tmp.path() + "/dup_a.bin", 5000);
        writeFile(tmp.path() + "/dup_b.bin", 5000);
        writeFile(tmp.path() + "/sub/dup_c.bin", 5000);

        // Cùng KÍCH THƯỚC (5000) nhưng nội dung khác - phải KHÔNG bị coi là trùng lặp (xác nhận
        // bước 2 hash nội dung hoạt động, không chỉ dựa vào bước 1 nhóm theo kích thước)
        {
            QFile f(tmp.path() + "/sub/different.bin");
            f.open(QIODevice::WriteOnly);
            f.write(QByteArray(5000, 'y'));
        }

        writeFile(tmp.path() + "/alone.bin", 7000); // không trùng ai

        // Nhỏ hơn ngưỡng tối thiểu - trùng nội dung với nhau nhưng vẫn phải bị loại
        writeFile(tmp.path() + "/tiny_a.bin", 10);
        writeFile(tmp.path() + "/tiny_b.bin", 10);

        DuplicateFinder finder;
        finder.setRootPath(tmp.path());
        finder.setMinSizeBytes(100);

        // scanFinished giờ chỉ phát MỘT LẦN DUY NHẤT với cả danh sách (không còn groupFound từng
        // nhóm một) - xem lý do trong DuplicateFinder.h: tránh làm luồng giao diện phải dựng hàng
        // nghìn mục cây liên tiếp theo từng tín hiệu riêng lẻ (nguy cơ Windows báo "Không phản hồi"
        // với thư mục kiểu cache trình duyệt có nhiều tệp trùng kích thước khối cố định).
        QList<DuplicateGroup> groups;
        bool finished = false;
        qint64 finalWasted = -1;
        int finalTotalGroups = -1;
        QObject::connect(&finder, &DuplicateFinder::scanFinished, [&](QList<DuplicateGroup> g, qint64 wasted, int total) {
            groups = g;
            finished = true;
            finalWasted = wasted;
            finalTotalGroups = total;
        });
        finder.start();
        CHECK(waitUntil([&] { return finished; }, 15000));
        finder.wait();

        CHECK(groups.size() == 1); // đúng 1 nhóm trùng lặp thật
        CHECK(finalTotalGroups == 1);
        if (groups.size() == 1)
        {
            CHECK(groups[0].paths.size() == 3);
            CHECK(groups[0].sizeEachBytes == 5000);
            CHECK(groups[0].wastedBytes() == 5000 * 2); // giữ 1 bản, 2 bản còn lại là lãng phí
            CHECK(!groups[0].hashHex.isEmpty());
            int matchCount = 0;
            for (const auto& p : groups[0].paths)
                if (p.endsWith("dup_a.bin") || p.endsWith("dup_b.bin") || p.endsWith("dup_c.bin"))
                    ++matchCount;
            CHECK(matchCount == 3); // không lẫn different.bin/alone.bin
        }
        CHECK(finalWasted == 5000 * 2);

        // Lối tắt (.lnk) KHÔNG được đi theo (QDir::NoSymLinks) - nếu không sẽ có nguy cơ hash nhầm
        // một tệp đích hoàn toàn khác với đường dẫn gốc đang quét (giống lỗi đã sửa ở CleanupScanner).
        const bool linked = QFile::link(tmp.path() + "/dup_a.bin", tmp.path() + "/shortcut_to_dup.lnk");
        if (linked)
        {
            DuplicateFinder finder2;
            finder2.setRootPath(tmp.path());
            finder2.setMinSizeBytes(100);
            QList<DuplicateGroup> groups2;
            bool finished2 = false;
            QObject::connect(&finder2, &DuplicateFinder::scanFinished, [&](QList<DuplicateGroup> g, qint64, int) {
                groups2 = g;
                finished2 = true;
            });
            finder2.start();
            CHECK(waitUntil([&] { return finished2; }, 15000));
            finder2.wait();
            for (const auto& g : groups2)
                for (const auto& p : g.paths)
                    CHECK(!p.endsWith(".lnk")); // lối tắt không bao giờ là ứng viên trùng lặp
        }

        // Dừng giữa chừng không crash
        DuplicateFinder finder3;
        finder3.setRootPath(tmp.path());
        finder3.setMinSizeBytes(1);
        bool stopped = false;
        QObject::connect(&finder3, &DuplicateFinder::scanStopped, [&] { stopped = true; });
        finder3.startScan();
        finder3.requestStop();
        CHECK(waitUntil([&] { return stopped || !finder3.isRunning(); }));
        finder3.wait();
        CHECK(stopped); // yêu cầu dừng ngay sau startScan() không được mất

        // Thư mục không tồn tại -> không crash, 0 nhóm
        DuplicateFinder finder4;
        finder4.setRootPath(tmp.path() + "/khong_ton_tai");
        finder4.setMinSizeBytes(1);
        bool finished4 = false;
        int count4 = -1;
        QObject::connect(&finder4, &DuplicateFinder::scanFinished, [&](QList<DuplicateGroup>, qint64, int total) {
            finished4 = true;
            count4 = total;
        });
        finder4.start();
        CHECK(waitUntil([&] { return finished4; }));
        finder4.wait();
        CHECK(count4 == 0);
    }

    // ---- DuplicateFinder: nhiều nhóm (giống thư mục cache trình duyệt) - giới hạn + sắp xếp đúng ----
    // Hồi quy cho lỗi thật phát hiện khi dùng tay: quét một thư mục kiểu cache trình duyệt thật (hàng
    // nghìn tệp trùng kích thước khối cố định) từng khiến cửa sổ bị Windows báo "Không phản hồi" vì
    // luồng giao diện phải dựng cây giao diện cho TỪNG nhóm một theo từng tín hiệu riêng lẻ, không
    // giới hạn. Mô phỏng bằng 20 nhóm trùng lặp kích thước khác nhau, xác nhận setMaxGroups() cắt
    // đúng số lượng VÀ giữ lại đúng các nhóm lãng phí nhiều nhất (sắp xếp giảm dần).
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());

        // Tạo 20 nhóm, mỗi nhóm 2 tệp cùng nội dung nhưng kích thước khác nhau (1000, 2000, ..., 20000
        // byte) - nhóm có kích thước CÀNG LỚN thì wastedBytes() càng lớn (vì chỉ 2 bản/nhóm, lãng phí
        // = sizeEachBytes * 1).
        for (int i = 1; i <= 20; ++i)
        {
            const qint64 size = i * 1000;
            writeFile(tmp.path() + QString("/g%1_a.bin").arg(i), size);
            writeFile(tmp.path() + QString("/g%1_b.bin").arg(i), size);
        }

        DuplicateFinder finder;
        finder.setRootPath(tmp.path());
        finder.setMinSizeBytes(1);
        finder.setMaxGroups(5); // chỉ giữ 5 nhóm lãng phí nhiều nhất trong số 20 nhóm tìm được

        QList<DuplicateGroup> groups;
        bool finished = false;
        qint64 finalWasted = -1;
        int finalTotalGroups = -1;
        QObject::connect(&finder, &DuplicateFinder::scanFinished, [&](QList<DuplicateGroup> g, qint64 wasted, int total) {
            groups = g;
            finished = true;
            finalWasted = wasted;
            finalTotalGroups = total;
        });
        finder.start();
        CHECK(waitUntil([&] { return finished; }, 15000));
        finder.wait();

        CHECK(finalTotalGroups == 20); // tổng số nhóm THẬT tìm được, không bị giới hạn bởi setMaxGroups
        CHECK(groups.size() == 5);     // nhưng danh sách trả về chỉ có 5 (đúng setMaxGroups)
        // finalWasted là tổng lãng phí của TẤT CẢ 20 nhóm (1000+2000+...+20000 = 210000), không chỉ 5
        // nhóm hiển thị - để người dùng biết tổng tiềm năng, dù danh sách bị cắt bớt.
        CHECK(finalWasted == 210000);

        // 5 nhóm trả về phải là 5 nhóm LỚN NHẤT (20000,19000,18000,17000,16000 byte mỗi tệp) và phải
        // được sắp xếp giảm dần theo wastedBytes().
        CHECK(groups.size() == 5 && groups[0].sizeEachBytes == 20000);
        for (int i = 1; i < groups.size(); ++i)
            CHECK(groups[i - 1].wastedBytes() >= groups[i].wastedBytes());
        if (groups.size() == 5)
            CHECK(groups[4].sizeEachBytes == 16000); // nhóm nhỏ nhất trong 5 nhóm được giữ
    }

    // ---- PartitionManager: parse JSON thuần bằng dữ liệu mẫu (không gọi PowerShell/đổi gì thật) ----
    {
        using namespace PartitionManager;

        // Mảng JSON bình thường (>= 2 phân vùng)
        const QByteArray arrayJson = R"([
            {"DiskNumber":0,"PartitionNumber":2,"DriveLetter":"C","Type":"Basic","Size":500105753600,
             "IsBoot":true,"IsSystem":false,"IsActive":true,"FileSystem":"NTFS","Label":"Windows","SizeRemaining":123456789},
            {"DiskNumber":0,"PartitionNumber":1,"DriveLetter":"","Type":"System","Size":104857600,
             "IsBoot":false,"IsSystem":true,"IsActive":false,"FileSystem":"FAT32","Label":"","SizeRemaining":50000000}
        ])";
        QString err;
        const auto list = internal::parsePartitionsJson(arrayJson, &err);
        CHECK(err.isEmpty());
        CHECK(list.size() == 2);
        if (list.size() == 2)
        {
            CHECK(list[0].diskNumber == 0 && list[0].partitionNumber == 2);
            CHECK(list[0].driveLetter == "C");
            CHECK(list[0].sizeBytes == 500105753600LL);
            CHECK(list[0].isBoot == true);
            CHECK(list[0].fileSystem == "NTFS");
            CHECK(list[1].driveLetter.isEmpty()); // phân vùng EFI/System không có ổ đĩa logic
            CHECK(list[1].isSystem == true);
        }

        // PowerShell 5.1: CHỈ 1 phân vùng -> ConvertTo-Json trả về 1 OBJECT đơn, KHÔNG bọc mảng -
        // đây chính là lỗi thật dễ gặp nếu chỉ giả định luôn luôn là mảng.
        const QByteArray singleObjectJson = R"({"DiskNumber":1,"PartitionNumber":1,"DriveLetter":"D","Type":"Basic",
            "Size":1000000000000,"IsBoot":false,"IsSystem":false,"IsActive":false,"FileSystem":"NTFS","Label":"Data","SizeRemaining":999999999})";
        QString err2;
        const auto list2 = internal::parsePartitionsJson(singleObjectJson, &err2);
        CHECK(err2.isEmpty());
        CHECK(list2.size() == 1);
        if (list2.size() == 1)
            CHECK(list2[0].driveLetter == "D");

        // Rỗng -> không có phân vùng nào, không phải lỗi
        QString err3;
        CHECK(internal::parsePartitionsJson("", &err3).isEmpty());
        CHECK(err3.isEmpty());

        // JSON hỏng -> báo lỗi rõ ràng, không crash, trả về danh sách rỗng
        QString err4;
        CHECK(internal::parsePartitionsJson("{khong phai json hop le", &err4).isEmpty());
        CHECK(!err4.isEmpty());

        // SupportedSizeRange
        QString err5;
        const auto range = internal::parseSupportedSizeJson(R"({"SizeMin":100000000,"SizeMax":500000000000})", &err5);
        CHECK(err5.isEmpty());
        CHECK(range.ok);
        CHECK(range.minBytes == 100000000LL);
        CHECK(range.maxBytes == 500000000000LL);

        QString err6;
        const auto badRange = internal::parseSupportedSizeJson("khong phai json", &err6);
        CHECK(!badRange.ok);
        CHECK(!err6.isEmpty());

        // Dựng script PowerShell: chỉ kiểm tra NỘI DUNG CHUỖI (thuần, không gọi powershell.exe, không
        // đổi gì thật trên máy) - xác nhận đúng cmdlet + đúng tham số được truyền vào.
        CHECK(internal::buildListPartitionsScript().contains("Get-Partition"));
        CHECK(internal::buildListPartitionsScript().contains("Get-Volume"));

        const QString sizeScript = internal::buildSupportedSizeScript(2, 3);
        CHECK(sizeScript.contains("Get-PartitionSupportedSize"));
        CHECK(sizeScript.contains("-DiskNumber 2"));
        CHECK(sizeScript.contains("-PartitionNumber 3"));

        const QString resizeScript = internal::buildResizeScript(2, 3, 123456789LL);
        CHECK(resizeScript.contains("Resize-Partition"));
        CHECK(resizeScript.contains("-DiskNumber 2"));
        CHECK(resizeScript.contains("-PartitionNumber 3"));
        CHECK(resizeScript.contains("-Size 123456789"));
    }

    // ---- PartitionManager: truy vấn THẬT nhưng CHỈ ĐỌC (liệt kê phân vùng, kiểm tra quyền) ----
    // CHỦ Ý KHÔNG gọi resizePartition() ở đây dưới bất kỳ hình thức nào - đó là thao tác đĩa thật,
    // không có "hoàn tác" an toàn, không phù hợp để chạy trong bộ test tự động.
    {
        const bool elevated = PartitionManager::isElevated();
        std::printf("PartitionManager: tien trinh hien tai %s quyen Administrator\n", elevated ? "CO" : "KHONG CO");
        CHECK(true); // tới được đây tức là isElevated() không crash

        QString error;
        const auto partitions = PartitionManager::listPartitions(&error);
        std::printf("PartitionManager: tim thay %lld phan vung%s%s\n", static_cast<long long>(partitions.size()),
                   error.isEmpty() ? "" : " (loi: ", error.isEmpty() ? "" : qPrintable(error + ")"));
        CHECK(!partitions.isEmpty()); // máy Windows nào cũng có ít nhất phân vùng chứa Windows
        for (const auto& p : partitions)
        {
            CHECK(p.diskNumber >= 0);
            CHECK(p.partitionNumber >= 0);
            CHECK(p.sizeBytes > 0);
        }
    }

    // =====================================================================================
    // Hồi quy cho đợt rà soát "chống xóa nhầm": mọi thứ dưới đây chỉ đụng tới QTemporaryDir.
    // =====================================================================================

    // Tạo junction bằng mklink /J (không cần quyền Administrator). Đích LUÔN nằm trong thư mục tạm của test.
    auto makeJunction = [](const QString& link, const QString& target) -> bool {
        QProcess proc;
        proc.start(AppPaths::systemExecutable("cmd.exe"),
                   {"/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)});
        return proc.waitForFinished(10000) && proc.exitCode() == 0 && FsSafety::rawInfo(link).isLink;
    };

    // ---- FsSafety::unsafeCleanupRootReason: thư mục gốc dọn dẹp phải hợp lý ----
    {
        const QStringList prot = {"C:/Users/Ai Do", "C:/Program Files", "C:/Windows"};
        CHECK(!FsSafety::unsafeCleanupRootReason("", prot).isEmpty());                 // rỗng
        CHECK(!FsSafety::unsafeCleanupRootReason("   ", prot).isEmpty());
        CHECK(!FsSafety::unsafeCleanupRootReason("/Temp", prot).isEmpty());            // sinh ra khi biến môi trường rỗng
        CHECK(!FsSafety::unsafeCleanupRootReason("Windows.old", prot).isEmpty());      // tương đối
        CHECK(!FsSafety::unsafeCleanupRootReason("C:Temp", prot).isEmpty());           // tương đối theo ổ
        CHECK(!FsSafety::unsafeCleanupRootReason("C:/", prot).isEmpty());              // gốc ổ đĩa
        CHECK(!FsSafety::unsafeCleanupRootReason("D:\\", prot).isEmpty());
        CHECK(!FsSafety::unsafeCleanupRootReason("d:", prot).isEmpty());
        CHECK(!FsSafety::unsafeCleanupRootReason("C:/Users/Ai Do", prot).isEmpty());   // trùng thư mục được bảo vệ
        CHECK(!FsSafety::unsafeCleanupRootReason("c:\\users\\ai do\\", prot).isEmpty()); // không phân biệt hoa thường/dấu gạch
        CHECK(!FsSafety::unsafeCleanupRootReason("C:/Users", prot).isEmpty());         // CHỨA thư mục được bảo vệ
        CHECK(!FsSafety::unsafeCleanupRootReason("C:/Windows", prot).isEmpty());
        CHECK(!FsSafety::unsafeCleanupRootReason("C:/Program Files/../Windows", prot).isEmpty()); // có ".." vẫn nhận ra
        CHECK(FsSafety::unsafeCleanupRootReason("C:/Windows/Temp", prot).isEmpty());   // thư mục CON thì hợp lệ
        CHECK(FsSafety::unsafeCleanupRootReason("C:/Users/Ai Do/AppData/Local/Temp", prot).isEmpty());
        CHECK(FsSafety::unsafeCleanupRootReason("C:/UsersBackup", prot).isEmpty());    // chỉ giống tiền tố, không phải cha
        CHECK(!FsSafety::unsafeCleanupRootReason("//may/chiase", prot).isEmpty());     // gốc thư mục chia sẻ mạng
        CHECK(FsSafety::unsafeCleanupRootReason("//may/chiase/Temp", prot).isEmpty());

        // Máy thật: các thư mục được bảo vệ đọc được và chính chúng bị từ chối
        const QStringList real = FsSafety::systemProtectedDirs();
        CHECK(!real.isEmpty());
        for (const QString& dir : real)
            CHECK(!FsSafety::unsafeCleanupRootReason(dir, real).isEmpty());
        CHECK(!FsSafety::windowsDirectory().isEmpty());
        // %TEMP% thật của máy build phải vẫn được chấp nhận (không chặn nhầm trường hợp bình thường)
        const CleanupEnvironment realEnv = CleanupEnvironment::current();
        CHECK(FsSafety::unsafeCleanupRootReason(realEnv.tempDir, realEnv.protectedDirs + QStringList{realEnv.windowsDir}).isEmpty());
    }

    // ---- CategoryRegistry: biến môi trường rỗng/trỏ sai KHÔNG được sinh ra hạng mục nào ----
    {
        // Mọi biến đều rỗng -> không hạng mục nào có thư mục gốc (trước đây sinh "/Temp", "/Logs",
        // "/Microsoft/..." - bị Windows hiểu theo gốc ổ đĩa hiện hành - và "Windows.old" tương đối).
        const QList<CleanupCategory> none = CategoryRegistry::buildCategories(CleanupEnvironment{});
        CHECK(!none.isEmpty());
        for (const auto& c : none)
            CHECK(c.rootPaths.isEmpty());

        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        QDir(tmp.path()).mkpath("Temp");
        QDir(tmp.path()).mkpath("Roaming/Microsoft/Windows/Recent");

        auto find = [](const QList<CleanupCategory>& cats, CleanupCategoryId id) -> const CleanupCategory* {
            for (const auto& c : cats)
                if (c.id == id)
                    return &c;
            return nullptr;
        };

        // TEMP trỏ vào gốc ổ đĩa -> hạng mục "Tệp tạm" không khả dụng (không phải "cả ổ C: là rác")
        CleanupEnvironment rootEnv;
        rootEnv.tempDir = QDir(tmp.path()).rootPath(); // "C:/"
        const auto rootCats = CategoryRegistry::buildCategories(rootEnv);
        const CleanupCategory* rootTemp = find(rootCats, CleanupCategoryId::UserTemp);
        CHECK(rootTemp && rootTemp->rootPaths.isEmpty());

        // TEMP trỏ đúng vào một thư mục được bảo vệ (vd hồ sơ người dùng) -> cũng không khả dụng
        CleanupEnvironment protEnv;
        protEnv.tempDir = tmp.path() + "/Temp";
        protEnv.roamingAppData = tmp.path() + "/Roaming";
        protEnv.protectedDirs = {tmp.path() + "/Temp"};
        const auto protCats = CategoryRegistry::buildCategories(protEnv);
        const CleanupCategory* protTemp = find(protCats, CleanupCategoryId::UserTemp);
        CHECK(protTemp && protTemp->rootPaths.isEmpty());

        // Bình thường: khả dụng, có lọc "mới sửa gần đây"; Recent loại trừ 2 thư mục chứa mục ghim
        protEnv.protectedDirs.clear();
        const auto okCats = CategoryRegistry::buildCategories(protEnv);
        const CleanupCategory* okTemp = find(okCats, CleanupCategoryId::UserTemp);
        CHECK(okTemp && okTemp->rootPaths.size() == 1);
        CHECK(okTemp && okTemp->minAgeSeconds > 0);
        const CleanupCategory* recent = find(okCats, CleanupCategoryId::RecentItems);
        CHECK(recent && recent->rootPaths.size() == 1);
        CHECK(recent && recent->excludeNames.contains("AutomaticDestinations"));
        CHECK(recent && recent->excludeNames.contains("CustomDestinations"));
    }

    // ---- CleanupScanner: excludeNames + minAgeSeconds + gốc không hợp lệ ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        QDir root(tmp.path());
        root.mkpath("Recent/AutomaticDestinations");
        root.mkpath("Recent/CustomDestinations");
        root.mkpath("Recent/OtherDir");
        writeFile(tmp.path() + "/Recent/AutomaticDestinations/pinned.automaticDestinations-ms", 4000);
        writeFile(tmp.path() + "/Recent/CustomDestinations/pinned.customDestinations-ms", 4000);
        writeFile(tmp.path() + "/Recent/OtherDir/x.bin", 300);
        writeFile(tmp.path() + "/Recent/doc.txt", 100);

        auto scan = [&](const CleanupCategory& cat) {
            QList<CleanupItem> items;
            CleanupScanner scanner;
            scanner.setCategories({cat});
            QObject::connect(&scanner, &CleanupScanner::itemFound, [&](CleanupItem i) { items.push_back(i); });
            scanner.startScan();
            scanner.wait();
            return items;
        };

        CleanupCategory cat;
        cat.id = CleanupCategoryId::RecentItems;
        cat.name = "Exclude test";
        cat.rootPaths = {tmp.path() + "/Recent"};
        cat.excludeNames = {"automaticdestinations", "CustomDestinations"}; // không phân biệt hoa thường
        const auto items = scan(cat);
        CHECK(items.size() == 2); // OtherDir + doc.txt
        for (const auto& it : items)
        {
            CHECK(!it.path.contains("AutomaticDestinations"));
            CHECK(!it.path.contains("CustomDestinations"));
        }

        // Mục vừa tạo (mới sửa trong 1 giờ qua) bị bỏ qua khi hạng mục yêu cầu "đủ cũ"
        cat.excludeNames.clear();
        cat.minAgeSeconds = 3600;
        CHECK(scan(cat).isEmpty());
        cat.minAgeSeconds = 0;
        CHECK(scan(cat).size() == 4);

        // Thư mục gốc rỗng/tương đối/gốc ổ đĩa: máy quét tự bỏ qua, không liệt kê gì
        CleanupCategory bad;
        bad.id = CleanupCategoryId::UserTemp;
        bad.name = "Bad root";
        bad.rootPaths = {QString(), "/Temp", "Windows.old", QDir(tmp.path()).rootPath()};
        CHECK(scan(bad).isEmpty());
        bad.keepRootFolder = false;
        CHECK(scan(bad).isEmpty());
    }

    // ---- Junction/symlink thư mục + hardlink: không đi xuyên, không coi là bản trùng, không xóa ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        QDir root(tmp.path());
        root.mkpath("scan/real");
        root.mkpath("outside");
        writeFile(tmp.path() + "/scan/real/big.bin", 50000);
        writeFile(tmp.path() + "/outside/precious.bin", 70000); // dữ liệu NGOÀI thư mục quét

        const QString junction = tmp.path() + "/scan/jlink";
        const bool haveJunction = makeJunction(junction, tmp.path() + "/outside");
        if (!haveJunction)
            std::printf("LUU Y: khong tao duoc junction (mklink /J) - bo qua cac kiem tra junction\n");
        else
        {
            const FsSafety::RawInfo info = FsSafety::rawInfo(junction);
            CHECK(info.exists && info.isDir && info.isLink);
            CHECK(!FsSafety::rawInfo(tmp.path() + "/scan/real").isLink);

            // walkFiles KHÔNG đi vào junction: chỉ thấy big.bin, không thấy precious.bin
            std::atomic_bool stop{false};
            QStringList seen;
            qint64 total = 0;
            CHECK(FsSafety::walkFiles(tmp.path() + "/scan", stop, [&](const FsSafety::WalkEntry& e) {
                seen << e.path;
                total += e.sizeBytes;
            }));
            CHECK(seen.size() == 1);
            CHECK(total == 50000);

            // CleanupScanner: junction không được liệt kê (không tính kích thước đích, không đưa vào danh sách xóa)
            CleanupCategory cat;
            cat.id = CleanupCategoryId::UserTemp;
            cat.name = "Junction test";
            cat.rootPaths = {tmp.path() + "/scan"};
            QList<CleanupItem> items;
            qint64 finalBytes = -1;
            CleanupScanner scanner;
            scanner.setCategories({cat});
            QObject::connect(&scanner, &CleanupScanner::itemFound, [&](CleanupItem i) { items.push_back(i); });
            QObject::connect(&scanner, &CleanupScanner::scanFinished, [&](qint64 bytes, int) { finalBytes = bytes; });
            scanner.startScan();
            scanner.wait();
            CHECK(items.size() == 1); // chỉ "real"
            CHECK(finalBytes == 50000);
            for (const auto& it : items)
                CHECK(!it.path.endsWith("jlink"));

            // CleanupExecutor: dù bị yêu cầu, junction KHÔNG bị xóa và dữ liệu đích còn nguyên
            CleanupExecutor executor;
            executor.setItems({junction}, {70000});
            bool success = true;
            int count = -1;
            QString note;
            QObject::connect(&executor, &CleanupExecutor::executionFinished, [&](bool ok, QString n, qint64, int cnt) {
                success = ok;
                count = cnt;
                note = n;
            });
            executor.start();
            executor.wait();
            CHECK(!success);
            CHECK(count == 0);
            CHECK(!note.isEmpty());
            CHECK(FsSafety::existsNoFollow(junction));
            CHECK(QFile::exists(tmp.path() + "/outside/precious.bin"));

            // LargeFileScanner: tệp sau junction không được đưa vào danh sách
            LargeFileScanner large;
            large.setRootPath(tmp.path() + "/scan");
            large.setMinSizeBytes(1000);
            QList<LargeFileEntry> largeResults;
            QObject::connect(&large, &LargeFileScanner::scanFinished, [&](QList<LargeFileEntry> r) { largeResults = r; });
            large.startScan();
            large.wait();
            CHECK(largeResults.size() == 1);
            CHECK(largeResults.size() == 1 && largeResults[0].path.endsWith("big.bin"));
            CHECK(largeResults.size() == 1 && largeResults[0].lastModified.isValid());
        }

        // DuplicateFinder: cùng MỘT tệp thấy qua junction hoặc hardlink không phải là "bản trùng"
        QTemporaryDir dupTmp;
        CHECK(dupTmp.isValid());
        QDir(dupTmp.path()).mkpath("a");
        writeFile(dupTmp.path() + "/a/only.bin", 9000);
        const bool hardLinked = CreateHardLinkW(reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(dupTmp.path() + "/a/hard.bin").utf16()),
                                                reinterpret_cast<const wchar_t*>(QDir::toNativeSeparators(dupTmp.path() + "/a/only.bin").utf16()),
                                                nullptr) != 0;
        CHECK(hardLinked);
        const bool dupJunction = makeJunction(dupTmp.path() + "/b", dupTmp.path() + "/a");

        FsSafety::FileIdentity id1, id2;
        CHECK(FsSafety::fileIdentity(dupTmp.path() + "/a/only.bin", &id1));
        if (hardLinked)
        {
            CHECK(FsSafety::fileIdentity(dupTmp.path() + "/a/hard.bin", &id2));
            CHECK(id1 == id2);
        }

        auto findDuplicates = [&]() {
            QList<DuplicateGroup> groups;
            DuplicateFinder finder;
            finder.setRootPath(dupTmp.path());
            finder.setMinSizeBytes(100);
            QObject::connect(&finder, &DuplicateFinder::scanFinished, [&](QList<DuplicateGroup> g, qint64, int) { groups = g; });
            finder.startScan();
            finder.wait();
            return groups;
        };
        // only.bin, hard.bin (hardlink) và b/only.bin, b/hard.bin (qua junction) đều là MỘT tệp -> 0 nhóm
        CHECK(findDuplicates().isEmpty());

        // Thêm một bản sao THẬT -> đúng 1 nhóm, 2 thành viên là 2 tệp vật lý khác nhau
        writeFile(dupTmp.path() + "/a/copy.bin", 9000);
        const auto groups = findDuplicates();
        CHECK(groups.size() == 1);
        if (groups.size() == 1)
        {
            const DuplicateGroup& g = groups[0];
            CHECK(g.paths.size() == 2);
            CHECK(g.lastWriteTimes.size() == g.paths.size());
            CHECK(g.wastedBytes() == 9000);
            int copies = 0;
            for (const QString& path : g.paths)
            {
                if (path.endsWith("copy.bin")) ++copies;
                if (dupJunction)
                    CHECK(!path.contains("/b/")); // không có đường dẫn nào đi qua junction
            }
            CHECK(copies == 1);

            // fileUnchanged: đúng lúc vừa quét; sai sau khi tệp bị sửa hoặc bị xóa
            for (int i = 0; i < g.paths.size(); ++i)
                CHECK(DuplicateFinder::fileUnchanged(g.paths[i], g.sizeEachBytes, g.lastWriteTimes[i]));
            CHECK(!DuplicateFinder::fileUnchanged(g.paths[0], g.sizeEachBytes + 1, g.lastWriteTimes[0]));
            CHECK(!DuplicateFinder::fileUnchanged(g.paths[0], g.sizeEachBytes, g.lastWriteTimes[0] + 10000000));
            {
                QFile f(dupTmp.path() + "/a/copy.bin");
                CHECK(f.open(QIODevice::Append));
                f.write("thay doi");
            }
            for (int i = 0; i < g.paths.size(); ++i)
                if (g.paths[i].endsWith("copy.bin"))
                    CHECK(!DuplicateFinder::fileUnchanged(g.paths[i], g.sizeEachBytes, g.lastWriteTimes[i]));
            CHECK(!DuplicateFinder::fileUnchanged(dupTmp.path() + "/a/khong_ton_tai.bin", 9000, 0));
        }
    }

    // ---- CleanupExecutor: lối tắt hỏng phải được XÓA THẬT, đường dẫn lặp chỉ tính một lần ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString target = tmp.path() + "/target.txt";
        const QString link = tmp.path() + "/broken.lnk";
        writeFile(target, 50);
        const bool linked = QFile::link(target, link);
        CHECK(linked);
        CHECK(QFile::remove(target)); // lối tắt giờ trỏ tới một tệp không còn tồn tại
        if (linked)
        {
            CHECK(FsSafety::existsNoFollow(link)); // chính tệp .lnk vẫn còn đó
            const qint64 linkSize = FsSafety::rawInfo(link).sizeBytes;

            const QString dup = tmp.path() + "/dup_listed.tmp";
            writeFile(dup, 400);

            CleanupExecutor executor;
            // 'dup' xuất hiện 3 lần (2 hạng mục cùng quét một thư mục, khác hoa thường/dấu gạch)
            executor.setItems({link, dup, QString(dup).toUpper(), dup + "/."}, {linkSize, 400, 400, 400});
            bool success = false;
            qint64 freed = -1;
            int count = -1;
            QObject::connect(&executor, &CleanupExecutor::executionFinished, [&](bool ok, QString, qint64 bytes, int cnt) {
                success = ok;
                freed = bytes;
                count = cnt;
            });
            executor.start();
            executor.wait();
            CHECK(success);
            CHECK(count == 2);              // lối tắt + dup (một lần)
            CHECK(freed == linkSize + 400); // không cộng 400 ba lần
            CHECK(!FsSafety::existsNoFollow(link)); // trước đây: báo "đã giải phóng" nhưng tệp .lnk vẫn còn
            CHECK(!FsSafety::existsNoFollow(dup));
        }

        // Đường dẫn rỗng/tương đối/gốc ổ đĩa không bao giờ được chuyển cho Shell để xóa
        CleanupExecutor refuser;
        refuser.setItems({QString(), "Windows.old", QDir(tmp.path()).rootPath()}, {1, 1, 1});
        bool success = true;
        int count = -1;
        QObject::connect(&refuser, &CleanupExecutor::executionFinished, [&](bool ok, QString, qint64, int cnt) {
            success = ok;
            count = cnt;
        });
        refuser.start();
        refuser.wait();
        CHECK(!success);
        CHECK(count == 0);
        CHECK(QDir(QDir(tmp.path()).rootPath()).exists());
    }

    // ---- RecycleBinOps: mục không vào Thùng rác được phải bị nhận ra TRƯỚC khi đưa cho Shell ----
    {
        using namespace RecycleBinOps::internal;
        const qint64 mb = 1024 * 1024;
        CHECK(recycleVerdict(DriveKind::Fixed, false, 8225, 100 * mb).isEmpty());
        CHECK(recycleVerdict(DriveKind::Fixed, false, 8225, 8225 * mb).isEmpty());     // vừa khít vẫn vào được
        CHECK(!recycleVerdict(DriveKind::Fixed, false, 8225, 8225 * mb + 1).isEmpty()); // lớn hơn -> Shell sẽ hủy hẳn
        CHECK(!recycleVerdict(DriveKind::Fixed, true, 8225, 10).isEmpty());             // Thùng rác bị tắt cho ổ này
        CHECK(recycleVerdict(DriveKind::Fixed, false, -1, 500000 * mb).isEmpty());      // không đọc được cấu hình -> không kết luận
        CHECK(!recycleVerdict(DriveKind::Network, false, 8225, 10).isEmpty());
        CHECK(!recycleVerdict(DriveKind::Removable, false, 8225, 10).isEmpty());
        CHECK(!recycleVerdict(DriveKind::Other, false, 8225, 10).isEmpty());

        // Máy thật (chỉ đọc registry + loại ổ): một tệp tạm nhỏ trên ổ cố định phải vào Thùng rác được,
        // còn một "tệp" 100.000 TB chắc chắn lớn hơn mọi Thùng rác nếu đọc được cấu hình.
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString small = tmp.path() + "/small.tmp";
        writeFile(small, 10);
        const QString smallReason = RecycleBinOps::notRecyclableReason(small, 10);
        const QString hugeReason = RecycleBinOps::notRecyclableReason(small, 100000LL * 1024 * 1024 * mb);
        std::printf("RecycleBinOps: tep nho -> '%s'; tep khong lo -> '%s'\n", qPrintable(smallReason), qPrintable(hugeReason));
        CHECK(smallReason.isEmpty());
    }

    // ---- PartitionManager: kẹp kích thước + lớp kiểm tra cuối trước khi resize (THUẦN, không chạy gì) ----
    {
        using namespace PartitionManager;
        const double gb = 1024.0 * 1024.0 * 1024.0;

        SupportedSizeRange range;
        range.ok = true;
        range.minBytes = 40000000000LL;
        range.maxBytes = 99999999488LL;
        // Ô nhập làm tròn 2 chữ số GB: giá trị tối đa hiển thị (93.13) đổi ra byte VƯỢT maxBytes thật
        const double spinMax = 93.14; // làm tròn lên của 99999999488 / GB = 93.1322...
        CHECK(static_cast<qint64>(spinMax * gb) > range.maxBytes);
        CHECK(internal::clampResizeBytes(spinMax, range) == range.maxBytes);
        CHECK(internal::clampResizeBytes(1.0, range) == range.minBytes);
        CHECK(internal::clampResizeBytes(50.0, range) == static_cast<qint64>(50.0 * gb));

        PartitionInfo c;
        c.diskNumber = 0;
        c.partitionNumber = 2;
        c.driveLetter = "C";
        c.sizeBytes = 90000000000LL;
        PartitionInfo d;
        d.diskNumber = 1;
        d.partitionNumber = 1;
        d.driveLetter = "D";
        d.sizeBytes = 500000000000LL;
        const QList<PartitionInfo> list = {c, d};

        // Hợp lệ: đúng phân vùng, đúng kích thước lúc tra, kích thước mới trong khoảng
        CHECK(internal::resizeBlockReason(0, 2, 90000000000LL, list, range, 60000000000LL).isEmpty());
        // Phân vùng không còn (rút ổ/đổi số) -> từ chối
        CHECK(!internal::resizeBlockReason(3, 1, 90000000000LL, list, range, 60000000000LL).isEmpty());
        CHECK(!internal::resizeBlockReason(0, 2, 90000000000LL, {}, range, 60000000000LL).isEmpty());
        CHECK(!internal::resizeBlockReason(-1, -1, 90000000000LL, list, range, 60000000000LL).isEmpty());
        // Số đĩa/phân vùng vẫn có nhưng giờ là phân vùng KHÁC (kích thước khác lúc tra) -> từ chối
        CHECK(!internal::resizeBlockReason(1, 1, 90000000000LL, list, range, 60000000000LL).isEmpty());
        // Kích thước mới ngoài khoảng (theo byte) -> từ chối
        CHECK(!internal::resizeBlockReason(0, 2, 90000000000LL, list, range, range.minBytes - 1).isEmpty());
        CHECK(!internal::resizeBlockReason(0, 2, 90000000000LL, list, range, range.maxBytes + 1).isEmpty());
        CHECK(!internal::resizeBlockReason(0, 2, 90000000000LL, list, range, 0).isEmpty());
        CHECK(internal::resizeBlockReason(0, 2, 90000000000LL, list, range, range.minBytes).isEmpty());
        CHECK(internal::resizeBlockReason(0, 2, 90000000000LL, list, range, range.maxBytes).isEmpty());
        // Không đổi gì -> từ chối
        CHECK(!internal::resizeBlockReason(0, 2, 90000000000LL, list, range, 90000000000LL).isEmpty());
        // Không xác nhận lại được khoảng cho phép -> từ chối
        CHECK(!internal::resizeBlockReason(0, 2, 90000000000LL, list, SupportedSizeRange{}, 60000000000LL).isEmpty());
    }

    // =====================================================================================
    // Hồi quy cho đợt stress test (tests/disk_vpn_stress_tests.cpp): mỗi khối dưới đây là một lỗi THẬT đã
    // tái hiện được bằng chạy thử. Vẫn chỉ đụng tới QTemporaryDir.
    // =====================================================================================
    // Thao tác tệp THÔ qua "\\?\..." - cách duy nhất tạo/kiểm được tên có dấu chấm/khoảng trắng ở cuối.
    auto rawPath = [](const QString& path) { return (QStringLiteral("\\\\?\\") + QDir::toNativeSeparators(path)).toStdWString(); };
    auto rawCreate = [&](const QString& path, const QByteArray& content) {
        const HANDLE h = CreateFileW(rawPath(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false;
        DWORD written = 0;
        WriteFile(h, content.constData(), static_cast<DWORD>(content.size()), &written, nullptr);
        CloseHandle(h);
        return written == static_cast<DWORD>(content.size());
    };
    auto rawExists = [&](const QString& path) { return GetFileAttributesW(rawPath(path).c_str()) != INVALID_FILE_ATTRIBUTES; };

    // ---- Tên có dấu chấm/khoảng trắng cuối: yêu cầu xóa "report." từng làm Shell xóa "report" (tệp KHÁC) ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString dir = tmp.path();
        CHECK(rawCreate(dir + "/report", QByteArray(6000, 'A')));
        CHECK(rawCreate(dir + "/report.", QByteArray(6000, 'B')));
        CHECK(rawCreate(dir + "/copy", QByteArray(6000, 'B'))); // trùng NỘI DUNG với "report."
        CHECK(rawCreate(dir + "/notes", QByteArray("that")));
        CHECK(rawCreate(dir + "/notes ", QByteArray("khoang trang cuoi")));

        // FsSafety nhìn đúng CHÍNH tệp có dấu chấm cuối, và biết nó không an toàn để giao cho Shell
        CHECK(FsSafety::rawInfo(dir + "/report.").exists);
        CHECK(!FsSafety::unsafeShellPathReason(dir + "/report.").isEmpty());
        CHECK(!FsSafety::unsafeShellPathReason(dir + "/notes ").isEmpty());
        CHECK(FsSafety::unsafeShellPathReason(dir + "/report").isEmpty());

        for (bool permanent : {false, true})
        {
            QString err;
            const QStringList request = {dir + "/report.", dir + "/notes "};
            CHECK(!(permanent ? RecycleBinOps::permanentlyDelete(request, &err) : RecycleBinOps::moveToRecycleBin(request, &err)));
            CHECK(!err.isEmpty());
            CHECK(rawExists(dir + "/report")); // tệp "hàng xóm" KHÔNG bị xóa thay
            CHECK(rawExists(dir + "/notes"));
            CHECK(rawExists(dir + "/report."));
            CHECK(rawExists(dir + "/notes "));
        }

        // CleanupExecutor: bị từ chối, báo là mục bị bỏ qua vì lý do an toàn, không tính là đã giải phóng
        CleanupExecutor executor;
        executor.setItems({dir + "/report.", dir + "/notes "}, {6000, 17});
        executor.setPermanentDelete(true);
        bool success = true;
        int count = -1;
        QString note;
        QObject::connect(&executor, &CleanupExecutor::executionFinished, [&](bool ok, QString n, qint64, int c) {
            success = ok;
            note = n;
            count = c;
        });
        executor.start();
        executor.wait();
        CHECK(!success && count == 0);
        CHECK(note.contains(QString::fromUtf8("lý do an toàn")));
        CHECK(rawExists(dir + "/report") && rawExists(dir + "/notes"));

        // DuplicateFinder: "report." không thể được băm đúng tệp qua Win32 (QFile mở nhầm sang "report") nên
        // bị loại - nhóm duy nhất hợp lệ không tồn tại ở đây ("report" khác nội dung với "copy").
        DuplicateFinder finder;
        QList<DuplicateGroup> groups;
        bool finished = false;
        QObject::connect(&finder, &DuplicateFinder::scanFinished, [&](QList<DuplicateGroup> g, qint64, int) {
            groups = g;
            finished = true;
        });
        finder.setRootPath(dir);
        finder.setMinSizeBytes(1);
        finder.startScan();
        finder.wait();
        CHECK(finished);
        CHECK(groups.isEmpty());

        // QTemporaryDir không tự xóa được 2 tên này
        DeleteFileW(rawPath(dir + "/report.").c_str());
        DeleteFileW(rawPath(dir + "/notes ").c_str());
    }

    // ---- Một tệp đang bị khóa/một mục đã mất/một mục lặp không còn chặn các tệp đứng SAU nó ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        QStringList files;
        for (int i = 0; i < 6; ++i)
        {
            files << tmp.path() + QString("/f%1.tmp").arg(i);
            writeFile(files.last(), 10);
        }
        const HANDLE lock = CreateFileW(rawPath(files[1]).c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        CHECK(lock != INVALID_HANDLE_VALUE);
        CHECK(FsSafety::lockedAgainstDelete(files[1]));
        CHECK(!FsSafety::lockedAgainstDelete(files[2]));

        QString err;
        const QStringList request = {files[0], files[1], tmp.path() + "/da_mat.tmp", files[2], files[2].toUpper(),
                                     QDir::toNativeSeparators(files[3]), files[4], files[5]};
        CHECK(!RecycleBinOps::moveToRecycleBin(request, &err)); // có mục không xóa được -> false + lý do
        CHECK(!err.isEmpty());
        CHECK(QFile::exists(files[1]));
        for (int i : {0, 2, 3, 4, 5})
            CHECK(!QFile::exists(files[i]));
        CloseHandle(lock);
        CHECK(!FsSafety::lockedAgainstDelete(files[1]));
        CHECK(RecycleBinOps::moveToRecycleBin(request, &err)); // hết khóa: xóa nốt, mục đã mất được bỏ qua
        CHECK(!QFile::exists(files[1]));
    }

    // ---- Ký tự đại diện: Shell sẽ xóa MỌI tệp khớp mẫu; FindFirstFile trả về một tệp KHÁC khớp mẫu ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        writeFile(tmp.path() + "/a1.tmp", 5);
        writeFile(tmp.path() + "/a2.tmp", 5);
        CHECK(!FsSafety::existsNoFollow(tmp.path() + "/a*.tmp"));
        CHECK(!FsSafety::rawInfo(tmp.path() + "/a?.tmp").exists);
        CHECK(!FsSafety::existsNoFollow(tmp.path() + "/*"));
        QString err;
        CHECK(!RecycleBinOps::permanentlyDelete({tmp.path() + "/a*.tmp", tmp.path() + "/a?.tmp", tmp.path() + "/*"}, &err));
        CHECK(!err.isEmpty());
        CHECK(QFile::exists(tmp.path() + "/a1.tmp") && QFile::exists(tmp.path() + "/a2.tmp"));
    }

    // ---- unsafeCleanupRootReason: các cách viết KHÁC của cùng một thư mục được bảo vệ ----
    {
        const QStringList prot = {"C:/Users/Ai Do", "C:/Program Files", "C:/Windows"};
        // QDir::cleanPath để sót "//" sau khi bỏ "." ("C://.//Windows" -> "C://Windows") - từng lọt qua phép so
        CHECK(!FsSafety::unsafeCleanupRootReason("C://.//Windows", prot).isEmpty());
        CHECK(!FsSafety::unsafeCleanupRootReason("c:\\\\.\\\\Program Files", prot).isEmpty());
        CHECK(!FsSafety::unsafeCleanupRootReason("C:///.///", prot).isEmpty()); // gốc ổ đĩa
        CHECK(!FsSafety::unsafeCleanupRootReason("\\\\?\\C:\\Windows", prot).isEmpty());
        CHECK(!FsSafety::unsafeCleanupRootReason("//?/C:/Users/Ai Do", prot).isEmpty());
        CHECK(!FsSafety::unsafeCleanupRootReason("\\\\.\\C:\\Windows\\Temp", prot).isEmpty()); // tiền tố thiết bị
        CHECK(!FsSafety::unsafeCleanupRootReason("C:/Windows.", prot).isEmpty());             // Win32 bỏ dấu chấm cuối
        CHECK(!FsSafety::unsafeCleanupRootReason("C:/Users./Ai Do", prot).isEmpty());
        CHECK(!FsSafety::unsafeCleanupRootReason("\\\\localhost\\C$\\Windows", prot).isEmpty()); // chia sẻ quản trị
        CHECK(!FsSafety::unsafeCleanupRootReason("\\\\127.0.0.1\\c$", prot).isEmpty());
        CHECK(!FsSafety::unsafeCleanupRootReason("\\\\may\\ADMIN$\\Temp", prot).isEmpty());
        CHECK(!FsSafety::unsafeCleanupRootReason("C:/Temp/*", prot).isEmpty());                // ký tự đại diện
        CHECK(!FsSafety::unsafeCleanupRootReason("C:/Windows::$DATA", prot).isEmpty());        // luồng dữ liệu phụ
        CHECK(!FsSafety::unsafeCleanupRootReason(QString("C:/Temp") + QChar(0) + "x", prot).isEmpty());
        // ...mà không chặn nhầm thư mục con hợp lệ viết theo cùng các kiểu đó
        CHECK(FsSafety::unsafeCleanupRootReason("C://.//Windows//Temp", prot).isEmpty());
        CHECK(FsSafety::unsafeCleanupRootReason("\\\\?\\C:\\Windows\\Temp", prot).isEmpty());
        CHECK(FsSafety::unsafeCleanupRootReason("\\\\localhost\\C$\\Windows\\Temp", prot).isEmpty());
        CHECK(FsSafety::unsafeCleanupRootReason("C:/Windows.old", prot).isEmpty());

        // Tên ngắn 8.3 của một thư mục được bảo vệ THẬT (TEMP của Windows rất hay ở dạng tên ngắn)
        const QString programFiles = QDir::fromNativeSeparators(qEnvironmentVariable("ProgramFiles"));
        wchar_t shortBuffer[MAX_PATH] = {};
        const DWORD shortLen = GetShortPathNameW(QDir::toNativeSeparators(programFiles).toStdWString().c_str(), shortBuffer, MAX_PATH);
        const QString shortName = QDir::fromNativeSeparators(QString::fromWCharArray(shortBuffer, static_cast<int>(shortLen < MAX_PATH ? shortLen : 0)));
        if (!shortName.isEmpty() && shortName.compare(programFiles, Qt::CaseInsensitive) != 0)
        {
            CHECK(!FsSafety::unsafeCleanupRootReason(shortName, {programFiles}).isEmpty());
            CHECK(!FsSafety::unsafeCleanupRootReason(programFiles, {shortName}).isEmpty());
        }
        else
        {
            std::printf("LUU Y: o dia khong co ten ngan 8.3 cho Program Files - bo qua kiem tra ten ngan\n");
        }
    }

    // ---- CategoryRegistry: thư mục gốc để ghép là GỐC Ổ ĐĨA -> không sinh "C:/Temp", "C:/Logs"... ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        CleanupEnvironment env;
        env.windowsDir = QDir(tmp.path()).rootPath(); // "C:/"
        env.localAppData = QDir(tmp.path()).rootPath();
        env.roamingAppData = QDir(tmp.path()).rootPath();
        for (const CleanupCategory& c : CategoryRegistry::buildCategories(env))
            CHECK(c.rootPaths.isEmpty());
        // Môi trường thật: thư mục Windows lấy từ API của Windows, không phụ thuộc biến môi trường WINDIR
        CHECK(CleanupEnvironment::current().windowsDir.compare(FsSafety::windowsDirectory(), Qt::CaseInsensitive) == 0);
    }

    // ---- PartitionManager: giá trị không phải số/tràn số/kích thước không đọc được ----
    {
        using namespace PartitionManager;
        SupportedSizeRange range;
        range.ok = true;
        range.minBytes = 40000000000LL;
        range.maxBytes = 99999999488LL;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        // Ép double -> qint64 ngoài khoảng là hành vi không xác định (thực tế ra INT64_MIN -> bị kẹp về MIN:
        // "vô cực GB" từng thành lệnh THU NHỎ tối đa)
        CHECK(internal::clampResizeBytes(inf, range) == range.maxBytes);
        CHECK(internal::clampResizeBytes(1e300, range) == range.maxBytes);
        CHECK(internal::clampResizeBytes(-inf, range) == range.minBytes);
        CHECK(internal::clampResizeBytes(nan, range) == 0);
        CHECK(internal::clampResizeBytes(50.0, SupportedSizeRange{}) == 0); // khoảng chưa tra -> 0, luôn bị từ chối

        QString err;
        const auto huge = internal::parsePartitionsJson(R"({"DiskNumber":0,"PartitionNumber":1,"Size":1e300})", &err);
        CHECK(huge.size() == 1 && huge[0].sizeBytes == std::numeric_limits<qint64>::max());
        CHECK(!internal::parseSupportedSizeJson(R"({"SizeMin":1e300,"SizeMax":1000})", &err).ok);
        CHECK(!internal::parseSupportedSizeJson(R"({"SizeMin":-5,"SizeMax":1000})", &err).ok);

        // JSON thiếu trường Size -> kích thước 0; "0 lúc tra == 0 hiện tại" KHÔNG phải là đã đối chiếu
        const auto noSize = internal::parsePartitionsJson(R"({"DiskNumber":2,"PartitionNumber":1})", &err);
        CHECK(noSize.size() == 1 && noSize[0].sizeBytes == 0);
        CHECK(!internal::resizeBlockReason(2, 1, 0, noSize, range, range.minBytes).isEmpty());
        SupportedSizeRange negativeMin = range;
        negativeMin.minBytes = -1;
        PartitionInfo p;
        p.diskNumber = 0;
        p.partitionNumber = 2;
        p.sizeBytes = 90000000000LL;
        CHECK(!internal::resizeBlockReason(0, 2, 90000000000LL, {p}, negativeMin, 60000000000LL).isEmpty());

        // Ô nhập 2 chữ số thập phân CHƯA được sửa: 500.107.862.016 byte hiện là 465,76 GB; đổi ngược ra byte
        // lệch ~1,7 MB - từng thành một lệnh Resize-Partition thật dù người dùng không nhập gì.
        const double gb = 1024.0 * 1024.0 * 1024.0;
        CHECK(static_cast<qint64>(465.76 * gb) != 500107862016LL);
        CHECK(internal::isSameSizeAtInputPrecision(465.76, 500107862016LL));
        CHECK(!internal::isSameSizeAtInputPrecision(465.77, 500107862016LL));
        CHECK(!internal::isSameSizeAtInputPrecision(465.75, 500107862016LL));
        CHECK(!internal::isSameSizeAtInputPrecision(nan, 500107862016LL));
    }

    // ---- Sơ đồ đĩa kiểu Disk Management (DiskLayout + đọc Offset/Get-Disk): THUẦN, không chạy PowerShell/không đụng đĩa ----
    {
        using namespace PartitionManager;
        constexpr qint64 MiB = 1024LL * 1024, GiB = 1024 * MiB;
        auto part = [](int num, const char* type, qint64 offsetMiB, qint64 sizeMiB, const char* letter = "", const char* fs = "NTFS") {
            PartitionInfo p;
            p.diskNumber = 0;
            p.partitionNumber = num;
            p.type = type;
            p.driveLetter = letter;
            p.fileSystem = fs;
            p.offsetBytes = offsetMiB * MiB;
            p.sizeBytes = sizeMiB * MiB;
            return p;
        };
        DiskInfo disk0;
        disk0.number = 0;
        disk0.sizeBytes = 256060514304LL;
        disk0.partitionStyle = "GPT";
        // Bố cục THẬT của máy dựng ra ảnh Disk Management: EFI, MSR, C:, D:, ~67 GB chưa phân bổ, Recovery.
        const QList<PartitionInfo> parts = {part(1, "System", 1, 100, "", "FAT32"), part(2, "Reserved", 101, 16, "", ""),
                                            part(3, "Basic", 117, 123557, "C"), part(4, "Basic", 123675, 51394, "D"),
                                            part(5, "Recovery", 243675, 522)};
        const auto rows = DiskLayout::build({disk0}, parts);
        CHECK(rows.size() == 1 && !rows[0].approximate);
        const auto& segs = rows[0].segments;
        CHECK(segs.size() == 6); // khe 1 MiB giữa C: và D: và 1 MiB cuối đĩa nhỏ hơn kMinGapBytes nên KHÔNG thành vùng chưa phân bổ
        CHECK(segs.size() == 6 && segs[4].kind == DiskLayout::SegmentKind::Unallocated);
        CHECK(segs.size() == 6 && segs[4].offsetBytes == (123675 + 51394) * MiB && segs[4].sizeBytes == (243675 - 123675 - 51394) * MiB);
        CHECK(segs.size() == 6 && segs[3].kind == DiskLayout::SegmentKind::Partition && segs[3].partitionIndex == 3); // D: = phần tử số 3 của danh sách
        CHECK(DiskLayout::freeBytesAfter(segs, 3) == (243675 - 123675 - 51394) * MiB); // D: có vùng trống liền kề BÊN PHẢI
        CHECK(DiskLayout::freeBytesAfter(segs, 2) == 0);                                // C: sát D: -> không mở rộng được
        CHECK(DiskLayout::freeBytesBefore(segs, 4) == DiskLayout::freeBytesAfter(segs, 3)); // Recovery: vùng trống nằm bên TRÁI nó
        CHECK(DiskLayout::freeBytesBefore(segs, 3) == 0 && DiskLayout::freeBytesAfter(segs, 99) == 0);

        qint64 total = 0;
        for (const auto& s : segs) total += s.sizeBytes;

        // Thu nhỏ D: 20 GiB -> phần giải phóng NỐI VÀO vùng chưa phân bổ ngay sau (mép phải dịch sang trái, mép trái giữ nguyên)
        {
            const auto after = DiskLayout::projectResize(segs, 3, segs[3].sizeBytes - 20 * GiB);
            CHECK(after.size() == 6);
            CHECK(after[3].offsetBytes == segs[3].offsetBytes); // mép TRÁI đứng yên
            CHECK(after[3].sizeBytes == segs[3].sizeBytes - 20 * GiB);
            CHECK(after[4].kind == DiskLayout::SegmentKind::Unallocated && after[4].sizeBytes == segs[4].sizeBytes + 20 * GiB);
            CHECK(after[4].offsetBytes == segs[4].offsetBytes - 20 * GiB); // vùng trống mở rộng NGƯỢC về phía trái, vẫn liền kề D:
            qint64 t2 = 0;
            for (const auto& s : after) t2 += s.sizeBytes;
            CHECK(t2 == total); // tổng dung lượng bảo toàn
        }
        // Mở rộng D: 30 GiB (< vùng trống) -> lấy từ vùng chưa phân bổ liền kề bên phải
        {
            const auto after = DiskLayout::projectResize(segs, 3, segs[3].sizeBytes + 30 * GiB);
            CHECK(after.size() == 6 && after[3].sizeBytes == segs[3].sizeBytes + 30 * GiB);
            CHECK(after[4].sizeBytes == segs[4].sizeBytes - 30 * GiB && after[4].offsetBytes == segs[4].offsetBytes + 30 * GiB);
        }
        // Mở rộng quá vùng trống -> KẸP bằng đúng vùng trống, vùng đó biến mất, KHÔNG nuốt Recovery
        {
            const auto after = DiskLayout::projectResize(segs, 3, segs[3].sizeBytes + 500 * GiB);
            CHECK(after.size() == 5 && after[3].sizeBytes == segs[3].sizeBytes + segs[4].sizeBytes);
            CHECK(after[4].kind == DiskLayout::SegmentKind::Partition && after[4].sizeBytes == 522 * MiB);
        }
        // C: không có vùng trống liền kề -> mở rộng không đổi được gì; kích thước bằng nhau -> không đổi
        CHECK(DiskLayout::projectResize(segs, 2, segs[2].sizeBytes + GiB)[2].sizeBytes == segs[2].sizeBytes);
        CHECK(DiskLayout::projectResize(segs, 3, segs[3].sizeBytes).size() == segs.size());
        CHECK(DiskLayout::projectResize(segs, 99, GiB).size() == segs.size()); // chỉ số lạ -> nguyên trạng
        // Thu nhỏ phân vùng KHÔNG có vùng trống sau nó (Recovery, cuối đĩa) -> tạo mới một vùng chưa phân bổ phía sau
        {
            const auto after = DiskLayout::projectResize(segs, 4, 400 * MiB);
            CHECK(after.size() == 7 && after[5].sizeBytes == 400 * MiB && after[6].kind == DiskLayout::SegmentKind::Unallocated);
            CHECK(after.size() == 7 && after[6].sizeBytes == 122 * MiB && after[6].offsetBytes == 243675 * MiB + 400 * MiB);
        }

        // Thiếu Offset -> xếp tuần tự theo số phân vùng, báo gần đúng, KHÔNG bịa vùng chưa phân bổ
        {
            QList<PartitionInfo> noOffset = {part(2, "Basic", 0, 500, "D"), part(1, "Basic", 0, 300, "C")};
            for (auto& p : noOffset) p.offsetBytes = -1;
            const auto r = DiskLayout::build({}, noOffset); // cũng không có thông tin đĩa (Get-Disk lỗi)
            CHECK(r.size() == 1 && r[0].approximate && r[0].segments.size() == 2);
            CHECK(r.size() == 1 && r[0].segments.size() == 2 && r[0].segments[0].partitionIndex == 1 && r[0].segments[1].offsetBytes == 300 * MiB);
            CHECK(r.size() == 1 && r[0].disk.sizeBytes == 800 * MiB); // dung lượng đĩa suy từ phân vùng
        }
        // Đĩa trống/chưa khởi tạo -> một vùng chưa phân bổ duy nhất; phân vùng chồng lấn không tạo khoảng âm
        {
            DiskInfo blank;
            blank.number = 3;
            blank.sizeBytes = 1000 * GiB;
            blank.partitionStyle = "RAW";
            const auto r = DiskLayout::build({blank}, {});
            CHECK(r.size() == 1 && r[0].segments.size() == 1 && r[0].segments[0].kind == DiskLayout::SegmentKind::Unallocated);
            CHECK(r.size() == 1 && r[0].segments.size() == 1 && r[0].segments[0].sizeBytes == 1000 * GiB);

            const auto overlap = DiskLayout::build({}, {part(1, "Basic", 1, 100, "C"), part(2, "Basic", 50, 100, "D")});
            CHECK(overlap.size() == 1 && overlap[0].segments.size() == 2 && overlap[0].segments[1].offsetBytes == 101 * MiB);
        }
        // Hai đĩa -> hai hàng theo thứ tự số đĩa
        {
            DiskInfo d1; d1.number = 1; d1.sizeBytes = 100 * GiB;
            PartitionInfo usb = part(1, "Basic", 1, 1000, "E");
            usb.diskNumber = 1;
            const auto r = DiskLayout::build({d1, disk0}, parts + QList<PartitionInfo>{usb});
            CHECK(r.size() == 2 && r[0].disk.number == 0 && r[1].disk.number == 1);
            CHECK(r.size() == 2 && r[1].segments.size() == 2 && r[1].segments[1].kind == DiskLayout::SegmentKind::Unallocated);
        }

        // Độ rộng khối: tổng chính xác, khối nhỏ (EFI 100 MB) vẫn thấy được, tỉ lệ đúng với khối lớn
        {
            QList<qint64> sizes;
            for (const auto& s : segs) sizes << s.sizeBytes;
            for (const int totalPx : {420, 700, 900, 1500})
            {
                const auto w = DiskLayout::computeBlockWidths(sizes, totalPx, 36);
                int sum = 0;
                bool allMin = true;
                for (const int v : w) { sum += v; allMin = allMin && v >= 36; }
                CHECK(sum == totalPx && w.size() == sizes.size() && allMin);
            }
            const auto w = DiskLayout::computeBlockWidths(sizes, 900, 36);
            CHECK(w[0] == 36 && w[1] == 36 && w[5] == 36);          // EFI, MSR, Recovery được nâng lên tối thiểu
            CHECK(w[2] > w[4] && w[4] > w[3]);                       // C: (120 GB) > vùng chưa phân bổ (67 GB) > D: (50 GB)
            const double bpp = DiskLayout::bytesPerPixel(sizes, w, 36);
            CHECK(bpp > 0.0 && std::abs(bpp * w[3] - double(sizes[3])) < double(sizes[3]) * 0.02); // đổi pixel -> byte ở khối lớn sai < 2%
            CHECK(DiskLayout::computeBlockWidths({}, 900, 36).isEmpty());
            CHECK(DiskLayout::computeBlockWidths({5 * GiB}, 300, 36) == QList<int>{300});
            const auto tooNarrow = DiskLayout::computeBlockWidths({GiB, GiB, GiB}, 60, 36); // không đủ chỗ cho 3 khối tối thiểu
            CHECK(tooNarrow.size() == 3 && tooNarrow[0] + tooNarrow[1] + tooNarrow[2] == 60);
            const auto zeroSizes = DiskLayout::computeBlockWidths({0, 0}, 100, 36);
            CHECK(zeroSizes.size() == 2 && zeroSizes[0] + zeroSizes[1] == 100);
        }

        // Hướng thay đổi: khoảng kéo được = giao của khoảng Windows cho phép và hướng đã khóa
        {
            SupportedSizeRange range;
            range.ok = true;
            range.minBytes = qint64(45.29 * GiB);
            range.maxBytes = qint64(117.19 * GiB);
            const qint64 cur = qint64(50.19 * GiB);
            using DiskLayout::ResizeDirection;
            auto both = DiskLayout::boundsForDirection(cur, range, ResizeDirection::Free);
            auto shrink = DiskLayout::boundsForDirection(cur, range, ResizeDirection::ShrinkOnly);
            auto grow = DiskLayout::boundsForDirection(cur, range, ResizeDirection::GrowOnly);
            CHECK(both.minBytes == range.minBytes && both.maxBytes == range.maxBytes);
            CHECK(shrink.minBytes == range.minBytes && shrink.maxBytes == cur); // kéo TRÁI: không vượt quá hiện tại
            CHECK(grow.minBytes == cur && grow.maxBytes == range.maxBytes);     // kéo PHẢI: không nhỏ hơn hiện tại
            CHECK(DiskLayout::boundsForDirection(cur, SupportedSizeRange{}, ResizeDirection::Free).maxBytes == 0);
            CHECK(DiskLayout::boundsForDirection(10 * GiB, range, ResizeDirection::GrowOnly).minBytes == range.minBytes); // hiện tại ngoài khoảng -> kẹp
            CHECK(DiskLayout::boundsForDirection(999 * GiB, range, ResizeDirection::ShrinkOnly).maxBytes == range.maxBytes);

            const double nan = std::numeric_limits<double>::quiet_NaN();
            CHECK(DiskLayout::directionBlockReason(80.0, cur, ResizeDirection::Free).isEmpty());
            CHECK(!DiskLayout::directionBlockReason(80.0, cur, ResizeDirection::ShrinkOnly).isEmpty()); // lớn hơn mà chỉ cho thu nhỏ
            CHECK(DiskLayout::directionBlockReason(46.0, cur, ResizeDirection::ShrinkOnly).isEmpty());
            CHECK(!DiskLayout::directionBlockReason(46.0, cur, ResizeDirection::GrowOnly).isEmpty());   // nhỏ hơn mà chỉ cho mở rộng
            CHECK(DiskLayout::directionBlockReason(80.0, cur, ResizeDirection::GrowOnly).isEmpty());
            CHECK(DiskLayout::directionBlockReason(50.19, cur, ResizeDirection::ShrinkOnly).isEmpty()); // chưa đổi (đúng độ chính xác ô nhập)
            CHECK(DiskLayout::directionBlockReason(50.19, cur, ResizeDirection::GrowOnly).isEmpty());
            CHECK(!DiskLayout::directionBlockReason(nan, cur, ResizeDirection::ShrinkOnly).isEmpty());
            CHECK(!DiskLayout::directionBlockReason(std::numeric_limits<double>::infinity(), cur, ResizeDirection::GrowOnly).isEmpty());
            CHECK(DiskLayout::directionName(ResizeDirection::ShrinkOnly).contains("trái"));
            CHECK(DiskLayout::directionName(ResizeDirection::GrowOnly).contains("phải"));
        }

        // Mô tả thay đổi: nói rõ thu nhỏ/mở rộng, phía giải phóng/lấy thêm, và cảnh báo khi không có vùng trống bên phải
        {
            const qint64 cur = 50 * GiB;
            const QString shrinkText = DiskLayout::describeResize("D:", cur, 30 * GiB, 67 * GiB, 0);
            CHECK(shrinkText.contains("THU NHỎ") && shrinkText.contains("BÊN PHẢI") && shrinkText.contains("TRÁI giữ nguyên"));
            CHECK(shrinkText.contains("20.00 GB"));
            const QString growText = DiskLayout::describeResize("D:", cur, 70 * GiB, 67 * GiB, 0);
            CHECK(growText.contains("MỞ RỘNG") && growText.contains("LIỀN KỀ BÊN PHẢI") && !growText.contains("⚠"));
            const QString noFree = DiskLayout::describeResize("C:", cur, 60 * GiB, 0, 10 * GiB);
            CHECK(noFree.contains("⚠") && noFree.contains("KHÔNG có vùng chưa phân bổ liền kề bên phải") && noFree.contains("BÊN TRÁI"));
            const QString tooMuch = DiskLayout::describeResize("D:", cur, 150 * GiB, 20 * GiB, 0);
            CHECK(tooMuch.contains("Vượt quá"));
            CHECK(DiskLayout::describeResize("D:", cur, cur + 1000, 5 * GiB, 0).contains("Chưa thay đổi")); // < 1 MiB = coi như chưa đổi
            CHECK(DiskLayout::formatSize(100 * MiB) == "100.0 MB" && DiskLayout::formatSize(3 * GiB / 2) == "1.50 GB");
        }

        // Đọc JSON: Offset (thiếu -> -1) và Get-Disk (mảng / object đơn / rỗng / hỏng)
        {
            QString err;
            const auto withOffset = internal::parsePartitionsJson(R"([{"DiskNumber":0,"PartitionNumber":1,"Size":104857600,"Offset":1048576},{"DiskNumber":0,"PartitionNumber":2,"Size":5}])", &err);
            CHECK(withOffset.size() == 2 && withOffset[0].offsetBytes == 1048576 && withOffset[1].offsetBytes == -1);
            const auto disks = internal::parseDisksJson(R"([{"Number":0,"FriendlyName":"SAMSUNG","Size":256060514304,"PartitionStyle":"GPT","IsOffline":false,"IsReadOnly":false},{"Number":1,"FriendlyName":"USB","Size":8000000000,"PartitionStyle":"RAW","IsOffline":true,"IsReadOnly":true}])", &err);
            CHECK(disks.size() == 2 && disks[0].number == 0 && disks[0].sizeBytes == 256060514304LL && disks[0].partitionStyle == "GPT");
            CHECK(disks.size() == 2 && disks[1].isOffline && disks[1].isReadOnly && disks[1].partitionStyle == "RAW");
            const auto single = internal::parseDisksJson(R"({"Number":4,"FriendlyName":"X","Size":10,"PartitionStyle":"MBR"})", &err);
            CHECK(single.size() == 1 && single[0].number == 4 && single[0].partitionStyle == "MBR");
            QString emptyErr, badErr;
            CHECK(internal::parseDisksJson("", &emptyErr).isEmpty() && emptyErr.isEmpty());
            CHECK(internal::parseDisksJson("{khong phai json", &badErr).isEmpty() && !badErr.isEmpty());
            CHECK(internal::parseDisksJson(R"({"Number":0,"Size":1e300})", &err)[0].sizeBytes == std::numeric_limits<qint64>::max()); // tràn số bị bão hòa
            // Kịch bản đọc đĩa chỉ ĐỌC: không có lệnh nào đổi đĩa
            const QString diskScript = internal::buildListDisksScript();
            CHECK(diskScript.contains("Get-Disk") && !diskScript.contains("Resize-Partition") && !diskScript.contains("Set-") &&
                  !diskScript.contains("Remove-") && !diskScript.contains("Clear-") && !diskScript.contains("New-Partition"));
            CHECK(internal::buildListPartitionsScript().contains("Offset"));
        }
    }

    // ---- Hồi quy: junction trỏ vào thư mục ĐƯỢC BẢO VỆ phải bị nhận ra theo danh tính vật lý ----
    // Trước đây unsafeCleanupRootReason chỉ so CHUỖI: "…/rac/toi-baove" trông chẳng liên quan gì tới thư mục
    // được bảo vệ nên được nhận làm thư mục rác. "Thư mục được bảo vệ" ở đây là GIẢ LẬP, nằm trong thư mục tạm.
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        QDir root(tmp.path());
        root.mkpath("baove/trong/chau");
        root.mkpath("khac");
        root.mkpath("rac");
        writeFile(tmp.path() + "/baove/trong/quy.bin", 1234);
        const QString prot = tmp.path() + "/baove/trong";
        const QString jProt = tmp.path() + "/rac/toi-baove";  // -> chính thư mục được bảo vệ
        const QString jParent = tmp.path() + "/rac/toi-cha";  // -> thư mục CHA của nó (chứa thư mục được bảo vệ)
        const QString jChild = tmp.path() + "/rac/toi-chau";  // -> thư mục CON của nó (hợp lệ, như C:/Windows/Temp)
        const QString jOther = tmp.path() + "/rac/toi-khac";  // -> thư mục không liên quan
        const bool made = makeJunction(jProt, prot) && makeJunction(jParent, tmp.path() + "/baove") &&
                          makeJunction(jChild, prot + "/chau") && makeJunction(jOther, tmp.path() + "/khac");
        if (!made)
            std::printf("LUU Y: khong tao duoc junction (mklink /J) - bo qua kiem tra junction toi thu muc duoc bao ve\n");
        else
        {
            CHECK(!FsSafety::unsafeCleanupRootReason(jProt, {prot}).isEmpty());
            CHECK(!FsSafety::unsafeCleanupRootReason(jParent, {prot}).isEmpty());
            CHECK(FsSafety::unsafeCleanupRootReason(jChild, {prot}).isEmpty());
            CHECK(FsSafety::unsafeCleanupRootReason(jOther, {prot}).isEmpty());
            CHECK(FsSafety::unsafeCleanupRootReason(tmp.path() + "/rac", {prot}).isEmpty()); // thư mục thường
            CHECK(FsSafety::unsafeCleanupRootReason(jProt, {}).isEmpty()); // không có gì được bảo vệ để trùng
            // Đường dẫn đi XUYÊN qua một liên kết: "<junction tới cha>/trong" chính là thư mục được bảo vệ
            // (không đặt tên thư mục là "con": đó là tên thiết bị dành riêng của Windows, không tạo được)
            CHECK(!FsSafety::unsafeCleanupRootReason(jParent + "/trong", {prot}).isEmpty());
            CHECK(FsSafety::unsafeCleanupRootReason(jParent + "/trong/chau", {prot}).isEmpty());
            std::printf("junction -> thu muc duoc bao ve: %s\n", qPrintable(FsSafety::unsafeCleanupRootReason(jProt, {prot})));

            // CategoryRegistry: TEMP là junction tới thư mục được bảo vệ -> hạng mục "Tệp tạm" không khả dụng
            auto userTempRoots = [](const CleanupEnvironment& env) {
                for (const CleanupCategory& c : CategoryRegistry::buildCategories(env))
                    if (c.id == CleanupCategoryId::UserTemp)
                        return c.rootPaths;
                return QStringList{"<khong co hang muc>"};
            };
            CleanupEnvironment env;
            env.protectedDirs = {prot};
            env.tempDir = jProt;
            CHECK(userTempRoots(env).isEmpty());
            env.tempDir = jOther;
            CHECK(userTempRoots(env).size() == 1);
        }
        // Gỡ CHÍNH các junction (rmdir chỉ gỡ liên kết, không đi vào đích) trước khi thư mục tạm bị dọn.
        for (const QString& j : {jProt, jParent, jChild, jOther})
            if (FsSafety::existsNoFollow(j))
                CHECK(QDir().rmdir(j) && !FsSafety::existsNoFollow(j));
        CHECK(QFile::exists(tmp.path() + "/baove/trong/quy.bin")); // đích còn nguyên
        CHECK(QDir(prot + "/chau").exists());

        // Phép nhận "đích là GỐC Ổ ĐĨA" (dùng cho junction/điểm gắn trỏ vào gốc ổ) - kiểm trên đường dẫn thật,
        // KHÔNG tạo junction nào trỏ ra ngoài thư mục tạm.
        CHECK(FsSafety::resolvesToVolumeRoot(QDir(tmp.path()).rootPath()));
        CHECK(!FsSafety::resolvesToVolumeRoot(tmp.path()));
        CHECK(!FsSafety::resolvesToVolumeRoot(tmp.path() + "/baove/trong/quy.bin"));
        CHECK(!FsSafety::resolvesToVolumeRoot(tmp.path() + "/khong-ton-tai"));
    }

    // ---- Hồi quy: tuổi tệp tạm được kiểm LẠI ngay trước khi xóa, không chỉ lúc quét ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString fresh = tmp.path() + "/vua-sua.tmp";       // được ghi SAU lúc quét (thời điểm sửa = bây giờ)
        const QString old = tmp.path() + "/cu.tmp";              // sửa lần cuối 1 giờ trước
        const QString freshDir = tmp.path() + "/thu-muc-dang-dung";
        const QString noLimit = tmp.path() + "/khong-gioi-han.log"; // hạng mục không đặt tuổi tối thiểu
        writeFile(fresh, 10);
        writeFile(old, 20);
        QDir(tmp.path()).mkpath("thu-muc-dang-dung");
        writeFile(freshDir + "/dang-ghi.dat", 30);
        writeFile(noLimit, 40);
        {
            QFile f(old);
            CHECK(f.open(QIODevice::ReadWrite));
            CHECK(f.setFileTime(QDateTime::currentDateTime().addSecs(-3600), QFileDevice::FileModificationTime));
        }

        CleanupExecutor executor;
        executor.setItems({fresh, old, freshDir, noLimit}, {10, 20, 30, 40}, {300, 300, 300, 0});
        executor.setPermanentDelete(true); // tệp của chính test trong thư mục tạm - không đưa rác vào Thùng rác
        bool success = false;
        qint64 freed = -1;
        int count = -1;
        QString note;
        QObject::connect(&executor, &CleanupExecutor::executionFinished, [&](bool ok, QString n, qint64 bytes, int cnt) {
            success = ok;
            note = n;
            freed = bytes;
            count = cnt;
        });
        executor.start();
        executor.wait();
        CHECK(success);
        CHECK(count == 2);
        CHECK(freed == 60);
        CHECK(note.contains(QString::fromUtf8("2 mục được GIỮ LẠI")));
        CHECK(FsSafety::existsNoFollow(fresh));
        CHECK(FsSafety::existsNoFollow(freshDir + "/dang-ghi.dat"));
        CHECK(!FsSafety::existsNoFollow(old));
        CHECK(!FsSafety::existsNoFollow(noLimit));

        // Không truyền tuổi tối thiểu (mọi nơi gọi cũ): hành vi như trước - xóa cả mục vừa sửa.
        CleanupExecutor plain;
        plain.setItems({fresh}, {10});
        plain.setPermanentDelete(true);
        plain.start();
        plain.wait();
        CHECK(!FsSafety::existsNoFollow(fresh));
    }

    // =====================================================================================
    // FolderTreeScanner: cây thư mục + dung lượng từng nhánh (tab "Cây thư mục"). Mọi thứ chỉ đụng tới QTemporaryDir.
    // =====================================================================================

    // ---- FolderTreeInternal::allocatedBytes / needsExactSize (thuần) ----
    {
        using namespace FolderTreeInternal;
        const qint64 cl = 4096;
        CHECK(allocatedBytes(FILE_ATTRIBUTE_ARCHIVE, 0, cl, -1, 600) == 0);               // tệp rỗng
        CHECK(allocatedBytes(FILE_ATTRIBUTE_ARCHIVE, 1, cl, -1, 600) == 8);               // nằm gọn trong MFT: làm tròn lên 8 byte
        CHECK(allocatedBytes(FILE_ATTRIBUTE_ARCHIVE, 100, cl, -1, 600) == 104);
        CHECK(allocatedBytes(FILE_ATTRIBUTE_ARCHIVE, 600, cl, -1, 600) == 600);           // đúng ngưỡng
        CHECK(allocatedBytes(FILE_ATTRIBUTE_ARCHIVE, 601, cl, -1, 600) == 4096);          // vượt ngưỡng -> 1 cụm
        CHECK(allocatedBytes(FILE_ATTRIBUTE_ARCHIVE, 4096, cl, -1, 600) == 4096);
        CHECK(allocatedBytes(FILE_ATTRIBUTE_ARCHIVE, 4097, cl, -1, 600) == 8192);
        // Giới hạn "resident" theo độ dài tên: tên dài hơn -> ngưỡng thấp hơn (mỗi ký tự lấy 2 byte của bản ghi MFT).
        CHECK(residentLimitFor(0) == kResidentBaseBytes && residentLimitFor(10) == kResidentBaseBytes - 20);
        CHECK(residentLimitFor(1000) == 0 && residentLimitFor(341) == 1 && residentLimitFor(342) == 0); // không bao giờ âm
        CHECK(allocatedBytes(FILE_ATTRIBUTE_ARCHIVE, 100, cl, -1, 0) == 4096);            // FAT/exFAT: không có tệp "resident"
        CHECK(allocatedBytes(FILE_ATTRIBUTE_ARCHIVE, 100, 0, -1, 0) == 100);              // không biết cụm -> kích thước thật
        CHECK(allocatedBytes(FILE_ATTRIBUTE_ARCHIVE, 5000000, 65536, -1, 0) == 5046272);  // cụm lớn (exFAT 64 KiB)
        // Tệp chỉ có trên đám mây (OneDrive "Files On-Demand"): không chiếm chỗ trên đĩa dù kích thước thật rất lớn.
        CHECK(allocatedBytes(FILE_ATTRIBUTE_OFFLINE, 9000000, cl, -1, 600) == 0);
        CHECK(allocatedBytes(0x00400000 /*RECALL_ON_DATA_ACCESS*/, 9000000, cl, -1, 600) == 0);
        CHECK(allocatedBytes(0x00040000 /*RECALL_ON_OPEN*/, 9000000, cl, -1, 600) == 0);
        // Tệp nén/thưa: dùng kích thước thật từ GetCompressedFileSizeW khi có.
        CHECK(allocatedBytes(FILE_ATTRIBUTE_SPARSE_FILE, 50000000, cl, 8192, 600) == 8192);
        CHECK(allocatedBytes(FILE_ATTRIBUTE_COMPRESSED, 1000000, cl, 400000, 600) == 400000);
        CHECK(allocatedBytes(FILE_ATTRIBUTE_SPARSE_FILE, 50000000, cl, -1, 600) == 50003968); // không hỏi được -> làm tròn
        CHECK(needsExactSize(FILE_ATTRIBUTE_SPARSE_FILE, 0));
        CHECK(needsExactSize(FILE_ATTRIBUTE_COMPRESSED, 0));
        CHECK(needsExactSize(FILE_ATTRIBUTE_REPARSE_POINT, 0x80000017)); // WOF (CompactOS)
        CHECK(!needsExactSize(FILE_ATTRIBUTE_REPARSE_POINT, 0x8000001B)); // APPEXECLINK: tệp 0 byte
        CHECK(!needsExactSize(FILE_ATTRIBUTE_ARCHIVE, 0x80000017));       // không phải reparse point thì bỏ qua tag
    }

    // ---- Đối chiếu xấp xỉ với dung lượng cấp phát THẬT của NTFS (FileStandardInfo.AllocationSize) ----
    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString native = QDir::toNativeSeparators(tmp.path());
        wchar_t volume[MAX_PATH + 1] = {};
        wchar_t fsName[MAX_PATH + 1] = {};
        DWORD spc = 0, bps = 0;
        const bool haveVolume = GetVolumePathNameW(reinterpret_cast<LPCWSTR>(native.utf16()), volume, MAX_PATH) &&
                                GetDiskFreeSpaceW(volume, &spc, &bps, nullptr, nullptr) &&
                                GetVolumeInformationW(volume, nullptr, 0, nullptr, nullptr, nullptr, fsName, MAX_PATH);
        if (haveVolume && std::wstring(fsName) == L"NTFS")
        {
            const qint64 cluster = static_cast<qint64>(spc) * bps;
            int mismatches = 0;
            // 601..900 là vùng mờ (tùy độ dài tên/ACL tệp có còn nằm trong MFT không) - không đòi chính xác ở đó.
            for (qint64 size : {0LL, 1LL, 100LL, 400LL, 1500LL, 4096LL, 4097LL, 70000LL, 1000000LL})
            {
                const QString p = tmp.path() + "/cal_" + QString::number(size) + ".bin";
                writeFile(p, size);
                const HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(p).utf16()), FILE_READ_ATTRIBUTES,
                                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
                FILE_STANDARD_INFO info{};
                const bool got = h != INVALID_HANDLE_VALUE && GetFileInformationByHandleEx(h, FileStandardInfo, &info, sizeof(info));
                if (h != INVALID_HANDLE_VALUE)
                    CloseHandle(h);
                CHECK(got);
                if (!got)
                    continue;
                // Đúng như bộ quét: giới hạn "resident" tính theo độ dài TÊN tệp.
                const qint64 estimate = FolderTreeInternal::allocatedBytes(
                    FILE_ATTRIBUTE_ARCHIVE, size, cluster, -1, FolderTreeInternal::residentLimitFor(static_cast<int>(QFileInfo(p).fileName().size())));
                if (estimate != info.AllocationSize.QuadPart)
                {
                    ++mismatches;
                    std::printf("  lech: tep %lld byte -> uoc tinh %lld, NTFS cap phat %lld (cum %lld)\n", size, estimate,
                                static_cast<long long>(info.AllocationSize.QuadPart), cluster);
                }
            }
            CHECK(mismatches == 0);
        }
        else
            std::printf("LUU Y: thu muc tam khong nam tren NTFS - bo qua doi chieu AllocationSize\n");
    }

    {
        QTemporaryDir tmp;
        CHECK(tmp.isValid());
        const QString r = tmp.path();
        const QString scanRoot = r + "/scan";

        // Kích thước cụm của ổ chứa thư mục tạm.
        qint64 cluster = 4096;
        bool isNtfs = false;
        {
            const QString native = QDir::toNativeSeparators(r);
            wchar_t volume[MAX_PATH + 1] = {};
            wchar_t fsName[MAX_PATH + 1] = {};
            DWORD spc = 0, bps = 0;
            if (GetVolumePathNameW(reinterpret_cast<LPCWSTR>(native.utf16()), volume, MAX_PATH) &&
                GetDiskFreeSpaceW(volume, &spc, &bps, nullptr, nullptr))
                cluster = static_cast<qint64>(spc) * bps;
            isNtfs = GetVolumeInformationW(volume, nullptr, 0, nullptr, nullptr, nullptr, fsName, MAX_PATH) && std::wstring(fsName) == L"NTFS";
        }
        // Dung lượng chiếm trên đĩa THẬT của từng tệp thử, đọc ĐỘC LẬP bằng FileStandardInfo.AllocationSize (nguồn sự thật);
        // kích thước các tệp thử đều khác nhau nên tra theo kích thước. Bộ quét phải ra ĐÚNG những con số này.
        std::map<qint64, qint64> realAlloc;
        const auto recordTruth = [&](const QString& path, qint64 size) {
            const HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(path).utf16()), FILE_READ_ATTRIBUTES,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            FILE_STANDARD_INFO info{};
            if (h != INVALID_HANDLE_VALUE && GetFileInformationByHandleEx(h, FileStandardInfo, &info, sizeof(info)))
                realAlloc[size] = info.AllocationSize.QuadPart;
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
        };
        const auto od = [&](qint64 logical) { return realAlloc.count(logical) ? realAlloc.at(logical) : qint64(-1); };

        QDir().mkpath(scanRoot + "/a/deep");
        QDir().mkpath(scanRoot + "/b");
        QDir().mkpath(scanRoot + "/empty");
        QDir().mkpath(scanRoot + "/tiny");
        QDir().mkpath(scanRoot + "/c/d/e/f");
        QDir().mkpath(r + "/outside");
        writeFile(scanRoot + "/a/a1.bin", 3000000);
        writeFile(scanRoot + "/a/a2.bin", 1500000);
        writeFile(scanRoot + "/a/deep/x.bin", 2000000);
        writeFile(scanRoot + "/a/deep/y.bin", 100);
        writeFile(scanRoot + "/b/b1.bin", 500000);
        writeFile(scanRoot + "/tiny/t.bin", 10);
        writeFile(scanRoot + "/c/d/e/f/g.bin", 7000000);
        writeFile(scanRoot + "/root1.bin", 1000);
        writeFile(r + "/outside/precious.bin", 9000000);
        recordTruth(scanRoot + "/a/a1.bin", 3000000);
        recordTruth(scanRoot + "/a/a2.bin", 1500000);
        recordTruth(scanRoot + "/a/deep/x.bin", 2000000);
        recordTruth(scanRoot + "/a/deep/y.bin", 100);
        recordTruth(scanRoot + "/b/b1.bin", 500000);
        recordTruth(scanRoot + "/tiny/t.bin", 10);
        recordTruth(scanRoot + "/c/d/e/f/g.bin", 7000000);
        recordTruth(scanRoot + "/root1.bin", 1000);
        CHECK(realAlloc.size() == 8);
        // Đã xác nhận bằng chạy thật: tệp lớn được cấp phát đúng bội số của cụm (không cấp dư), tệp rất nhỏ nằm trong MFT.
        CHECK(od(3000000) >= 3000000 && od(3000000) < 3000000 + cluster && od(3000000) % cluster == 0);
        CHECK(od(100) < cluster && od(10) < cluster);
        const bool haveJunction = makeJunction(scanRoot + "/jlink", r + "/outside");
        if (!haveJunction)
            std::printf("LUU Y: khong tao duoc junction (mklink /J) - bo qua kiem tra junction cua FolderTreeScanner\n");

        const qint64 logicalTotal = 3000000 + 1500000 + 2000000 + 100 + 500000 + 10 + 7000000 + 1000;
        const qint64 onDiskTotal = od(3000000) + od(1500000) + od(2000000) + od(100) + od(500000) + od(10) + od(7000000) + od(1000);

        bool forceEstimate = false; // true: ép dùng đường ước tính dự phòng (xem FolderTreeScanner::setForceEstimateForTest)
        const auto scan = [&](const QString& root, qint64 big, qint64 keep, int maxBig) {
            FolderTreeScanner s;
            s.setForceEstimateForTest(forceEstimate);
            std::shared_ptr<FolderTree> result;
            bool stopped = false;
            int finished = 0;
            QObject::connect(&s, &FolderTreeScanner::scanFinished, [&](std::shared_ptr<FolderTree> t) { result = t; ++finished; });
            QObject::connect(&s, &FolderTreeScanner::scanStopped, [&]() { stopped = true; });
            s.setRootPath(root);
            s.setBigFileBytes(big);
            s.setKeepFolderBytes(keep);
            s.setMaxBigFilesPerFolder(maxBig);
            s.startScan();
            waitUntil([&]() { return result || stopped; }, 30000);
            s.wait();
            CHECK(!stopped && finished == 1);
            return result;
        };

        // Mặc định (ngưỡng gộp 128 KiB, tệp lớn >= 1 MiB).
        const auto tree = scan(scanRoot, 1024 * 1024, 128 * 1024, 25);
        CHECK(tree && tree->root);
        if (tree && tree->root)
        {
            const FolderNode* root = tree->root.get();
            CHECK(tree->rootPath == QDir::cleanPath(scanRoot));
            CHECK(root->name == tree->rootPath && root->parent == nullptr);
            CHECK(root->fileCount == 8 && tree->stats.files == 8);
            CHECK(root->folderCount == 9 && tree->stats.folders == 9);
            CHECK(root->logicalBytes == logicalTotal && tree->stats.logicalBytes == logicalTotal);
            CHECK(root->bytes == onDiskTotal && tree->stats.bytes == onDiskTotal);
            CHECK(root->ownFileCount == 1 && root->ownBytes == od(1000));
            CHECK(tree->stats.clusterBytes == cluster);
            CHECK(tree->stats.unreadableFolders == 0);
            // Trên NTFS mọi số "chiếm trên đĩa" là số THẬT lấy từ lần liệt kê thư mục - không tệp nào phải ước tính.
            if (isNtfs)
                CHECK(tree->stats.estimatedFiles == 0);
            if (haveJunction)
                CHECK(tree->stats.skippedLinks == 1); // junction KHÔNG được đi vào: precious.bin không bị cộng
            // Thư mục con giữ lại: a (~6,5 MB), c (7 MB), b (~0,5 MB >= 128 KiB); "empty" và "tiny" (0 byte) bị gộp.
            CHECK(root->children.size() == 3);
            if (root->children.size() == 3)
            {
                CHECK(root->children[0]->name == "c" && root->children[1]->name == "a" && root->children[2]->name == "b"); // giảm dần
                CHECK(root->children[0]->bytes == od(7000000));
                CHECK(root->children[1]->bytes == od(3000000) + od(1500000) + od(2000000) + od(100));
                CHECK(root->children[2]->parent == root);
                CHECK(root->children[2]->path() == QDir::cleanPath(scanRoot) + "/b");
            }
            CHECK(root->prunedFolderCount == 2 && root->prunedBytes == od(10)); // "empty" (0) + "tiny" (tệp 10 byte)
            CHECK(root->bytes == root->ownBytes + root->children[0]->bytes + root->children[1]->bytes + root->children[2]->bytes + root->prunedBytes);

            // Tệp lớn (>= 1 MiB) được giữ riêng, giảm dần; phần còn lại gộp trong own*.
            const FolderNode* a = root->children.size() > 1 ? root->children[1].get() : nullptr;
            CHECK(a && a->bigFiles.size() == 2 && a->bigFiles[0].name == "a1.bin" && a->bigFiles[1].name == "a2.bin");
            if (a && a->bigFiles.size() == 2)
                CHECK(a->bigFiles[0].bytes == od(3000000) && a->bigFiles[0].logicalBytes == 3000000);
            const FolderNode* deep = a && !a->children.empty() ? a->children[0].get() : nullptr;
            CHECK(deep && deep->name == "deep" && deep->bigFiles.size() == 1 && deep->bigFiles[0].name == "x.bin");
            CHECK(deep && deep->ownFileCount == 2 && deep->ownBytes == od(2000000) + od(100));
            // Chuỗi thư mục lồng sâu c/d/e/f: tệp g.bin lớn nằm ở đáy, đường dẫn dựng ngược đúng.
            const FolderNode* f = root->children.size() > 0 ? root->children[0].get() : nullptr;
            for (const char* step : {"d", "e", "f"})
            {
                CHECK(f && !f->children.empty() && f->children[0]->name == step);
                f = (f && !f->children.empty()) ? f->children[0].get() : nullptr;
            }
            CHECK(f && f->bigFiles.size() == 1 && f->bigFiles[0].name == "g.bin");
            CHECK(f && f->path() == QDir::cleanPath(scanRoot) + "/c/d/e/f");
        }

        // Đường ƯỚC TÍNH dự phòng (ổ không hỗ trợ FileIdBothDirectoryInfo): cây thử có kích thước dễ đoán nên ra đúng số thật.
        {
            forceEstimate = true;
            const auto est = scan(scanRoot, 1024 * 1024, 128 * 1024, 25);
            forceEstimate = false;
            CHECK(est && est->root);
            if (est && est->root)
            {
                CHECK(est->stats.estimatedFiles == 8 && est->stats.files == 8 && est->stats.folders == 9);
                CHECK(est->stats.logicalBytes == logicalTotal);
                if (isNtfs)
                    CHECK(est->stats.bytes == onDiskTotal); // ước tính cụm + "nằm trong MFT" trùng số thật với các tệp này
                CHECK(est->root->children.size() == 3 && est->root->children[0]->name == "c");
            }
        }

        // Không gộp gì (keep = 0): mọi bất biến cộng dồn phải khớp tuyệt đối ở MỌI nút.
        const auto full = scan(scanRoot, 1, 0, 25);
        CHECK(full && full->root);
        if (full && full->root)
        {
            CHECK(full->root->children.size() == 5); // a, b, c, empty, tiny - junction KHÔNG phải thư mục con
            bool ok = true;
            int nodes = 0;
            std::vector<const FolderNode*> pending{full->root.get()};
            while (!pending.empty())
            {
                const FolderNode* n = pending.back();
                pending.pop_back();
                ++nodes;
                qint64 kidBytes = 0, kidLogical = 0, kidFiles = 0, kidFolders = 0;
                for (size_t i = 0; i < n->children.size(); ++i)
                {
                    const FolderNode* c = n->children[i].get();
                    ok = ok && c->parent == n;
                    ok = ok && (i == 0 || n->children[i - 1]->bytes >= c->bytes); // giảm dần
                    kidBytes += c->bytes;
                    kidLogical += c->logicalBytes;
                    kidFiles += c->fileCount;
                    kidFolders += 1 + c->folderCount;
                    pending.push_back(c);
                }
                ok = ok && n->prunedFolderCount == 0 && n->prunedBytes == 0;
                ok = ok && n->bytes == n->ownBytes + kidBytes;
                ok = ok && n->logicalBytes == n->ownLogicalBytes + kidLogical;
                ok = ok && n->fileCount == n->ownFileCount + kidFiles;
                ok = ok && n->folderCount == kidFolders;
                qint64 big = 0;
                for (const FolderFile& bf : n->bigFiles)
                    big += bf.bytes;
                ok = ok && big <= n->ownBytes;
            }
            CHECK(ok);
            CHECK(nodes == 10); // gốc + 9 thư mục
            CHECK(full->root->bytes == onDiskTotal);
        }

        // Giới hạn số tệp lớn giữ lại mỗi thư mục: chỉ giữ các tệp LỚN NHẤT.
        QDir().mkpath(r + "/many");
        for (int i = 1; i <= 8; ++i)
            writeFile(r + "/many/f" + QString::number(i) + ".bin", i * 100000);
        const auto many = scan(r + "/many", 1, 0, 3);
        CHECK(many && many->root && many->root->bigFiles.size() == 3);
        if (many && many->root && many->root->bigFiles.size() == 3)
        {
            CHECK(many->root->bigFiles[0].name == "f8.bin" && many->root->bigFiles[1].name == "f7.bin" && many->root->bigFiles[2].name == "f6.bin");
            CHECK(many->root->fileCount == 8 && many->root->ownBytes >= 3600000); // vẫn tính ĐỦ 8 tệp trong tổng
        }

        // Gốc không tồn tại / rỗng -> báo "không đọc được", không crash, vẫn kết thúc đúng một lần.
        const auto missing = scan(r + "/khong_ton_tai", 1, 0, 25);
        CHECK(missing && missing->root && missing->root->unreadable && missing->stats.unreadableFolders == 1 && missing->stats.files == 0);
        const auto blank = scan("", 1, 0, 25);
        CHECK(blank && blank->root && blank->root->unreadable);

        // Tệp THƯA (sparse) 50 MB không có dữ liệu: kích thước thật 50 MB nhưng gần như không chiếm chỗ trên đĩa.
        QDir().mkpath(r + "/sparse");
        {
            const HANDLE h = CreateFileW(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(r + "/sparse/s.bin").utf16()),
                                         GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            bool madeSparse = false;
            if (h != INVALID_HANDLE_VALUE)
            {
                DWORD ret = 0;
                LARGE_INTEGER size;
                size.QuadPart = 50LL * 1024 * 1024;
                madeSparse = DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &ret, nullptr) &&
                             SetFilePointerEx(h, size, nullptr, FILE_BEGIN) && SetEndOfFile(h);
                CloseHandle(h);
            }
            if (madeSparse)
            {
                const auto sp = scan(r + "/sparse", 1, 0, 25);
                CHECK(sp && sp->root && sp->root->logicalBytes == 50LL * 1024 * 1024);
                CHECK(sp && sp->root && sp->root->bytes < 1024 * 1024); // đọc kích thước THẬT qua GetCompressedFileSizeW
            }
            else
                std::printf("LUU Y: khong tao duoc tep thua (sparse) - bo qua kiem tra tep thua\n");
        }

        // Đường dẫn DÀI hơn 260 ký tự (rất hay gặp trong node_modules/cache): vẫn đếm đủ tệp ở đáy.
        {
            QString deepDir = r + "/long";
            for (int i = 0; i < 100; ++i)
                deepDir += "/dddddddd"; // 100 x 9 = 900 ký tự
            if (QDir().mkpath(deepDir) && QFileInfo(deepDir).isDir())
            {
                writeFile(deepDir + "/bottom.bin", 5000);
                const auto lt = scan(r + "/long", 1, 0, 25);
                CHECK(lt && lt->root && lt->root->fileCount == 1 && lt->root->folderCount == 100);
                const FolderNode* n = lt && lt->root ? lt->root.get() : nullptr;
                while (n && !n->children.empty())
                    n = n->children[0].get();
                CHECK(n && n->path() == QDir::cleanPath(deepDir)); // path() dựng ngược đúng ở độ sâu 100
            }
            else
                std::printf("LUU Y: khong tao duoc duong dan dai - bo qua kiem tra duong dan dai\n");
        }

        // Thư mục KHÔNG đọc được (từ chối truy cập): được đánh dấu và đếm, các nhánh khác vẫn quét bình thường.
        {
            QDir().mkpath(r + "/denied/locked");
            writeFile(r + "/denied/locked/secret.bin", 4000);
            writeFile(r + "/denied/open.bin", 2000);
            const QString user = qEnvironmentVariable("USERNAME");
            QProcess icacls;
            icacls.start(AppPaths::systemExecutable("icacls.exe"),
                         {QDir::toNativeSeparators(r + "/denied/locked"), "/deny", user + ":(OI)(CI)(RX)"});
            const bool denied = !user.isEmpty() && icacls.waitForFinished(10000) && icacls.exitCode() == 0;
            if (denied)
            {
                const auto dt = scan(r + "/denied", 1, 0, 25);
                CHECK(dt && dt->root && dt->stats.unreadableFolders == 1 && dt->root->fileCount == 1);
                bool flagged = false;
                if (dt && dt->root)
                    for (const auto& c : dt->root->children)
                        if (c->name == "locked" && c->unreadable)
                            flagged = true;
                CHECK(flagged);
                // Khôi phục quyền để QTemporaryDir dọn được.
                QProcess undo;
                undo.start(AppPaths::systemExecutable("icacls.exe"),
                           {QDir::toNativeSeparators(r + "/denied/locked"), "/remove:d", user});
                undo.waitForFinished(10000);
            }
            else
                std::printf("LUU Y: khong chan duoc quyen doc bang icacls - bo qua kiem tra thu muc khong doc duoc\n");
        }

        // Dừng giữa chừng / chạy lại liên tục: mỗi lượt kết thúc ĐÚNG MỘT lần (xong HOẶC dừng) và lượt sau vẫn đầy đủ.
        {
            for (int d = 0; d < 40; ++d)
            {
                QDir().mkpath(r + "/wide/d" + QString::number(d));
                for (int f = 0; f < 25; ++f)
                    writeFile(r + "/wide/d" + QString::number(d) + "/f" + QString::number(f) + ".bin", 10);
            }
            FolderTreeScanner s;
            int finished = 0, stopped = 0;
            std::shared_ptr<FolderTree> last;
            QObject::connect(&s, &FolderTreeScanner::scanFinished, [&](std::shared_ptr<FolderTree> t) { ++finished; last = t; });
            QObject::connect(&s, &FolderTreeScanner::scanStopped, [&]() { ++stopped; });
            s.setRootPath(r + "/wide");
            for (int round = 0; round < 30; ++round)
            {
                s.startScan();
                if (round % 3 != 0)
                    s.requestStop();
                s.wait();
                QCoreApplication::processEvents();
                CHECK(finished + stopped == round + 1);
            }
            CHECK(finished >= 10 && stopped + finished == 30);
            CHECK(last && last->root && last->root->fileCount == 1000 && last->root->folderCount == 40);
        }
    }

    // =====================================================================================
    // ProcessMonitor: danh sách tiến trình + RAM/CPU (tab "Tiến trình & RAM"). CHỈ ĐỌC; tiến trình phụ duy nhất là
    // ping.exe do chính test khởi chạy rồi tự kết thúc.
    // =====================================================================================
    {
        // ---- Thanh RAM: JSON + tên loại ----
        CHECK(ProcessMonitor::memoryTypeName(26) == "DDR4" && ProcessMonitor::memoryTypeName(34) == "DDR5");
        CHECK(ProcessMonitor::memoryTypeName(0).isEmpty() && ProcessMonitor::memoryTypeName(999).isEmpty());
        {
            QString err;
            const auto two = ProcessMonitor::internal::parseMemoryModulesJson(
                R"([{"Slot":"DIMM A","Capacity":8589934592,"Speed":3200,"Configured":2933,"Type":26,"Manufacturer":"Samsung ","Part":"M471A1K43"},)"
                R"({"Slot":"DIMM B","Capacity":8589934592,"Speed":3200,"Configured":0,"Type":26,"Manufacturer":"","Part":""}])", &err);
            CHECK(err.isEmpty() && two.size() == 2);
            if (two.size() == 2)
            {
                CHECK(two[0].slot == "DIMM A" && two[0].capacityBytes == 8589934592LL && two[0].speedMHz == 2933); // ưu tiên tốc độ đang chạy
                CHECK(two[0].type == "DDR4" && two[0].manufacturer == "Samsung" && two[0].partNumber == "M471A1K43");
                CHECK(two[1].speedMHz == 3200); // Configured = 0 -> rơi về Speed
            }
            // Một thanh RAM: ConvertTo-Json trả đối tượng đơn, không bọc mảng.
            const auto one = ProcessMonitor::internal::parseMemoryModulesJson(
                R"({"Slot":"ChannelA","Capacity":17179869184,"Speed":4800,"Configured":4800,"Type":34,"Manufacturer":"X","Part":"Y"})", &err);
            CHECK(one.size() == 1 && one[0].type == "DDR5" && one[0].capacityBytes == 17179869184LL);
            CHECK(ProcessMonitor::internal::parseMemoryModulesJson("[]", &err).isEmpty() && err.isEmpty());
            CHECK(ProcessMonitor::internal::parseMemoryModulesJson("", &err).isEmpty() && err.isEmpty());
            QString bad;
            CHECK(ProcessMonitor::internal::parseMemoryModulesJson("{khong phai json", &bad).isEmpty() && !bad.isEmpty());
            // Mục dung lượng 0 / không phải đối tượng bị bỏ qua.
            CHECK(ProcessMonitor::internal::parseMemoryModulesJson(R"([{"Slot":"A","Capacity":0},5,"x"])", &err).isEmpty());
        }
        // ---- describeMemoryModules: một dòng tóm tắt ----
        {
            const auto mod = [](qint64 gib, int mhz, const QString& type, const QString& maker) {
                MemoryModule m;
                m.slot = "S";
                m.capacityBytes = gib * 1024 * 1024 * 1024;
                m.speedMHz = mhz;
                m.type = type;
                m.manufacturer = maker;
                return m;
            };
            CHECK(ProcessMonitor::describeMemoryModules({}).isEmpty());
            CHECK(ProcessMonitor::describeMemoryModules({mod(8, 2933, "DDR4", "Samsung"), mod(8, 2933, "DDR4", "samsung")}) ==
                  QString::fromUtf8("2 thanh RAM: 8 GB + 8 GB · DDR4 2933 MHz · Samsung (tổng 16 GB)"));
            CHECK(ProcessMonitor::describeMemoryModules({mod(16, 4800, "DDR5", "")}) == QString::fromUtf8("1 thanh RAM: 16 GB · DDR5 4800 MHz"));
            // Khác dung lượng/tốc độ/loại: liệt kê giảm dần, tốc độ/loại cách nhau bằng "/".
            CHECK(ProcessMonitor::describeMemoryModules({mod(4, 2133, "DDR4", ""), mod(8, 2400, "DDR4", "")}) ==
                  QString::fromUtf8("2 thanh RAM: 8 GB + 4 GB · DDR4 · 2400/2133 MHz (tổng 12 GB)") ||
                  ProcessMonitor::describeMemoryModules({mod(4, 2133, "DDR4", ""), mod(8, 2400, "DDR4", "")}) ==
                  QString::fromUtf8("2 thanh RAM: 8 GB + 4 GB · DDR4 · 2133/2400 MHz (tổng 12 GB)"));
            // Không rõ loại/tốc độ: chỉ dung lượng.
            CHECK(ProcessMonitor::describeMemoryModules({mod(8, 0, "", "")}) == QString::fromUtf8("1 thanh RAM: 8 GB"));
        }
        {
            QString err;
            const auto real = ProcessMonitor::memoryModules(&err); // đọc THẬT (chỉ đọc); máy ảo có thể rỗng
            std::printf("Win32_PhysicalMemory that: %d thanh RAM%s%s\n", static_cast<int>(real.size()), err.isEmpty() ? "" : " (loi: ", err.isEmpty() ? "" : qPrintable(err + ")"));
            for (const auto& m : real)
                std::printf("  - %s %lld MB %s %d MHz\n", qPrintable(m.slot), m.capacityBytes / (1024 * 1024), qPrintable(m.type), m.speedMHz);
            for (const auto& m : real)
                CHECK(m.capacityBytes > 0);
        }

        // ---- ProcessSampler: đối chiếu với API gốc của Windows trên CHÍNH tiến trình này ----
        const quint32 selfPid = GetCurrentProcessId();
        const auto findPid = [](const SystemSnapshot& s, quint32 pid) -> const ProcessInfo* {
            for (const ProcessInfo& p : s.processes)
                if (p.pid == pid)
                    return &p;
            return nullptr;
        };

        // Đốt CPU một luồng + ghi 30 MB ra đĩa trong khoảng giữa hai mẫu để thấy % CPU và tốc độ I/O của chính mình.
        QTemporaryDir ioTmp;
        ProcessSampler sampler;
        SystemSnapshot s1 = sampler.sample();
        std::atomic_bool spin{true};
        std::thread burner([&]() {
            volatile quint64 x = 0;
            while (spin.load())
                x = x + 1;
        });
        writeFile(ioTmp.path() + "/io.bin", 30 * 1024 * 1024);
        QThread::msleep(700);
        const SystemSnapshot s2 = sampler.sample();
        spin = false;
        burner.join();

        CHECK(s1.nativeApi && s2.nativeApi);
        CHECK(s1.processes.size() > 20 && s2.processes.size() > 20);
        CHECK(s2.logicalProcessors >= 1);
        const ProcessInfo* me = findPid(s2, selfPid);
        CHECK(me != nullptr);
        if (me)
        {
            const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
            CHECK(me->name.compare(exeName, Qt::CaseInsensitive) == 0);
            CHECK(QDir::fromNativeSeparators(me->path).compare(QDir::fromNativeSeparators(QCoreApplication::applicationFilePath()), Qt::CaseInsensitive) == 0);
            const QString userName = qEnvironmentVariable("USERNAME");
            CHECK(!me->user.isEmpty() && me->user.contains(userName, Qt::CaseInsensitive));
            CHECK(me->startTime.isValid() && me->startTime <= QDateTime::currentDateTime() && me->startTime > QDateTime::currentDateTime().addDays(-30));

            PROCESS_MEMORY_COUNTERS_EX pmc;
            pmc.cb = sizeof(pmc);
            CHECK(GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)));
            // ("near" là macro rỗng của windows.h nên không dùng làm tên.)
            const auto closeTo = [](qint64 a, qint64 b, double rel, qint64 slack) { return qAbs(a - b) <= qMax<qint64>(slack, static_cast<qint64>(rel * static_cast<double>(qMax(a, b)))); };
            CHECK(closeTo(me->workingSetBytes, static_cast<qint64>(pmc.WorkingSetSize), 0.25, 8 * 1024 * 1024));
            CHECK(closeTo(me->privateBytes, static_cast<qint64>(pmc.PrivateUsage), 0.25, 8 * 1024 * 1024));
            CHECK(me->privateWorkingSetBytes > 0 && me->privateWorkingSetBytes <= me->workingSetBytes + 4 * 1024 * 1024);
            CHECK(me->peakWorkingSetBytes >= me->workingSetBytes / 2);
            DWORD handles = 0;
            CHECK(GetProcessHandleCount(GetCurrentProcess(), &handles));
            CHECK(qAbs(static_cast<qint64>(me->handles) - static_cast<qint64>(handles)) <= 100);

            // Số luồng + PID cha so với Toolhelp (nguồn độc lập).
            quint32 toolhelpThreads = 0, toolhelpParent = 0;
            const HANDLE ts = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD | TH32CS_SNAPPROCESS, 0);
            if (ts != INVALID_HANDLE_VALUE)
            {
                THREADENTRY32 te;
                te.dwSize = sizeof(te);
                if (Thread32First(ts, &te))
                    do { if (te.th32OwnerProcessID == selfPid) ++toolhelpThreads; } while (Thread32Next(ts, &te));
                PROCESSENTRY32W pe;
                pe.dwSize = sizeof(pe);
                if (Process32FirstW(ts, &pe))
                    do { if (pe.th32ProcessID == selfPid) toolhelpParent = pe.th32ParentProcessID; } while (Process32NextW(ts, &pe));
                CloseHandle(ts);
            }
            CHECK(toolhelpThreads > 0 && qAbs(static_cast<int>(me->threads) - static_cast<int>(toolhelpThreads)) <= 8);
            CHECK(toolhelpParent != 0 && me->parentPid == toolhelpParent);

            // CPU: một luồng đốt liên tục ~0,7 giây => ít nhất ~40% của (100 / số lõi) % tổng CPU.
            const double oneCore = 100.0 / s2.logicalProcessors;
            CHECK(me->cpuPercent >= 0.4 * oneCore && me->cpuPercent <= 100.0);
            CHECK(s2.cpuPercent >= 0.4 * oneCore && s2.cpuPercent <= 100.0);
            // I/O: ghi 30 MB trong ~0,7 giây => tốc độ phải thấy rõ (đặt ngưỡng thấp: 5 MB/s).
            CHECK(me->ioBytesPerSec > 5 * 1024 * 1024);
            CHECK(me->ioWriteBytes >= 30LL * 1024 * 1024);
            // Mẫu ĐẦU TIÊN chưa có mốc so sánh -> % CPU và I/O bằng 0.
            const ProcessInfo* me1 = findPid(s1, selfPid);
            CHECK(me1 && me1->cpuPercent == 0.0 && me1->ioBytesPerSec == 0 && s1.cpuPercent == 0.0);
        }

        // RAM toàn máy so với GlobalMemoryStatusEx / GetPerformanceInfo.
        MEMORYSTATUSEX ms;
        ms.dwLength = sizeof(ms);
        CHECK(GlobalMemoryStatusEx(&ms));
        CHECK(s2.memory.totalPhysical == static_cast<qint64>(ms.ullTotalPhys));
        CHECK(s2.memory.availablePhysical > 0 && s2.memory.availablePhysical < s2.memory.totalPhysical);
        CHECK(qAbs(s2.memory.availablePhysical - static_cast<qint64>(ms.ullAvailPhys)) < s2.memory.totalPhysical / 5);
        CHECK(s2.memory.usedPercent() > 0.0 && s2.memory.usedPercent() < 100.0);
        CHECK(s2.memory.commitUsed > 0 && s2.memory.commitLimit >= s2.memory.commitUsed && s2.memory.commitPeak >= s2.memory.commitUsed);
        CHECK(s2.memory.kernelNonpaged > 0 && s2.memory.kernelPaged > 0);
        CHECK(s2.uptimeMs > 0 && s2.threadCount > s2.processCount && s2.handleCount > 1000);
        CHECK(qAbs(static_cast<int>(s2.processCount) - static_cast<int>(s2.processes.size())) <= 15);
        // Tiến trình nhân luôn có: System Idle Process (PID 0) và System (PID 4).
        const ProcessInfo* idle = findPid(s2, 0);
        const ProcessInfo* sys = findPid(s2, 4);
        CHECK(idle && idle->name == "System Idle Process" && sys && sys->name == "System");
        CHECK(sys && sys->workingSetBytes > 0 && sys->threads > 10); // dữ liệu cả tiến trình được bảo vệ (không cần OpenProcess)
        CHECK(idle && idle->cpuPercent > 1.0); // CPU rảnh hiện ở "tiến trình" Idle, như Task Manager
        // Mọi PID duy nhất, tên không rỗng.
        {
            QSet<quint32> seen;
            bool unique = true, named = true;
            for (const ProcessInfo& p : s2.processes)
            {
                unique = unique && !seen.contains(p.pid);
                seen.insert(p.pid);
                named = named && !p.name.isEmpty();
            }
            CHECK(unique && named);
        }

        // Tiến trình CON do test khởi chạy: phải xuất hiện với PID cha = PID của test; kill xong thì biến mất.
        {
            QProcess child;
            child.start(AppPaths::systemExecutable("ping.exe"), {"-n", "30", "127.0.0.1"});
            CHECK(child.waitForStarted(5000));
            const quint32 childPid = static_cast<quint32>(child.processId());
            const SystemSnapshot s3 = sampler.sample();
            const ProcessInfo* c = findPid(s3, childPid);
            CHECK(c != nullptr);
            if (c)
            {
                CHECK(c->parentPid == selfPid && c->name.compare("ping.exe", Qt::CaseInsensitive) == 0);
                CHECK(c->path.endsWith("ping.exe", Qt::CaseInsensitive));
                CHECK(me && c->sessionId == me->sessionId);
                CHECK(c->workingSetBytes > 0 && c->threads >= 1);
            }
            child.kill();
            child.waitForFinished(5000);
            QThread::msleep(100);
            CHECK(findPid(sampler.sample(), childPid) == nullptr);
        }

        // Phương án dự phòng (Toolhelp + OpenProcess): cùng tập tiến trình, cùng số liệu của chính mình.
        {
            ProcessSampler fallback;
            fallback.setForceFallbackForTest(true);
            const SystemSnapshot f = fallback.sample();
            CHECK(!f.nativeApi && f.processes.size() > 20);
            const ProcessInfo* fme = findPid(f, selfPid);
            CHECK(fme != nullptr);
            if (fme && me)
            {
                CHECK(fme->name.compare(me->name, Qt::CaseInsensitive) == 0 && fme->parentPid == me->parentPid);
                CHECK(qAbs(fme->workingSetBytes - me->workingSetBytes) < 50LL * 1024 * 1024);
                CHECK(qAbs(static_cast<int>(fme->threads) - static_cast<int>(me->threads)) <= 8);
            }
            QSet<quint32> nativePids, fallbackPids;
            for (const ProcessInfo& p : s2.processes)
                nativePids.insert(p.pid);
            for (const ProcessInfo& p : f.processes)
                fallbackPids.insert(p.pid);
            CHECK(nativePids.subtract(fallbackPids).size() <= 15 && fallbackPids.size() > 20);
        }

        // reset(): quên mẫu trước.
        sampler.reset();
        const SystemSnapshot afterReset = sampler.sample();
        CHECK(afterReset.cpuPercent == 0.0);
        {
            bool allZero = true;
            for (const ProcessInfo& p : afterReset.processes)
                allZero = allZero && p.cpuPercent == 0.0 && p.ioBytesPerSec == 0;
            CHECK(allZero);
        }
    }

    // ---- ProcessSamplerThread: chỉ lấy mẫu khi được bật, ngủ khi tắt ----
    {
        ProcessSamplerThread worker;
        int received = 0;
        bool sane = true;
        QObject::connect(&worker, &ProcessSamplerThread::sampled, [&](std::shared_ptr<SystemSnapshot> s) {
            ++received;
            sane = sane && s && s->processes.size() > 20 && s->memory.totalPhysical > 0;
        });
        worker.setIntervalMs(250);
        worker.start(); // chưa bật -> không lấy mẫu
        QThread::msleep(600);
        QCoreApplication::processEvents();
        CHECK(received == 0 && worker.sampleCount() == 0 && !worker.isActive());
        worker.setActive(true);
        CHECK(waitUntil([&]() { return received >= 3; }, 8000));
        CHECK(worker.isActive() && sane);
        worker.setActive(false);
        QThread::msleep(400); // chờ mẫu đang dở (nếu có) xong
        QCoreApplication::processEvents();
        const int frozen = worker.sampleCount();
        QThread::msleep(900);
        QCoreApplication::processEvents();
        CHECK(worker.sampleCount() == frozen); // tắt rồi thì không lấy thêm
        // Bật lại: tiếp tục (mẫu đầu tiên sau khi bật lại đã có % CPU nhờ mẫu "khởi động").
        worker.setActive(true);
        CHECK(waitUntil([&]() { return worker.sampleCount() > frozen; }, 8000));
        worker.requestStop();
        CHECK(worker.wait(5000));
        CHECK(!worker.isRunning());
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
