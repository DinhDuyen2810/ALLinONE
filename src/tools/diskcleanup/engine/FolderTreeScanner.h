#pragma once

#include <QMetaType>
#include <QString>
#include <QThread>

#include <atomic>
#include <memory>
#include <vector>

/// Một tệp lớn nằm TRỰC TIẾP trong một thư mục (chỉ giữ tệp đủ lớn - xem FolderTreeScanner::setBigFileBytes).
struct FolderFile
{
    QString name;
    qint64 bytes{0};        // chiếm trên đĩa (xấp xỉ "Size on disk" của Explorer)
    qint64 logicalBytes{0}; // kích thước tệp (như "Size" của Explorer)
};

/// Một thư mục trong cây đã quét. Mọi số "tổng" tính cho CẢ cây con.
struct FolderNode
{
    QString name; // nút gốc: đường dẫn gốc đầy đủ ("C:/", "D:/Data"); nút khác: chỉ tên thư mục
    FolderNode* parent{nullptr};

    qint64 bytes{0};        // tổng chiếm trên đĩa của cây con
    qint64 logicalBytes{0}; // tổng kích thước tệp của cây con
    qint64 fileCount{0};    // tổng số tệp của cây con
    qint64 folderCount{0};  // tổng số thư mục con ở mọi cấp (không tính chính nó)

    qint64 ownBytes{0};        // chiếm trên đĩa của các tệp nằm TRỰC TIẾP trong thư mục này
    qint64 ownLogicalBytes{0};
    qint64 ownFileCount{0};
    std::vector<FolderFile> bigFiles; // tệp lớn trực tiếp, giảm dần theo bytes (phần còn lại gộp lại: own* - tổng bigFiles)

    std::vector<std::unique_ptr<FolderNode>> children; // thư mục con ĐƯỢC GIỮ LẠI, giảm dần theo bytes
    qint64 prunedFolderCount{0}; // số thư mục con nhỏ đã gộp (không giữ riêng để tiết kiệm bộ nhớ)
    qint64 prunedBytes{0};       // tổng chiếm trên đĩa của chúng (đã nằm trong bytes ở trên)

    bool unreadable{false}; // không liệt kê được nội dung (thiếu quyền, thư mục biến mất giữa chừng...)

    /// Đường dẫn đầy đủ, dấu '/'.
    QString path() const;
};

struct FolderTreeStats
{
    qint64 files{0};
    qint64 folders{0}; // số thư mục đã duyệt (không tính gốc)
    qint64 bytes{0};   // tổng chiếm trên đĩa
    qint64 logicalBytes{0};
    qint64 unreadableFolders{0};
    qint64 skippedLinks{0}; // junction/symlink bị bỏ qua (không đi vào, không cộng)
    qint64 cloudFiles{0};   // tệp "chỉ có trên đám mây" (OneDrive...) - không chiếm chỗ trên đĩa, tính 0 byte
    qint64 clusterBytes{0}; // kích thước cụm của ổ (chỉ dùng cho phần ước tính dự phòng)
    qint64 estimatedFiles{0}; // số tệp mà dung lượng chiếm trên đĩa là ƯỚC TÍNH (ổ không báo được số thật); 0 = toàn bộ là số thật
    qint64 reopenedFiles{0};  // số tệp phải mở riêng để hỏi lại dung lượng (mục liệt kê báo 0 byte cấp phát dù tệp có dữ liệu)
    qint64 elapsedMs{0};
    qint64 keepFolderBytes{0}; // ngưỡng đã dùng khi gộp thư mục nhỏ (để giao diện ghi rõ "mỗi thư mục < ...")
    qint64 bigFileBytes{0};    // ngưỡng đã dùng khi giữ riêng tệp lớn
};

struct FolderTree
{
    QString rootPath;
    std::unique_ptr<FolderNode> root;
    FolderTreeStats stats;
};
Q_DECLARE_METATYPE(std::shared_ptr<FolderTree>)

namespace FolderTreeInternal
{
/// ƯỚC TÍNH dung lượng một tệp chiếm trên đĩa (xấp xỉ "Size on disk" của Explorer) - đường DỰ PHÒNG, chỉ dùng khi hệ thống
/// tệp không báo dung lượng cấp phát thật lúc liệt kê thư mục (bình thường bộ quét lấy số thật, xem FolderTreeScanner).
/// Tách riêng để test được.
/// - Tệp chỉ có trên đám mây (OFFLINE / RECALL_ON_OPEN / RECALL_ON_DATA_ACCESS): 0.
/// - Tệp nén / thưa / nén kiểu WOF: dùng compressedBytes (từ GetCompressedFileSizeW) nếu có (>= 0), không thì
///   làm tròn lên cụm như tệp thường.
/// - Tệp rỗng: 0. Tệp cực nhỏ (<= residentBytes) nằm gọn trong bản ghi MFT, không chiếm cụm nào: NTFS báo kích thước
///   làm tròn LÊN 8 byte. Chỉ có ở NTFS - ổ FAT/exFAT truyền residentBytes = 0 (mọi tệp không rỗng chiếm ít nhất một cụm).
/// - Còn lại: làm tròn LÊN bội số của cụm.
qint64 allocatedBytes(quint32 attributes, qint64 logicalBytes, qint64 clusterBytes, qint64 compressedBytes,
                      qint64 residentBytes);

/// Tệp này có cần hỏi kích thước thật qua GetCompressedFileSizeW không (nén/thưa/WOF).
bool needsExactSize(quint32 attributes, quint32 reparseTag);

/// Kích thước tệp tối đa còn "nằm gọn trong bản ghi MFT" cho tệp có tên RẤT NGẮN; mỗi ký tự tên tệp (UTF-16) lấy đi 2 byte
/// của bản ghi 1 KB nên giới hạn thật của một tệp là kResidentBaseBytes - 2 * độ dài tên. Đo thật: tên 17 ký tự -> 592 byte,
/// 89 ký tự -> 448 byte (ổ có tên 8.3), ổ tắt tên 8.3 còn dư thêm ~100 byte; 683 là điểm giữa hai trường hợp - xem ghi chú
/// trong .cpp và PROJECT_OVERVIEW. Dùng: allocatedBytes(..., residentBase - 2 * độDàiTên).
constexpr qint64 kResidentBaseBytes = 683;
inline qint64 residentLimitFor(int nameLength) { return qMax<qint64>(0, kResidentBaseBytes - 2 * static_cast<qint64>(nameLength)); }
} // namespace FolderTreeInternal

/**
 * @brief Quét TOÀN BỘ một thư mục gốc (cả ổ C:...) và dựng cây thư mục kèm dung lượng từng nhánh để biết thư mục
 * nào chiếm nhiều chỗ. Chạy trên QThread riêng; CHỈ ĐỌC, không sửa/xóa gì.
 *
 * - "Chiếm trên đĩa" là dung lượng cấp phát THẬT do NTFS báo ngay trong lúc liệt kê thư mục (FileIdBothDirectoryInfo): chính xác
 *   cho tệp nhỏ trong MFT, tệp nén, tệp thưa, tệp CompactOS... không cần ước tính. Tên liên kết cứng có số cũ "0 byte" thì mở
 *   riêng tệp để hỏi lại. Chỉ khi hệ thống tệp không hỗ trợ cách này mới ước tính (stats.estimatedFiles > 0).
 * - Không đi vào junction/symlink thư mục và bỏ qua tệp symlink (cùng quy tắc với FsSafety::walkFiles) nên không
 *   "lạc" sang ổ khác hay đếm trùng; điểm gắn ổ đĩa cũng vậy.
 * - Duyệt bằng ngăn xếp tường minh (không đệ quy) nên cây sâu hàng chục nghìn cấp cũng không làm tràn stack.
 * - Dùng đường dẫn "\\?\" nên vượt được giới hạn 260 ký tự.
 * - Để cây vài trăm nghìn thư mục không ngốn bộ nhớ: thư mục con nhỏ (< keepFolderBytes) được GỘP vào một dòng
 *   tổng ngay khi xong nhánh đó, và mỗi thư mục chỉ giữ riêng vài tệp lớn nhất.
 */
class FolderTreeScanner : public QThread
{
    Q_OBJECT

public:
    explicit FolderTreeScanner(QObject* parent = nullptr);
    ~FolderTreeScanner() override;

    void setRootPath(const QString& path) { m_rootPath = path; }
    /// Tệp lớn hơn ngưỡng này (chiếm trên đĩa) mới được giữ riêng trong bigFiles. Mặc định 1 MiB.
    void setBigFileBytes(qint64 bytes) { m_bigFileBytes = bytes; }
    /// Thư mục con nhỏ hơn ngưỡng này bị gộp vào dòng tổng. Mặc định 128 KiB.
    void setKeepFolderBytes(qint64 bytes) { m_keepFolderBytes = bytes; }
    /// Số tệp lớn giữ lại tối đa mỗi thư mục. Mặc định 25.
    void setMaxBigFilesPerFolder(int n) { m_maxBigFiles = n; }
    /// Chỉ dùng cho test: bỏ qua dung lượng cấp phát thật, ép dùng đường ƯỚC TÍNH dự phòng.
    void setForceEstimateForTest(bool on) { m_forceEstimate = on; }

    /// Bắt đầu quét - LUÔN dùng hàm này thay cho start() trực tiếp (đặt lại cờ dừng trước khi luồng chạy).
    void startScan(QThread::Priority priority = QThread::InheritPriority);
    void requestStop();

signals:
    void progressTick(qint64 files, qint64 folders, qint64 bytes, QString currentFolder);
    void scanFinished(std::shared_ptr<FolderTree> tree);
    void scanStopped();

protected:
    void run() override;

private:
    QString m_rootPath;
    qint64 m_bigFileBytes{1024 * 1024};
    qint64 m_keepFolderBytes{128 * 1024};
    int m_maxBigFiles{25};
    bool m_forceEstimate{false};
    std::atomic_bool m_stopRequested{false};
};
