// Kiểm thử lõi Disk Cleanup. KHÔNG đụng vào thư mục hệ thống thật (Temp/Windows/SoftwareDistribution)
// - CategoryRegistry/CleanupScanner được test bằng môi trường giả trỏ vào QTemporaryDir. Phần
// RecycleBinOps CÓ chạy thật nhưng chỉ trên các tệp do chính test tạo ra trong thư mục tạm của nó
// (an toàn, có thể khôi phục từ Thùng rác) - không bao giờ gọi RecycleBinOps::empty() (sẽ xóa vĩnh
// viễn TOÀN BỘ Thùng rác thật của người dùng, kể cả các mục không liên quan tới test).
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>
#include <cstdio>

#include "tools/diskcleanup/engine/CategoryRegistry.h"
#include "tools/diskcleanup/engine/CleanupExecutor.h"
#include "tools/diskcleanup/engine/CleanupScanner.h"
#include "tools/diskcleanup/engine/DiskSpaceInfo.h"
#include "tools/diskcleanup/engine/LargeFileScanner.h"
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
        scanner2.start();
        scanner2.requestStop();
        CHECK(waitUntil([&] { return stopped || !scanner2.isRunning(); }));
        scanner2.wait();
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
        executor.setItems({f1, f2}, 123 + 456);

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

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
