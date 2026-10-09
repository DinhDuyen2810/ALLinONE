#pragma once

#include <QList>
#include <QString>

/**
 * @brief Liệt kê/đổi kích thước phân vùng đĩa qua các cmdlet PowerShell CHÍNH THỨC của Windows
 * (Get-Partition, Get-PartitionSupportedSize, Resize-Partition) - CÙNG cơ chế an toàn mà Disk
 * Management của Windows dùng cho "Shrink Volume"/"Extend Volume". KHÔNG tự viết thao tác đĩa mức
 * thấp (không đụng tới MBR/GPT, không ghi trực tiếp xuống đĩa) - rủi ro mất dữ liệu nếu tự làm sai
 * là có thật, nên mọi phép tính "kích thước tối thiểu/tối đa an toàn" đều giao hẳn cho Windows tự
 * quyết định qua Get-PartitionSupportedSize.
 *
 * Đổi kích thước phân vùng gần như luôn cần quyền Administrator (Resize-Partition) - dùng
 * isElevated() để kiểm tra trước, không cố âm thầm tự nâng quyền.
 */
namespace PartitionManager
{
struct PartitionInfo
{
    int diskNumber{-1};
    int partitionNumber{-1};
    QString driveLetter;  // "C" hoặc rỗng nếu không có ổ đĩa logic (vd phân vùng Recovery/EFI)
    QString label;
    QString type;         // "Basic", "Recovery", "System"(EFI), "Reserved"(MSR)... theo Get-Partition
    QString fileSystem;   // rỗng nếu không đọc được (phân vùng không có hệ thống tệp Windows hiểu)
    qint64 sizeBytes{0};
    qint64 freeBytes{-1};  // -1 = không xác định
    bool isBoot{false};
    bool isSystem{false};
    bool isActive{false};

    bool hasFileSystem() const { return !fileSystem.isEmpty(); }
};

struct SupportedSizeRange
{
    qint64 minBytes{0};
    qint64 maxBytes{0};
    bool ok{false};
    QString error;
};

/// Tiến trình hiện tại (chính ứng dụng này) có đang chạy với quyền Administrator không - kiểm tra
/// bằng Win32 (OpenProcessToken/TokenElevation), không cần PowerShell.
bool isElevated();

/// Khởi chạy LẠI chính ứng dụng này với quyền Administrator (ShellExecuteW verb "runas" - hiện hộp
/// thoại UAC chuẩn của Windows, người dùng tự xác nhận/từ chối, KHÔNG có gì diễn ra âm thầm). Gọi
/// xong, nếu thành công, nơi gọi tự quyết định có thoát tiến trình hiện tại hay không. Trả về false
/// nếu người dùng bấm "No" trên hộp thoại UAC hoặc có lỗi khác.
bool relaunchElevated(QString* error = nullptr);

/// Danh sách phân vùng thật trên máy (chỉ đọc, an toàn, không cần quyền Administrator).
QList<PartitionInfo> listPartitions(QString* error = nullptr);

/// Kích thước nhỏ nhất/lớn nhất Windows cho phép đổi tới với phân vùng này - đây CHÍNH LÀ phép
/// tính an toàn (tính tới vị trí dữ liệu, vùng hệ thống không thể di chuyển...) mà Disk Management
/// dùng; chỉ đọc, không thay đổi gì, nhưng thường cần quyền Administrator để đọc được.
SupportedSizeRange querySupportedSize(int diskNumber, int partitionNumber, QString* error = nullptr);

/// Đổi kích thước phân vùng THẬT - KHÔNG THỂ HOÀN TÁC dễ dàng nếu có sự cố giữa chừng (mất điện,
/// tắt máy đột ngột...). Chỉ gọi sau khi người dùng xác nhận rõ ràng. Cần quyền Administrator.
bool resizePartition(int diskNumber, int partitionNumber, qint64 newSizeBytes, QString* error = nullptr);

// ---- Lõi thuần (không gọi PowerShell/Win32) - tách riêng để kiểm thử được mà KHÔNG cần đổi gì
// thật trên máy và không cần quyền Administrator. Các hàm listPartitions/querySupportedSize/
// resizePartition ở trên chỉ là: (1) dựng script, (2) chạy qua QProcess, (3) parse JSON trả về -
// các bước (1) và (3) nằm ở đây, thuần túy xử lý chuỗi/JSON, test bằng dữ liệu JSON mẫu dựng sẵn. ----
namespace internal
{
QString buildListPartitionsScript();
QString buildSupportedSizeScript(int diskNumber, int partitionNumber);
QString buildResizeScript(int diskNumber, int partitionNumber, qint64 newSizeBytes);

/// PowerShell 5.1 (Windows PowerShell mặc định trên Win10/11) KHÔNG có `ConvertTo-Json -AsArray`:
/// khi chỉ có 1 phân vùng, kết quả là một OBJECT JSON đơn, không phải mảng 1 phần tử - hàm này
/// chuẩn hóa cả hai trường hợp về cùng một danh sách.
QList<PartitionInfo> parsePartitionsJson(const QByteArray& json, QString* error);
SupportedSizeRange parseSupportedSizeJson(const QByteArray& json, QString* error);

/// Đổi giá trị GB người dùng nhập sang byte rồi KẸP vào [minBytes, maxBytes]. Ô nhập chỉ có 2 chữ số
/// thập phân (bước ~10,7 MB) nên giá trị làm tròn có thể lọt ra ngoài khoảng Windows cho phép vài MB.
qint64 clampResizeBytes(double sizeGb, const SupportedSizeRange& range);

/// Lớp kiểm tra CUỐI CÙNG trước khi chạy Resize-Partition, làm trên dữ liệu VỪA đọc lại từ Windows
/// (currentPartitions/currentRange), không phải dữ liệu giao diện đang giữ: rỗng nếu được phép chạy,
/// ngược lại là lý do từ chối. Từ chối khi phân vùng (disk, partition) không còn, kích thước hiện tại
/// đã khác kích thước lúc người dùng tra và xác nhận (sizeAtQueryBytes) - tức số đĩa/số phân vùng có
/// thể đã trỏ sang một phân vùng KHÁC (cắm/rút ổ, đổi phân vùng bằng công cụ khác) - hoặc kích thước
/// mới nằm ngoài khoảng cho phép/không đổi gì.
QString resizeBlockReason(int diskNumber, int partitionNumber, qint64 sizeAtQueryBytes,
                          const QList<PartitionInfo>& currentPartitions, const SupportedSizeRange& currentRange,
                          qint64 newSizeBytes);
} // namespace internal
} // namespace PartitionManager
