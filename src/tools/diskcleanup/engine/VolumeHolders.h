#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <atomic>

/**
 * @brief Tìm các chương trình/dịch vụ đang GIỮ một ổ (làm FSCTL_LOCK_VOLUME thất bại "Access is denied (mã 5)") và dừng chúng - cho cửa sổ dời
 * phân vùng (v1.33.0). Bản cũ chỉ dò "exe nằm trên ổ" + "thư mục làm việc trên ổ", nên ổ bị giữ bởi một TỆP ĐANG MỞ (trình đánh chỉ mục, phần mềm
 * diệt virus/đồng bộ, Explorer, dịch vụ...) thì báo lỗi mà không chỉ ra ai.
 *
 * Cách dò (giống Process Explorer / handle.exe, CHỈ ĐỌC):
 *  1. Toàn bộ handle của hệ thống (NtQuerySystemInformation SystemExtendedHandleInformation), lọc loại "File"; với từng handle của tiến trình khác:
 *     nhân bản về tiến trình này (DuplicateHandle) rồi hỏi số sê-ri ổ (GetVolumeInformationByHandleW) + đường dẫn (GetFinalPathNameByHandleW) - các
 *     truy vấn chạy trên một luồng phụ có HẠN GIỜ: hỏi một handle ống dẫn (pipe) đồng bộ đang chờ đọc có thể treo vô hạn, luồng treo bị bỏ lại.
 *  2. exe/DLL nạp từ ổ (EnumProcessModulesEx) - ảnh thực thi không giữ handle nhưng vẫn giữ ổ.
 *  3. Dịch vụ Windows chạy trong từng tiến trình (EnumServicesStatusExW) để hiện tên dịch vụ và dừng ĐÚNG cách (qua Service Control Manager).
 *  4. Tệp hoán trang (pagefile.sys/swapfile.sys) trên ổ - chỉ gỡ được bằng cách tắt bộ nhớ ảo trên ổ đó rồi khởi động lại.
 * Cần quyền Administrator (+ SeDebugPrivilege, tự bật nếu có) để thấy tiến trình của tài khoản khác và dịch vụ; tiến trình được bảo vệ (vd phần mềm
 * diệt virus) không đọc được - được liệt kê riêng.
 */
namespace VolumeHolders
{
struct Holder
{
    quint32 pid{0};
    QString name;              // tên tệp chạy, vd "SearchIndexer.exe"
    QString imagePath;         // đường dẫn đầy đủ (nếu đọc được)
    QStringList serviceNames;  // tên dịch vụ (khóa) chạy trong tiến trình này - dừng qua SCM
    QStringList serviceLabels; // "Tên hiển thị (tên dịch vụ)"
    QStringList samplePaths;   // vài tệp/thư mục trên ổ đang mở (tối đa 5)
    int openHandles{0};        // số handle tệp/thư mục trên ổ
    bool imageOnDrive{false};  // exe nằm trên ổ
    bool moduleOnDrive{false}; // nạp DLL từ ổ
    bool critical{false};      // tiến trình hệ thống - KHÔNG cho dừng
    bool self{false};          // chính ứng dụng này

    /// Có thể bấm Dừng không (không phải tiến trình hệ thống/chính mình).
    bool stoppable() const { return !critical && !self && pid > 4; }
    /// Mô tả ngắn cho cột "Đang giữ": "3 tệp/thư mục: D:\..., ..." / "chạy từ ổ" / "nạp DLL từ ổ".
    QString holdingSummary() const;
};

struct Report
{
    QList<Holder> holders;           // xếp: dừng được trước, nhiều handle trước
    QStringList unreadable;          // tiến trình không đọc được handle (được bảo vệ / thiếu quyền) - CÓ THỂ đang giữ ổ
    bool pagefileOnDrive{false};     // pagefile.sys / swapfile.sys nằm trên ổ
    bool elevated{false};            // quét với quyền Administrator (không có thì chỉ thấy tiến trình của tài khoản này)
    bool handleScanComplete{true};   // false nếu quá nhiều truy vấn treo phải bỏ dở
    QString error;                   // lỗi không quét được (ổ không tồn tại...)
};

/// Quét các thứ đang giữ ổ 'driveLetter' ("D", "D:" hoặc "D:\"). Có thể mất vài giây - gọi từ luồng nền. stopFlag (tùy chọn) để hủy giữa chừng.
Report scan(const QString& driveLetter, const std::atomic_bool* stopFlag = nullptr);

/// Dừng một thứ đang giữ ổ: tiến trình chứa dịch vụ -> dừng từng dịch vụ (và dịch vụ phụ thuộc đang chạy) qua SCM, chờ tối đa ~20 giây; tiến trình
/// thường -> TerminateProcess (dữ liệu chưa lưu của nó bị mất), chờ thoát. Từ chối tiến trình hệ thống/chính mình. false + *error nếu không dừng được.
bool stop(const Holder& holder, QString* error = nullptr);

/// Tên tệp chạy của tiến trình hệ thống KHÔNG BAO GIỜ được dừng (System, smss, csrss, wininit, winlogon, services, lsass...). Không phân biệt hoa thường.
bool isCriticalProcessName(const QString& exeName);

/// Bật SeDebugPrivilege cho tiến trình này (chỉ có tác dụng khi đang chạy với quyền Administrator). true nếu đã bật.
bool enableDebugPrivilege();
} // namespace VolumeHolders
