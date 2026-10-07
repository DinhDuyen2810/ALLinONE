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
#include "tools/diskcleanup/engine/DuplicateFinder.h"
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
        finder3.start();
        finder3.requestStop();
        CHECK(waitUntil([&] { return stopped || !finder3.isRunning(); }));
        finder3.wait();

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

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
