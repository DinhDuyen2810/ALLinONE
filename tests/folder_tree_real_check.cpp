// KIỂM TRA TAY (CHỈ ĐỌC): quét THẬT một thư mục/ổ đĩa bằng FolderTreeScanner - đúng lớp mà tab "Cây thư mục" dùng - rồi in
// thống kê + các thư mục con lớn nhất, và (tùy chọn) đối chiếu ước tính "chiếm trên đĩa" với dung lượng cấp phát THẬT của
// NTFS (FileStandardInfo.AllocationSize) trên một mẫu ngẫu nhiên các tệp thật.
//
//   cmake --build build --target folder_tree_real_check
//   build\folder_tree_real_check.exe <đường dẫn> [--top N] [--verify N]
//     --top N     in N thư mục con trực tiếp lớn nhất (mặc định 15)
//     --verify N  lấy mẫu N tệp ngẫu nhiên, so ước tính với AllocationSize thật (mặc định: không)
//
// Không sửa/xóa gì; không phải test tự động (quét thư mục thật của máy).

#include "tools/diskcleanup/engine/DiskSpaceInfo.h"
#include "tools/diskcleanup/engine/FolderTreeScanner.h"
#include "tools/diskcleanup/engine/FsSafety.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QRandomGenerator>

#include <cstdio>
#include <string>
#include <vector>

#include <windows.h>
#include <psapi.h>

namespace
{
QString human(qint64 bytes)
{
    const double b = static_cast<double>(bytes);
    if (b >= 1024.0 * 1024 * 1024)
        return QString::number(b / (1024.0 * 1024 * 1024), 'f', 2) + " GB";
    if (b >= 1024.0 * 1024)
        return QString::number(b / (1024.0 * 1024), 'f', 1) + " MB";
    return QString::number(b / 1024.0, 'f', 0) + " KB";
}

std::wstring extended(const QString& path)
{
    const QString native = QDir::toNativeSeparators(path);
    return (QStringLiteral("\\\\?\\") + native).toStdWString();
}

/// AllocationSize của MỘT tệp lấy từ chính lần LIỆT KÊ thư mục (FileIdBothDirectoryInfo) - không mở từng tệp. -1 nếu không có.
qint64 dirListingAllocation(const QString& filePath)
{
    const int slash = filePath.lastIndexOf('/');
    if (slash <= 0)
        return -1;
    const std::wstring name = filePath.mid(slash + 1).toStdWString();
    const HANDLE h = CreateFileW(extended(filePath.left(slash)).c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    std::vector<char> buf(64 * 1024);
    bool first = true;
    qint64 result = -1;
    while (result < 0 && GetFileInformationByHandleEx(h, first ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo, buf.data(), static_cast<DWORD>(buf.size())))
    {
        first = false;
        const char* p = buf.data();
        for (;;)
        {
            const auto* e = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(p);
            const std::wstring n(e->FileName, e->FileNameLength / sizeof(wchar_t));
            if (_wcsicmp(n.c_str(), name.c_str()) == 0)
            {
                result = e->AllocationSize.QuadPart;
                break;
            }
            if (e->NextEntryOffset == 0)
                break;
            p += e->NextEntryOffset;
        }
    }
    CloseHandle(h);
    return result;
}

struct ClassStats
{
    qint64 files{0};
    qint64 exact{0};
    qint64 estimated{0};
    qint64 actual{0};
};
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QString root;
    int top = 15, verify = 0;
    for (int i = 1; i < argc; ++i)
    {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--top" && i + 1 < argc)
            top = QString::fromLocal8Bit(argv[++i]).toInt();
        else if (a == "--verify" && i + 1 < argc)
            verify = QString::fromLocal8Bit(argv[++i]).toInt();
        else
            root = a;
    }
    if (root.isEmpty())
    {
        std::printf("Cach dung: folder_tree_real_check <duong dan> [--top N] [--verify N]\n");
        return 2;
    }

    FolderTreeScanner scanner;
    std::shared_ptr<FolderTree> tree;
    bool stopped = false;
    QEventLoop loop;
    QObject::connect(&scanner, &FolderTreeScanner::progressTick, [](qint64 files, qint64 folders, qint64 bytes, QString) {
        std::fprintf(stderr, "\r  dang quet... %lld tep, %lld thu muc, %s        ", static_cast<long long>(files),
                     static_cast<long long>(folders), qPrintable(human(bytes)));
    });
    QObject::connect(&scanner, &FolderTreeScanner::scanFinished, [&](std::shared_ptr<FolderTree> t) { tree = t; loop.quit(); });
    QObject::connect(&scanner, &FolderTreeScanner::scanStopped, [&]() { stopped = true; loop.quit(); });
    scanner.setRootPath(root);
    scanner.startScan(QThread::LowPriority);
    loop.exec();
    scanner.wait();
    std::fprintf(stderr, "\r%80s\r", "");
    if (!tree || stopped)
    {
        std::printf("Khong co ket qua quet.\n");
        return 1;
    }

    PROCESS_MEMORY_COUNTERS pmc{};
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    const FolderTreeStats& s = tree->stats;
    std::printf("GOC: %s\n", qPrintable(tree->rootPath));
    std::printf("  tep: %lld | thu muc: %lld | khong doc duoc: %lld | junction/symlink bo qua: %lld | tep chi tren may chu: %lld\n",
                static_cast<long long>(s.files), static_cast<long long>(s.folders), static_cast<long long>(s.unreadableFolders),
                static_cast<long long>(s.skippedLinks), static_cast<long long>(s.cloudFiles));
    std::printf("  chiem tren dia: %lld (%s) | kich thuoc tep: %lld (%s) | cum: %lld\n", static_cast<long long>(s.bytes), qPrintable(human(s.bytes)),
                static_cast<long long>(s.logicalBytes), qPrintable(human(s.logicalBytes)), static_cast<long long>(s.clusterBytes));
    std::printf("  tep phai uoc tinh: %lld | tep phai mo rieng de hoi lai dung luong: %lld\n", static_cast<long long>(s.estimatedFiles), static_cast<long long>(s.reopenedFiles));
    std::printf("  thoi gian quet: %.1f giay | bo nho dinh cua tien trinh nay: %s\n", s.elapsedMs / 1000.0, qPrintable(human(static_cast<qint64>(pmc.PeakWorkingSetSize))));
    for (const DriveSpaceInfo& d : DiskSpaceInfo::listDrives())
        if (QDir::cleanPath(tree->rootPath).compare(QDir::cleanPath(d.rootPath), Qt::CaseInsensitive) == 0)
            std::printf("  Windows bao o da dung: %lld (%s) -> chenh %s so voi quet\n", static_cast<long long>(d.usedBytes()),
                        qPrintable(human(d.usedBytes())), qPrintable(human(d.usedBytes() - s.bytes)));

    std::printf("\nTHU MUC CON LON NHAT (cap 1):\n");
    int shown = 0;
    for (const auto& c : tree->root->children)
    {
        if (shown++ >= top)
            break;
        std::printf("  %-34s %12lld  %10s  (tep %10s) %5.1f%%%s\n", qPrintable(c->name.left(34)), static_cast<long long>(c->bytes),
                    qPrintable(human(c->bytes)), qPrintable(human(c->logicalBytes)),
                    tree->root->bytes > 0 ? 100.0 * c->bytes / tree->root->bytes : 0.0, c->unreadable ? "  [khong doc duoc]" : "");
    }
    std::printf("  [tep truc tiep o goc] %lld tep, %s | [%lld thu muc nho] %s\n", static_cast<long long>(tree->root->ownFileCount),
                qPrintable(human(tree->root->ownBytes)), static_cast<long long>(tree->root->prunedFolderCount), qPrintable(human(tree->root->prunedBytes)));

    if (verify > 0)
    {
        // Lấy mẫu ngẫu nhiên (reservoir) N tệp thật rồi so ước tính với AllocationSize thật.
        std::vector<QString> reservoir;
        reservoir.reserve(static_cast<size_t>(verify));
        qint64 seen = 0;
        std::atomic_bool stop{false};
        FsSafety::walkFiles(root, stop, [&](const FsSafety::WalkEntry& e) {
            ++seen;
            if (static_cast<int>(reservoir.size()) < verify)
                reservoir.push_back(e.path);
            else
            {
                const quint64 j = QRandomGenerator::global()->bounded(static_cast<quint64>(seen));
                if (j < static_cast<quint64>(verify))
                    reservoir[static_cast<size_t>(j)] = e.path;
            }
        });

        qint64 cluster = s.clusterBytes;
        qint64 resident = 0;
        {
            wchar_t volume[MAX_PATH + 1] = {};
            wchar_t fsName[MAX_PATH + 1] = {};
            const std::wstring native = QDir::toNativeSeparators(tree->rootPath).toStdWString();
            if (GetVolumePathNameW(native.c_str(), volume, MAX_PATH) &&
                GetVolumeInformationW(volume, nullptr, 0, nullptr, nullptr, nullptr, fsName, MAX_PATH) && std::wstring(fsName) == L"NTFS")
                resident = FolderTreeInternal::kResidentBaseBytes;
        }

        ClassStats normal, resident_, compressed, sparse, wof, cloud, all;
        qint64 altExact[5] = {0, 0, 0, 0, 0}, altSum[5] = {0, 0, 0, 0, 0}, altAbsErr[5] = {0, 0, 0, 0, 0}, actualSum = 0, altFiles = 0, listNotFound = 0;
        qint64 missZero = 0, missOther = 0, zeroListedNonEmpty = 0;
        int unreadable = 0;
        struct Miss { QString path; qint64 logical, est, actual; quint32 attrs; };
        std::vector<Miss> worst, listMiss;
        for (const QString& path : reservoir)
        {
            WIN32_FIND_DATAW fd;
            const HANDLE ff = FindFirstFileExW(extended(path).c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, 0);
            if (ff == INVALID_HANDLE_VALUE)
            {
                ++unreadable;
                continue;
            }
            FindClose(ff);
            const qint64 logical = (static_cast<qint64>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
            qint64 exactSize = -1;
            if (logical > 0 && FolderTreeInternal::needsExactSize(fd.dwFileAttributes, fd.dwReserved0))
            {
                DWORD high = 0;
                const DWORD low = GetCompressedFileSizeW(extended(path).c_str(), &high);
                if (!(low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR))
                    exactSize = (static_cast<qint64>(high) << 32) | low;
            }
            // Mô hình đang dùng trong bộ quét: giới hạn "nằm trong MFT" = 683 - 2 * độ dài tên (xem FolderTreeScanner.cpp).
            const int nameLen = static_cast<int>(wcslen(fd.cFileName));
            const qint64 est = FolderTreeInternal::allocatedBytes(fd.dwFileAttributes, logical, cluster, exactSize,
                                                                  resident > 0 ? FolderTreeInternal::residentLimitFor(nameLen) : 0);
            // Các mô hình thay thế để so sánh: A = hằng số 600 (bản đầu), B = 626 - 2*tên (ổ có tên 8.3), C = 740 - 2*tên (ổ tắt 8.3).
            const qint64 estA = resident > 0 ? FolderTreeInternal::allocatedBytes(fd.dwFileAttributes, logical, cluster, exactSize, 600) : est;
            const qint64 estB = resident > 0 ? FolderTreeInternal::allocatedBytes(fd.dwFileAttributes, logical, cluster, exactSize, qMax<qint64>(0, 626 - 2 * nameLen)) : est;
            const qint64 estC = resident > 0 ? FolderTreeInternal::allocatedBytes(fd.dwFileAttributes, logical, cluster, exactSize, qMax<qint64>(0, 740 - 2 * nameLen)) : est;

            const HANDLE h = CreateFileW(extended(path).c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                         nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            FILE_STANDARD_INFO info{};
            const bool got = h != INVALID_HANDLE_VALUE && GetFileInformationByHandleEx(h, FileStandardInfo, &info, sizeof(info));
            if (h != INVALID_HANDLE_VALUE)
                CloseHandle(h);
            if (!got)
            {
                ++unreadable;
                continue;
            }
            const qint64 actual = info.AllocationSize.QuadPart;
            const quint32 attrs = fd.dwFileAttributes;
            ClassStats* cls = &normal;
            if (attrs & (0x1000 | 0x40000 | 0x400000))
                cls = &cloud;
            else if (attrs & FILE_ATTRIBUTE_SPARSE_FILE)
                cls = &sparse;
            else if (attrs & FILE_ATTRIBUTE_COMPRESSED)
                cls = &compressed;
            else if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) && fd.dwReserved0 == 0x80000017)
                cls = &wof;
            else if (resident > 0 && logical <= FolderTreeInternal::residentLimitFor(nameLen))
                cls = &resident_;
            for (ClassStats* c : {cls, &all})
            {
                ++c->files;
                c->estimated += est;
                c->actual += actual;
                if (est == actual)
                    ++c->exact;
            }
            // Mô hình E: lấy thẳng AllocationSize từ lần liệt kê thư mục (không ước tính).
            const qint64 listed = dirListingAllocation(path);
            const qint64 estE = listed >= 0 ? listed : est;
            if (listed < 0)
                ++listNotFound;
            altExact[0] += estA == actual; altExact[1] += estB == actual; altExact[2] += estC == actual; altExact[3] += est == actual; altExact[4] += estE == actual;
            altSum[0] += estA; altSum[1] += estB; altSum[2] += estC; altSum[3] += est; altSum[4] += estE;
            altAbsErr[0] += qAbs(estA - actual); altAbsErr[1] += qAbs(estB - actual); altAbsErr[2] += qAbs(estC - actual); altAbsErr[3] += qAbs(est - actual); altAbsErr[4] += qAbs(estE - actual);
            if (listed >= 0 && listed != actual)
            {
                listMiss.push_back({path, logical, listed, actual, attrs});
                if (listed == 0 && logical > 0)
                    ++missZero; // "0 byte cấp phát nhưng tệp có dữ liệu" - chỉ những tệp này cần mở riêng để hỏi lại
                else
                    ++missOther;
            }
            if (listed == 0 && logical > 0 && actual > 0)
                ++zeroListedNonEmpty;
            actualSum += actual;
            ++altFiles;
            if (est != actual)
                worst.push_back({path, logical, est, actual, attrs});
        }
        std::sort(worst.begin(), worst.end(), [](const Miss& a, const Miss& b) { return qAbs(a.est - a.actual) > qAbs(b.est - b.actual); });

        const auto row = [](const char* name, const ClassStats& c) {
            if (c.files == 0)
                return;
            std::printf("  %-22s %6lld tep | khop chinh xac %5.1f%% | uoc tinh %12lld | that %12lld | lech %+.2f%%\n", name, static_cast<long long>(c.files),
                        100.0 * c.exact / c.files, static_cast<long long>(c.estimated), static_cast<long long>(c.actual),
                        c.actual > 0 ? 100.0 * (c.estimated - c.actual) / c.actual : 0.0);
        };
        std::printf("\nDOI CHIEU UOC TINH 'CHIEM TREN DIA' VOI AllocationSize THAT (mau %d / %lld tep, bo qua %d tep khong mo duoc):\n", verify,
                    static_cast<long long>(seen), unreadable);
        row("tep thuong", normal);
        row("tep nho (trong MFT)", resident_);
        row("tep nen NTFS", compressed);
        row("tep thua (sparse)", sparse);
        row("tep nen WOF", wof);
        row("tep OneDrive (cloud)", cloud);
        row("TONG", all);
        const char* altNames[5] = {"A: hang so 600", "B: 626 - 2*doDaiTen", "C: 740 - 2*doDaiTen", "D: 683 - 2*ten (uoc tinh)", "E: AllocationSize tu liet ke"};
        std::printf("  (E: %lld tep khong tim thay trong ket qua liet ke thu muc | lech do 'liet ke bao 0 nhung tep co du lieu': %lld | lech kieu khac: %lld | tong so tep co 'liet ke 0 + that > 0': %lld)\n",
                    static_cast<long long>(listNotFound), static_cast<long long>(missZero), static_cast<long long>(missOther), static_cast<long long>(zeroListedNonEmpty));
        for (int k = 0; k < 5 && altFiles > 0; ++k)
            std::printf("  mo hinh %-22s khop chinh xac %5.2f%% | tong uoc tinh lech %+.3f%% | sai so tuyet doi trung binh %.1f byte/tep\n", altNames[k],
                        100.0 * altExact[k] / altFiles, actualSum > 0 ? 100.0 * (altSum[k] - actualSum) / actualSum : 0.0, static_cast<double>(altAbsErr[k]) / altFiles);
        std::sort(listMiss.begin(), listMiss.end(), [](const Miss& a, const Miss& b) { return qAbs(a.est - a.actual) > qAbs(b.est - b.actual); });
        for (size_t i = 0; i < listMiss.size() && i < 6; ++i)
            std::printf("  E lech: %s | thuoc tinh 0x%X | kich thuoc %lld | liet ke %lld | that %lld\n", qPrintable(listMiss[i].path.right(90)),
                        listMiss[i].attrs, static_cast<long long>(listMiss[i].logical), static_cast<long long>(listMiss[i].est), static_cast<long long>(listMiss[i].actual));
        for (size_t i = 0; i < worst.size() && i < 8; ++i)
            std::printf("  lech lon: %s | thuoc tinh 0x%X | kich thuoc %lld | uoc tinh %lld | that %lld\n", qPrintable(worst[i].path.right(90)),
                        worst[i].attrs, static_cast<long long>(worst[i].logical), static_cast<long long>(worst[i].est), static_cast<long long>(worst[i].actual));
    }
    return 0;
}
