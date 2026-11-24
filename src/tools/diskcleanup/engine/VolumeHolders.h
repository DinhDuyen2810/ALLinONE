#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>

/**
 * @brief Tìm các chương trình/dịch vụ/thành phần Windows đang GIỮ một ổ (làm FSCTL_LOCK_VOLUME thất bại "Access is denied (mã 5)") và gỡ chúng - cho
 * cửa sổ dời phân vùng (v1.33.0, kiểm KỸ từ v1.33.1).
 *
 * Các bước (đều CHỈ ĐỌC):
 *  1. exe/DLL nạp từ ổ (EnumProcessModulesEx).
 *  2. Toàn bộ handle của hệ thống (NtQuerySystemInformation SystemExtendedHandleInformation) loại "File": nhân bản về rồi hỏi số sê-ri ổ + đường dẫn trên
 *     một luồng phụ có hạn giờ (hỏi một ống dẫn đồng bộ đang chờ đọc có thể treo vô hạn). Không thấy được handle của nhân Windows (PID 4) và của tiến
 *     trình được bảo vệ (csrss, phần mềm diệt virus, vmmem của WSL...) vì không nhân bản được.
 *  3. (v1.33.1) KIỂM KỸ: mở chỉ để đọc thuộc tính (chia sẻ toàn phần - không cản ai, không đọc nội dung) rồi hỏi Windows "PID nào đang mở tệp này"
 *     (NtQueryInformationFile FileProcessIdsUsingFileInformation) - thấy CẢ nhân Windows (PID 4 - ổ ảo VHDX/ISO đang gắn, bản sao bóng, registry
 *     hive...), tiến trình được bảo vệ và máy ảo WSL/Hyper-V. Mỗi lần hỏi ~22 ms (Windows duyệt cả bảng handle) nên chỉ hỏi tệp CÓ KHẢ NĂNG bị giữ:
 *     gốc ổ, tệp ổ ảo/container, System Volume Information, thư mục WSL, tệp ghi trong 24 giờ, tệp CSDL/nhật ký, thư
 *     mục 3 cấp đầu - song song nhiều luồng, tối đa 6000 tệp / 90 giây. Tệp mà ngay cả mở-đọc-thuộc-tính cũng bị từ chối "đang dùng" là tệp nhân
 *     Windows giữ độc quyền (pagefile...).
 *  4. WSL: bản Linux (HKCU\...\Lxss BasePath) nằm trên ổ + máy ảo WSL đang chạy -> một dòng "WSL" dừng bằng `wsl --shutdown`.
 *  5. Dịch vụ theo PID (EnumServicesStatusExW), registry hive nạp từ ổ (HKLM\SYSTEM\CurrentControlSet\Control\hivelist), pagefile/swapfile.
 * Cần quyền Administrator (+ SeDebugPrivilege/SeBackupPrivilege, tự bật) để thấy đủ.
 */
namespace VolumeHolders
{
enum class HolderKind
{
    Process,     // chương trình/dịch vụ thường
    System,      // nhân Windows (PID 4) hoặc thành phần không dừng được từ đây
    Wsl,         // máy ảo WSL đang giữ ổ đĩa ext4.vhdx trên ổ - dừng bằng `wsl --shutdown`
    VirtualDisk, // ổ đĩa ảo (VHD/VHDX/ISO) đang gắn từ một tệp trên ổ - tháo bằng Dismount-DiskImage
};

struct Holder
{
    HolderKind kind{HolderKind::Process};
    quint32 pid{0};
    QString name;              // tên tệp chạy, vd "SearchIndexer.exe"; với WSL/ổ ảo là mô tả
    QString imagePath;         // đường dẫn đầy đủ (nếu đọc được)
    QStringList serviceNames;  // tên dịch vụ (khóa) chạy trong tiến trình này - dừng qua SCM
    QStringList serviceLabels; // "Tên hiển thị (tên dịch vụ)"
    QStringList samplePaths;   // vài tệp/thư mục trên ổ đang mở (tối đa 5)
    int openHandles{0};        // số tệp/thư mục KHÁC NHAU trên ổ đang mở
    bool imageOnDrive{false};  // exe nằm trên ổ
    bool moduleOnDrive{false}; // nạp DLL từ ổ
    bool critical{false};      // tiến trình hệ thống - KHÔNG cho dừng
    bool self{false};          // chính ứng dụng này
    QString hint;              // giải thích vì sao nó giữ ổ / cách gỡ (vd "bản sao bóng (VSS) của ổ")
    QString diskImagePath;     // VirtualDisk: tệp ổ ảo cần tháo

    /// Có thể bấm nút gỡ không.
    bool stoppable() const;
    /// Nhãn nút: "⛔ Dừng" / "⛔ Tắt WSL" / "⏏ Tháo ổ ảo".
    QString stopLabel() const;
    /// Mô tả ngắn cho cột "Đang giữ": "3 tệp/thư mục đang mở: D:\..." / "chạy từ ổ" / "nạp DLL từ ổ" (+ gợi ý).
    QString holdingSummary() const;
};

struct Report
{
    QList<Holder> holders;           // xếp: gỡ được trước, nhiều tệp trước
    QStringList unreadable;          // tiến trình không nhân bản được handle (được bảo vệ / thiếu quyền)
    QStringList notes;               // ghi chú thêm (vd WSL có bản Linux trên ổ nhưng đang tắt)
    bool pagefileOnDrive{false};     // pagefile.sys / swapfile.sys nằm trên ổ
    bool elevated{false};            // quét với quyền Administrator
    bool handleScanComplete{true};   // false nếu quá nhiều truy vấn handle treo phải bỏ dở
    bool deepScanDone{false};        // đã chạy bước kiểm kỹ
    bool deepLimited{false};         // kiểm kỹ chạm trần (số tệp/thời gian) - có thể còn sót
    int entriesListed{0};            // số tệp/thư mục đã liệt kê trên ổ
    int filesChecked{0};             // số tệp/thư mục ĐÃ HỎI "ai đang mở" ở bước kiểm kỹ
    int filesDenied{0};              // số tệp/thư mục không mở được để hỏi (thiếu quyền)
    QString error;                   // lỗi không quét được (ổ không tồn tại...)
};

/// Quét các thứ đang giữ ổ 'driveLetter' ("D", "D:" hoặc "D:\"). deep=true: kiểm thêm TỪNG tệp/thư mục trên ổ (vài giây tới vài chục giây với ổ
/// nhiều tệp). progress(số tệp đã kiểm) được gọi thưa (mỗi ~2000 tệp) từ luồng đang quét. Gọi từ luồng nền.
Report scan(const QString& driveLetter, const std::atomic_bool* stopFlag = nullptr, bool deep = true,
            const std::function<void(int)>& progress = {});

/// Gỡ một thứ đang giữ ổ: dịch vụ -> dừng qua SCM (kèm dịch vụ phụ thuộc); tiến trình thường -> TerminateProcess (kiểm lại exe); WSL -> `wsl --shutdown`;
/// ổ ảo -> Dismount-DiskImage. Từ chối tiến trình hệ thống/chính mình. false + *error nếu không gỡ được.
bool stop(const Holder& holder, QString* error = nullptr);

/// Tên tệp chạy của tiến trình hệ thống KHÔNG BAO GIỜ được dừng (System, smss, csrss, wininit, winlogon, services, lsass, máy ảo Hyper-V...).
bool isCriticalProcessName(const QString& exeName);

/// Giải thích cho một tệp mà NHÂN Windows (PID 4) đang giữ, theo đường dẫn: ổ ảo, bản sao bóng, registry, pagefile... (rỗng nếu không nhận ra).
QString systemHoldHint(const QString& path);
/// Tệp là ổ đĩa ảo gắn được (.vhd, .vhdx, .iso, .avhdx...).
bool isDiskImagePath(const QString& path);

/// PID các tiến trình (kể cả nhân Windows = 4, tiến trình được bảo vệ) đang mở 'path' - một lần hỏi ~22 ms. Không tính chính tiến trình này.
QList<quint32> processesUsingPath(const QString& path);
/// Tên các mục trong thư mục, liệt kê qua handle FILE_FLAG_BACKUP_SEMANTICS (đọc được System Volume Information khi có SeBackupPrivilege).
QStringList directoryEntriesByHandle(const QString& dir);

/// Bật SeDebugPrivilege + SeBackupPrivilege cho tiến trình này (chỉ có tác dụng khi đang chạy với quyền Administrator).
bool enableDebugPrivilege();
} // namespace VolumeHolders
