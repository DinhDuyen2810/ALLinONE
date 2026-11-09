#include "FolderTreeScanner.h"

#include <QDir>
#include <QElapsedTimer>

#include <algorithm>
#include <functional>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#endif

QString FolderNode::path() const
{
    // Đi LÊN bằng vòng lặp (không đệ quy): cây có thể sâu hàng chục nghìn cấp.
    QStringList parts;
    for (const FolderNode* n = this; n; n = n->parent)
        parts.prepend(n->name);
    QString out = parts.takeFirst();
    for (const QString& p : parts)
        out += (out.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + p;
    return out;
}

namespace FolderTreeInternal
{
namespace
{
constexpr quint32 kAttrReparse = 0x400;
constexpr quint32 kAttrSparse = 0x200;
constexpr quint32 kAttrCompressed = 0x800;
constexpr quint32 kAttrOffline = 0x1000;
constexpr quint32 kAttrRecallOnOpen = 0x40000;
constexpr quint32 kAttrRecallOnDataAccess = 0x400000;
constexpr quint32 kReparseTagWof = 0x80000017; // Windows Overlay Filter: tệp nén kiểu CompactOS
} // namespace

bool needsExactSize(quint32 attributes, quint32 reparseTag)
{
    if (attributes & (kAttrSparse | kAttrCompressed))
        return true;
    return (attributes & kAttrReparse) && reparseTag == kReparseTagWof;
}

// ƯỚC TÍNH dung lượng chiếm trên đĩa - chỉ dùng khi ổ không báo được dung lượng cấp phát thật trong lúc liệt kê thư mục
// (xem Walker::listDirectoryInfo). Trên NTFS bộ quét lấy số THẬT nên hàm này gần như không chạy.
//
// Tệp "nằm gọn trong MFT": NTFS lưu nội dung tệp rất nhỏ ngay trong bản ghi MFT (1 KB/bản ghi) nên chúng không chiếm
// cụm nào; FileStandardInfo.AllocationSize của chúng là kích thước tệp làm tròn LÊN 8 byte (đo thật: tệp 1 byte -> 8,
// 100 -> 104, 400 -> 400), không phải 0 và cũng không phải một cụm. Giới hạn kích thước còn "resident" phụ thuộc độ dài
// tên tệp (mỗi ký tự lấy 2 byte của bản ghi) và việc ổ có tạo tên 8.3 hay không: thực nghiệm trên ổ C: (có 8.3) cho
// 592 byte với tên 17 ký tự, 568 với 29, 528 với 49, 448 với 89 (= 626 - 2*độ dài); ổ D: (tắt 8.3) còn ~740 - 2*độ dài.
// Ước tính dùng 683 - 2*độ dài (điểm giữa).
qint64 allocatedBytes(quint32 attributes, qint64 logicalBytes, qint64 clusterBytes, qint64 compressedBytes,
                      qint64 residentBytes)
{
    if (attributes & (kAttrOffline | kAttrRecallOnOpen | kAttrRecallOnDataAccess))
        return 0; // chỉ có trên đám mây, chưa tải về máy
    if (logicalBytes <= 0)
        return 0;
    if (compressedBytes >= 0)
        return compressedBytes;
    if (logicalBytes <= residentBytes)
        return (logicalBytes + 7) & ~static_cast<qint64>(7);
    if (clusterBytes <= 1)
        return logicalBytes;
    return ((logicalBytes + clusterBytes - 1) / clusterBytes) * clusterBytes;
}
} // namespace FolderTreeInternal

#ifdef Q_OS_WIN
namespace
{
constexpr DWORD kNameSurrogateBit = 0x20000000; // IsReparseTagNameSurrogate()
constexpr DWORD kCloudAttributes = 0x1000 | 0x40000 | 0x400000; // OFFLINE | RECALL_ON_OPEN | RECALL_ON_DATA_ACCESS

/// "\\?\C:\..." - vượt giới hạn MAX_PATH. Đầu vào đã ở dạng gốc Windows (dấu '\').
std::wstring extended(const std::wstring& nativePath)
{
    if (nativePath.compare(0, 4, L"\\\\?\\") == 0)
        return nativePath;
    if (nativePath.compare(0, 2, L"\\\\") == 0)
        return L"\\\\?\\UNC\\" + nativePath.substr(2);
    return L"\\\\?\\" + nativePath;
}

qint64 compressedSizeOf(const std::wstring& extendedFilePath)
{
    DWORD high = 0;
    const DWORD low = GetCompressedFileSizeW(extendedFilePath.c_str(), &high);
    if (low == INVALID_FILE_SIZE && GetLastError() != NO_ERROR)
        return -1;
    return (static_cast<qint64>(high) << 32) | static_cast<qint64>(low);
}

/// Dung lượng cấp phát THẬT của một tệp (FileStandardInfo.AllocationSize) - mở riêng tệp đó, không đọc dữ liệu. -1 nếu không hỏi được.
qint64 allocationOf(const std::wstring& extendedFilePath)
{
    const HANDLE h = CreateFileW(extendedFilePath.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                 nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return -1;
    FILE_STANDARD_INFO info{};
    const bool ok = GetFileInformationByHandleEx(h, FileStandardInfo, &info, sizeof(info)) != 0;
    CloseHandle(h);
    return ok ? info.AllocationSize.QuadPart : -1;
}

/// Trạng thái của MỘT lượt quét: duyệt bằng ngăn xếp tường minh, hoàn tất (cộng dồn + gộp nhánh nhỏ + sắp xếp) từng
/// thư mục ngay khi xong nhánh của nó nên bộ nhớ luôn xấp xỉ "phần được giữ lại + đường đang đi".
class Walker
{
public:
    using ProgressFn = std::function<void(const FolderTreeStats&, const std::wstring&)>;

    Walker(const std::atomic_bool& stop, qint64 bigBytes, qint64 keepBytes, int maxBigFiles, qint64 cluster,
           qint64 resident, ProgressFn progress)
        : m_stop(stop), m_bigBytes(bigBytes), m_keepBytes(keepBytes), m_maxBig(maxBigFiles), m_cluster(cluster),
          m_resident(resident), m_progress(std::move(progress)), m_buffer(64 * 1024)
    {
        m_tick.start();
        stats.clusterBytes = cluster;
    }

    /// false nếu bị dừng giữa chừng.
    bool run(FolderNode* root, const std::wstring& rootPath)
    {
        std::vector<Frame> stack;
        stack.push_back(makeFrame(root, rootPath));
        while (!stack.empty())
        {
            if (m_stop.load())
                return false;
            Frame& top = stack.back();
            if (top.next < top.subDirs.size())
            {
                const std::wstring sub = std::move(top.subDirs[top.next++]);
                auto child = std::make_unique<FolderNode>();
                child->name = QString::fromStdWString(sub);
                child->parent = top.node;
                FolderNode* raw = child.get();
                top.node->children.push_back(std::move(child));
                ++stats.folders;
                const std::wstring childPath = (top.path.back() == L'\\' ? top.path : top.path + L'\\') + sub;
                Frame f = makeFrame(raw, childPath); // KHÔNG giữ tham chiếu top sau push_back
                stack.push_back(std::move(f));
            }
            else
            {
                finalize(top.node);
                stack.pop_back();
            }
        }
        return !m_stop.load();
    }

    /// Chỉ dùng cho test: ép dùng đường ước tính dự phòng.
    void forceEstimate() { m_noDirectoryInfo = true; }

    FolderTreeStats stats;

private:
    struct Frame
    {
        FolderNode* node{nullptr};
        std::wstring path; // dạng gốc Windows, không có tiền tố "\\?\"
        std::vector<std::wstring> subDirs;
        size_t next{0};
    };

    Frame makeFrame(FolderNode* node, const std::wstring& path)
    {
        Frame f;
        f.node = node;
        f.path = path;
        list(f);
        return f;
    }

    /// Liệt kê một thư mục. Ưu tiên FileIdBothDirectoryInfo (trả luôn dung lượng cấp phát THẬT của từng mục); ổ/hệ thống
    /// tệp không hỗ trợ thì rơi về FindFirstFileEx + ước tính.
    void list(Frame& f)
    {
        const std::wstring base = f.path.back() == L'\\' ? f.path : f.path + L'\\';
        if (!m_noDirectoryInfo && listDirectoryInfo(f, base))
            return;
        listFindFile(f, base);
    }

    /// false = không dùng được cách này cho thư mục này (chưa xử lý mục nào) -> người gọi thử cách dự phòng.
    bool listDirectoryInfo(Frame& f, const std::wstring& base)
    {
        const HANDLE h = CreateFileW(extended(base).c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            return false; // thiếu quyền/không mở được: cách dự phòng sẽ báo "không đọc được" đúng cách
        bool first = true;
        bool fell = false;
        for (;;)
        {
            if (m_stop.load())
                break;
            if (!GetFileInformationByHandleEx(h, first ? FileIdBothDirectoryRestartInfo : FileIdBothDirectoryInfo, m_buffer.data(),
                                              static_cast<DWORD>(m_buffer.size())))
            {
                const DWORD err = GetLastError();
                if (err == ERROR_NO_MORE_FILES || err == ERROR_FILE_NOT_FOUND)
                    break; // hết thư mục
                if (first && (err == ERROR_INVALID_PARAMETER || err == ERROR_NOT_SUPPORTED || err == ERROR_INVALID_FUNCTION || err == ERROR_INVALID_LEVEL))
                {
                    m_noDirectoryInfo = true; // hệ thống tệp này không hỗ trợ - lần sau khỏi thử
                    fell = true;
                }
                else if (first)
                    fell = true; // lỗi ngay lần đầu: để cách dự phòng thử/báo
                else
                {
                    f.node->unreadable = true; // lỗi giữa chừng: phần đã đọc vẫn tính, đánh dấu để người dùng biết số có thể thiếu
                    ++stats.unreadableFolders;
                }
                break;
            }
            first = false;
            const char* p = m_buffer.data();
            for (;;)
            {
                const auto* e = reinterpret_cast<const FILE_ID_BOTH_DIR_INFO*>(p);
                const size_t nameLen = e->FileNameLength / sizeof(wchar_t);
                const bool dots = (nameLen == 1 && e->FileName[0] == L'.') || (nameLen == 2 && e->FileName[0] == L'.' && e->FileName[1] == L'.');
                if (!dots)
                {
                    const DWORD attrs = e->FileAttributes;
                    // Với reparse point, EaSize mang theo thẻ reparse (tài liệu của FILE_ID_BOTH_DIR_INFO).
                    const DWORD tag = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) ? e->EaSize : 0;
                    handleEntry(f, base, e->FileName, nameLen, attrs, tag, e->EndOfFile.QuadPart, e->AllocationSize.QuadPart);
                }
                if (e->NextEntryOffset == 0)
                    break;
                p += e->NextEntryOffset;
            }
        }
        CloseHandle(h);
        return !fell;
    }

    void listFindFile(Frame& f, const std::wstring& base)
    {
        WIN32_FIND_DATAW data;
        const HANDLE h = FindFirstFileExW(extended(base + L"*").c_str(), FindExInfoBasic, &data, FindExSearchNameMatch,
                                          nullptr, FIND_FIRST_EX_LARGE_FETCH);
        if (h == INVALID_HANDLE_VALUE)
        {
            f.node->unreadable = true;
            ++stats.unreadableFolders;
            return;
        }
        do
        {
            if (m_stop.load())
                break;
            const wchar_t* name = data.cFileName;
            if (name[0] == L'.' && (name[1] == 0 || (name[1] == L'.' && name[2] == 0)))
                continue;
            const qint64 logical = (static_cast<qint64>(data.nFileSizeHigh) << 32) | static_cast<qint64>(data.nFileSizeLow);
            handleEntry(f, base, name, wcslen(name), data.dwFileAttributes, data.dwReserved0, logical, -1);
        } while (FindNextFileW(h, &data));
        FindClose(h);
    }

    /// Một mục của thư mục. allocation < 0: chưa biết (đường dự phòng) -> ước tính.
    void handleEntry(Frame& f, const std::wstring& base, const wchar_t* name, size_t nameLen, DWORD attrs, DWORD reparseTag,
                     qint64 logical, qint64 allocation)
    {
        if ((++m_seen & 0xFF) == 0 && m_tick.elapsed() >= 120)
        {
            m_tick.restart();
            m_progress(stats, f.path);
        }
        const bool link = (attrs & FILE_ATTRIBUTE_REPARSE_POINT) && (reparseTag & kNameSurrogateBit);
        if (attrs & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (link)
                ++stats.skippedLinks; // junction/symlink thư mục/điểm gắn ổ: KHÔNG đi vào
            else
                f.subDirs.emplace_back(name, nameLen);
            return;
        }
        if (link)
        {
            ++stats.skippedLinks;
            return;
        }

        qint64 bytes = 0;
        if (attrs & kCloudAttributes)
        {
            ++stats.cloudFiles; // chỉ có trên đám mây (OneDrive...): không chiếm chỗ trên đĩa
        }
        else if (allocation >= 0)
        {
            bytes = allocation;
            if (allocation == 0 && logical > 0)
            {
                // Mục thư mục báo "0 byte cấp phát" cho tệp có dữ liệu: gặp ở tên liên kết cứng (hard link) có số cũ trong chỉ mục
                // NTFS (đo thật: tệp .pak của Edge). Hỏi riêng tệp này; không hỏi được thì ước tính.
                ++stats.reopenedFiles;
                const std::wstring full = extended(base + std::wstring(name, nameLen));
                const qint64 exact = allocationOf(full);
                if (exact >= 0)
                    bytes = exact;
                else
                {
                    ++stats.estimatedFiles;
                    bytes = FolderTreeInternal::allocatedBytes(attrs, logical, m_cluster, -1,
                                                               m_resident > 0 ? FolderTreeInternal::residentLimitFor(static_cast<int>(nameLen)) : 0);
                }
            }
        }
        else
        {
            ++stats.estimatedFiles;
            qint64 exact = -1;
            if (logical > 0 && FolderTreeInternal::needsExactSize(attrs, reparseTag))
                exact = compressedSizeOf(extended(base + std::wstring(name, nameLen)));
            bytes = FolderTreeInternal::allocatedBytes(attrs, logical, m_cluster, exact,
                                                       m_resident > 0 ? FolderTreeInternal::residentLimitFor(static_cast<int>(nameLen)) : 0);
        }

        FolderNode* node = f.node;
        node->ownBytes += bytes;
        node->ownLogicalBytes += logical;
        ++node->ownFileCount;
        ++stats.files;
        stats.bytes += bytes;
        if (bytes >= m_bigBytes && bytes > 0)
        {
            FolderFile ff;
            ff.name = QString::fromWCharArray(name, static_cast<int>(nameLen));
            ff.bytes = bytes;
            ff.logicalBytes = logical;
            node->bigFiles.push_back(std::move(ff));
            // Giữ vừa phải: cắt khi gấp đôi giới hạn (chỉ cần đúng các tệp lớn nhất ở cuối).
            if (m_maxBig > 0 && node->bigFiles.size() > static_cast<size_t>(m_maxBig) * 2)
                trimBigFiles(node);
        }
    }

    void trimBigFiles(FolderNode* n) const
    {
        std::sort(n->bigFiles.begin(), n->bigFiles.end(),
                  [](const FolderFile& a, const FolderFile& b) { return a.bytes > b.bytes; });
        if (m_maxBig >= 0 && n->bigFiles.size() > static_cast<size_t>(m_maxBig))
            n->bigFiles.resize(static_cast<size_t>(m_maxBig));
        n->bigFiles.shrink_to_fit();
    }

    void finalize(FolderNode* n) const
    {
        n->bytes = n->ownBytes;
        n->logicalBytes = n->ownLogicalBytes;
        n->fileCount = n->ownFileCount;
        n->folderCount = 0;
        for (const auto& c : n->children)
        {
            n->bytes += c->bytes;
            n->logicalBytes += c->logicalBytes;
            n->fileCount += c->fileCount;
            n->folderCount += 1 + c->folderCount;
        }
        // Gộp thư mục con nhỏ (giữ lại thư mục không đọc được để người dùng thấy chúng).
        const auto mid = std::stable_partition(n->children.begin(), n->children.end(), [&](const auto& c) {
            return c->bytes >= m_keepBytes || c->unreadable;
        });
        for (auto it = mid; it != n->children.end(); ++it)
        {
            ++n->prunedFolderCount;
            n->prunedBytes += (*it)->bytes;
        }
        n->children.erase(mid, n->children.end());
        std::sort(n->children.begin(), n->children.end(), [](const auto& a, const auto& b) {
            return a->bytes != b->bytes ? a->bytes > b->bytes : a->name.compare(b->name, Qt::CaseInsensitive) < 0;
        });
        n->children.shrink_to_fit();
        trimBigFiles(n);
    }

    const std::atomic_bool& m_stop;
    qint64 m_bigBytes;
    qint64 m_keepBytes;
    int m_maxBig;
    qint64 m_cluster;
    qint64 m_resident;
    ProgressFn m_progress;
    std::vector<char> m_buffer;
    QElapsedTimer m_tick;
    quint32 m_seen{0};
    bool m_noDirectoryInfo{false};
};
} // namespace
#endif

FolderTreeScanner::FolderTreeScanner(QObject* parent)
    : QThread(parent)
{
    qRegisterMetaType<std::shared_ptr<FolderTree>>("std::shared_ptr<FolderTree>");
}

FolderTreeScanner::~FolderTreeScanner()
{
    // Chờ KHÔNG giới hạn - xem CleanupScanner::~CleanupScanner (cờ dừng được kiểm tra ở từng mục).
    requestStop();
    wait();
}

void FolderTreeScanner::requestStop()
{
    m_stopRequested = true;
}

void FolderTreeScanner::startScan(QThread::Priority priority)
{
    if (isRunning())
        return;
    m_stopRequested = false;
    start(priority);
}

void FolderTreeScanner::run()
{
    QElapsedTimer clock;
    clock.start();
    auto tree = std::make_shared<FolderTree>();
    tree->rootPath = m_rootPath.trimmed().isEmpty() ? QString() : QDir::cleanPath(QDir(m_rootPath).absolutePath());
    tree->root = std::make_unique<FolderNode>();
    tree->root->name = tree->rootPath;

#ifdef Q_OS_WIN
    if (tree->rootPath.isEmpty())
    {
        tree->root->unreadable = true;
    }
    else
    {
        const std::wstring nativeRoot = QDir::toNativeSeparators(tree->rootPath).toStdWString();

        // Kích thước cụm + hệ thống tệp của ổ chứa gốc quét - chỉ dùng cho phần ƯỚC TÍNH dự phòng (ổ không báo được dung
        // lượng cấp phát thật); không hỏi được thì 4 KiB.
        qint64 cluster = 4096;
        qint64 resident = 0;
        wchar_t volume[MAX_PATH + 1] = {};
        if (GetVolumePathNameW(nativeRoot.c_str(), volume, MAX_PATH))
        {
            DWORD sectors = 0, bytesPerSector = 0;
            if (GetDiskFreeSpaceW(volume, &sectors, &bytesPerSector, nullptr, nullptr) && sectors > 0 && bytesPerSector > 0)
                cluster = static_cast<qint64>(sectors) * bytesPerSector;
            wchar_t fsName[MAX_PATH + 1] = {};
            if (GetVolumeInformationW(volume, nullptr, 0, nullptr, nullptr, nullptr, fsName, MAX_PATH) &&
                std::wstring(fsName) == L"NTFS")
                resident = FolderTreeInternal::kResidentBaseBytes; // > 0 = ổ NTFS (giới hạn thật tính theo độ dài tên từng tệp)
        }

        Walker walker(m_stopRequested, m_bigFileBytes, m_keepFolderBytes, m_maxBigFiles, cluster, resident,
                      [this](const FolderTreeStats& s, const std::wstring& dir) {
                          emit progressTick(s.files, s.folders, s.bytes, QString::fromStdWString(dir));
                      });
        if (m_forceEstimate)
            walker.forceEstimate();
        if (!walker.run(tree->root.get(), nativeRoot))
        {
            emit scanStopped();
            return;
        }
        tree->stats = walker.stats;
        tree->stats.files = tree->root->fileCount;
        tree->stats.folders = tree->root->folderCount;
        tree->stats.bytes = tree->root->bytes;
        tree->stats.logicalBytes = tree->root->logicalBytes;
    }
#else
    tree->root->unreadable = true;
#endif

    if (m_stopRequested)
    {
        emit scanStopped();
        return;
    }
    tree->stats.elapsedMs = clock.elapsed();
    tree->stats.keepFolderBytes = m_keepFolderBytes;
    tree->stats.bigFileBytes = m_bigFileBytes;
    emit scanFinished(tree);
}
