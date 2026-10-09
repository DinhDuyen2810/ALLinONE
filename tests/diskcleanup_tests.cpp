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
#include <QTemporaryDir>
#include <QProcess>
#include <QTimer>
#include <cstdio>
#include <limits>

#include <windows.h>

#include "core/AppPaths.h"

#include "tools/diskcleanup/engine/CategoryRegistry.h"
#include "tools/diskcleanup/engine/CleanupExecutor.h"
#include "tools/diskcleanup/engine/CleanupScanner.h"
#include "tools/diskcleanup/engine/DiskSpaceInfo.h"
#include "tools/diskcleanup/engine/DuplicateFinder.h"
#include "tools/diskcleanup/engine/FsSafety.h"
#include "tools/diskcleanup/engine/LargeFileScanner.h"
#include "tools/diskcleanup/engine/PartitionManager.h"
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

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
