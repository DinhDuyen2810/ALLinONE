#pragma once

#include <QByteArray>
#include <QString>

#include <atomic>
#include <functional>

/**
 * @brief Lõi THUẦN (không Win32, không PowerShell, không widget) của việc DỜI một phân vùng trên đĩa: chép từng khối dữ liệu
 * từ vị trí cũ sang vị trí mới (chồng lấn được, theo đúng chiều), có NHẬT KÝ phục hồi sau sự cố, kiểm tra đọc lại từng
 * khối và vá "Hidden sectors" của boot sector NTFS. Chạy trên một thiết bị khối trừu tượng (IBlockDevice) nên test được
 * bằng tệp thường - kể cả "giả lập sự cố" ở MỌI điểm (ghi dở một khối, mất nhật ký, rớt giữa chừng) rồi chạy tiếp.
 *
 * Vì sao an toàn khi mất điện giữa chừng (bất biến cốt lõi): mỗi "đơn vị" được chép (đọc nguồn -> ghi đích -> flush -> ghi nhật ký)
 * có độ dài <= khoảng dời |đích - nguồn| khi hai vùng chồng nhau. Khi đó vùng ghi của một đơn vị KHÔNG đè lên vùng đọc của chính
 * nó, cũng không đè lên nguồn của các đơn vị chưa chép -> chép lại một đơn vị (sau khi mất điện giữa lúc ghi) luôn đọc được dữ
 * liệu nguồn nguyên vẹn và cho đúng kết quả cũ (lũy đẳng). Nhật ký chỉ cần nhớ "đã xong tới đâu"; nhật ký lỗi thời hơn một nhóm
 * đơn vị vẫn đúng vì nhóm đó cũng lũy đẳng (xem saveIntervalBytes). Dời sang vùng KHÔNG chồng lấn thì nguồn còn nguyên tới tận khi
 * bảng phân vùng được đổi - hủy giữa chừng không mất gì.
 */
namespace PartitionMove
{
constexpr qint64 kMiB = 1024LL * 1024;

class IBlockDevice
{
public:
    virtual ~IBlockDevice() = default;
    virtual qint64 sectorSize() const = 0;
    /// offset/length là bội số của sectorSize(); buffer căn lề 4096.
    virtual bool read(qint64 offset, char* buffer, qint64 length, QString* error) = 0;
    virtual bool write(qint64 offset, const char* buffer, qint64 length, QString* error) = 0;
    /// Bảo đảm mọi thứ đã ghi nằm trên phương tiện lưu trữ (trước khi cập nhật nhật ký).
    virtual bool flush(QString* error) = 0;
};

/// Trạng thái của một lần dời - được lưu nguyên xi vào tệp nhật ký (JSON) để chạy tiếp sau sự cố.
struct JournalState
{
    enum Phase
    {
        Copying = 0, // đang chép dữ liệu (doneBytes cho biết tới đâu)
        Copied = 1,  // chép + kiểm tra xong, CHƯA đổi bảng phân vùng
        Patched = 2, // đã vá boot sector tại vị trí mới, CHƯA đổi bảng phân vùng
        TableDone = 3 // bảng phân vùng đã trỏ tới vị trí mới
    };

    int version{1};
    int diskNumber{-1};
    QString diskId;       // GUID đĩa (GPT) hoặc chữ ký đĩa (MBR); rỗng nếu không biết
    QString partitionId;  // GUID phân vùng (GPT) hoặc rỗng (MBR)
    int partitionNumber{-1};
    qint64 srcOffset{0};  // vị trí bắt đầu CŨ (byte từ đầu đĩa)
    qint64 dstOffset{0};  // vị trí bắt đầu MỚI
    qint64 length{0};     // số byte dữ liệu cần dời (độ dài phân vùng tại lúc dời)
    qint64 unitBytes{0};  // đơn vị lũy đẳng (xem safeUnitBytes)
    qint64 doneBytes{0};  // đã chép xong (theo thứ tự xử lý: tiến từ đầu hoặc lùi từ cuối)
    qint64 sectorBytes{512};
    int phase{Copying};
    QString startedAt;
    QString note;         // mô tả tự do cho người đọc nhật ký

    QByteArray toJson() const;
    /// false nếu không phải nhật ký hợp lệ (JSON hỏng, thiếu trường, số vô lý) - khi đó tuyệt đối KHÔNG dùng.
    static bool fromJson(const QByteArray& json, JournalState* out, QString* error = nullptr);

    /// Chép theo chiều tăng (đích < nguồn, hoặc hai vùng không chồng) hay lùi từ cuối (đích > nguồn và chồng nhau).
    bool backward() const { return dstOffset > srcOffset && (dstOffset - srcOffset) < length; }
    bool overlapping() const { return (dstOffset > srcOffset ? dstOffset - srcOffset : srcOffset - dstOffset) < length; }
};

class IJournalStore
{
public:
    virtual ~IJournalStore() = default;
    /// Ghi BỀN VỮNG (flush xuống đĩa) trạng thái. false = không ghi được -> việc chép phải dừng ngay.
    virtual bool save(const JournalState& state, QString* error) = 0;
};

/// Kiểm tra tham số của một lần dời; rỗng nếu hợp lệ. (Chưa kiểm vùng đích có trống không - việc của bên gọi, xem DiskLayout.)
QString validateMove(qint64 srcOffset, qint64 dstOffset, qint64 length, qint64 sectorBytes);

/// Đơn vị chép lũy đẳng: min(chunkBytes, |đích - nguồn|) làm tròn xuống bội sector khi hai vùng chồng nhau; = chunkBytes (làm tròn
/// xuống sector, tối thiểu 1 sector) khi không chồng.
qint64 safeUnitBytes(qint64 srcOffset, qint64 dstOffset, qint64 length, qint64 chunkBytes, qint64 sectorBytes);

/// Sau bao nhiêu byte thì flush + ghi nhật ký một lần (luôn là bội của unit và, khi chồng lấn, <= khoảng dời nên cả nhóm lũy đẳng).
qint64 saveIntervalBytes(qint64 srcOffset, qint64 dstOffset, qint64 length, qint64 unitBytes);

struct CopyOptions
{
    qint64 chunkBytes{16 * kMiB};
    bool verifyReadback{true}; // đọc lại vùng vừa ghi và so với dữ liệu đã đọc từ nguồn
};

enum class CopyResult
{
    Done,
    Cancelled, // dừng theo yêu cầu - nhật ký giữ đúng chỗ dừng, chạy tiếp được
    Failed     // lỗi đọc/ghi/kiểm tra/nhật ký - xem error; nhật ký giữ chỗ an toàn gần nhất
};

struct CopyOutcome
{
    CopyResult result{CopyResult::Failed};
    QString error;
    qint64 doneBytes{0};
};

/// Chép [srcOffset, +length) sang dstOffset từ chỗ doneBytes của 'state' (state mới = 0). Cập nhật state.doneBytes + lưu qua
/// 'journal' (state.unitBytes được điền nếu còn 0). Kết thúc Done: state.doneBytes == length, nhật ký đã lưu với phase = Copied.
CopyOutcome copyRange(IBlockDevice& device, JournalState& state, IJournalStore& journal, const CopyOptions& options,
                      const std::atomic_bool& cancel, const std::function<void(qint64 done, qint64 total)>& progress = {});

// ---- boot sector NTFS ----

/// Vá trường "Hidden sectors" (offset 0x1C, 4 byte) của một boot sector NTFS thành startLba (không đổi nếu startLba > 0xFFFFFFFF).
/// false nếu không phải boot sector NTFS ("NTFS    " ở offset 3) hoặc buffer quá ngắn.
bool patchNtfsHiddenSectors(QByteArray* sector, quint64 startLba);

/// Đọc "Hidden sectors" của boot sector; -1 nếu không phải NTFS.
qint64 readNtfsHiddenSectors(const QByteArray& sector);

/// Vá boot sector chính (sector đầu) và bản sao lưu (sector CUỐI của phân vùng) tại vị trí MỚI. Bản sao lưu không phải NTFS thì
/// bỏ qua (không phải lỗi). false nếu không đọc/ghi được hoặc sector đầu không phải NTFS.
bool patchNtfsBootSectorsAt(IBlockDevice& device, qint64 dstOffset, qint64 length, QString* error);
} // namespace PartitionMove
