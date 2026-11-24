// KIỂM TRA TAY - CHỈ ĐỌC: quét THẬT các hạng mục dọn dẹp của máy này bằng đúng CategoryRegistry + CleanupScanner
// của ứng dụng (tab "Dọn dẹp theo hạng mục") rồi in từng hạng mục tìm được bao nhiêu. KHÔNG xóa gì, KHÔNG làm
// trống Thùng rác, KHÔNG tắt ngủ đông - chỉ liệt kê + tính kích thước (giống bấm "Quét").
//
//   cleanup_scan_real_check [--sources] [--items N]
//     --sources  in thêm từng thư mục gốc (nguồn) của mỗi hạng mục và dung lượng của nó
//     --items N  với hạng mục "xem kỹ" (Downloads, thư mục mồ côi) in N mục đầu (mặc định 5)
//
// Dùng khi sửa CategoryRegistry/CacheLocator/LeftoverFinder để xem kết quả trên máy thật (vd hồ sơ Chrome
// "Profile 2" có được tính không). Không phải test tự động (kết quả phụ thuộc máy).
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QHash>
#include <QStringList>

#include <cstdio>

#include "tools/diskcleanup/engine/CategoryRegistry.h"
#include "tools/diskcleanup/engine/CleanupScanner.h"
#include "tools/diskcleanup/engine/CleanupSystem.h"
#include "tools/diskcleanup/engine/PartitionManager.h"

static QString human(qint64 b)
{
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = static_cast<double>(b);
    int u = 0;
    while (v >= 1024.0 && u < 4)
    {
        v /= 1024.0;
        ++u;
    }
    return QString::number(v, 'f', u == 0 ? 0 : 1) + " " + units[u];
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    const bool showSources = args.contains("--sources");
    int showItems = 5;
    const int idx = args.indexOf("--items");
    if (idx >= 0 && idx + 1 < args.size())
        showItems = args[idx + 1].toInt();

    const bool elevated = PartitionManager::isElevated();
    const QSet<QString> running = CleanupSystem::runningExeNamesLower();
    const QList<CleanupCategory> all = CategoryRegistry::buildCategories(CleanupEnvironment::current());
    QList<CleanupCategory> available;
    for (const CleanupCategory& c : all)
        if (CategoryRegistry::isAvailable(c))
            available << c;
    std::printf("Quyen Administrator: %s | %d/%d hang muc kha dung tren may nay\n\n", elevated ? "CO" : "KHONG",
                static_cast<int>(available.size()), static_cast<int>(all.size()));

    CleanupScanner scanner;
    scanner.setCategories(available);
    QHash<CleanupCategoryId, QList<CleanupItem>> items;
    QHash<CleanupCategoryId, QPair<qint64, int>> totals;
    qint64 grand = 0;
    QObject::connect(&scanner, &CleanupScanner::itemFound, [&](CleanupItem i) { items[i.categoryId] << i; });
    QObject::connect(&scanner, &CleanupScanner::categoryFinished, [&](CleanupCategoryId id, qint64 b, int n) {
        totals[id] = {b, n};
    });
    QObject::connect(&scanner, &CleanupScanner::scanFinished, [&](qint64 b, int) {
        grand = b;
        app.quit();
    });
    QObject::connect(&scanner, &CleanupScanner::scanStopped, &app, &QCoreApplication::quit);
    QElapsedTimer timer;
    timer.start();
    scanner.startScan();
    app.exec();
    scanner.wait();

    QString lastGroup;
    qint64 regenerable = 0;
    for (const CleanupCategory& c : available)
    {
        if (c.group != lastGroup)
        {
            std::printf("== %s\n", c.group.toUtf8().constData());
            lastGroup = c.group;
        }
        const auto t = totals.value(c.id);
        QStringList flags;
        flags << CategoryRegistry::riskName(c.risk);
        if (c.regenerable)
            flags << "xoa han";
        else
            flags << "tep cua ban";
        if (c.needsAdmin && !elevated)
            flags << "CAN ADMIN";
        for (const QString& exe : c.processNames)
            if (running.contains(exe))
            {
                flags << exe + " DANG CHAY";
                break;
            }
        std::printf("  %-55s %10s  %6d muc  [%s]\n", c.name.toUtf8().constData(), human(t.first).toUtf8().constData(),
                    t.second, flags.join(", ").toUtf8().constData());
        if (!c.note.isEmpty())
            std::printf("      %s\n", c.note.toUtf8().constData());
        if (c.regenerable)
            regenerable += t.first;
        const QList<CleanupItem> list = items.value(c.id);
        if (showSources && c.kind == CleanupKind::FolderContents)
        {
            QHash<QString, qint64> perRoot;
            for (const CleanupItem& i : list)
                perRoot[i.root] += i.sizeBytes;
            for (auto it = perRoot.constBegin(); it != perRoot.constEnd(); ++it)
                std::printf("      %10s  %s\n", human(it.value()).toUtf8().constData(),
                            QDir::toNativeSeparators(it.key()).toUtf8().constData());
        }
        if (c.showItems)
            for (int i = 0; i < list.size() && i < showItems; ++i)
                std::printf("      %10s  %s (%s)\n", human(list[i].sizeBytes).toUtf8().constData(),
                            QDir::toNativeSeparators(list[i].path).toUtf8().constData(), list[i].detail.toUtf8().constData());
    }
    std::printf("\nTong tim thay: %s (trong do du lieu tu tao lai - xoa han: %s) | quet mat %lld ms\n",
                human(grand).toUtf8().constData(), human(regenerable).toUtf8().constData(),
                static_cast<long long>(timer.elapsed()));
    return 0;
}
