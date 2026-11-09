#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include "DiskLayout.h"

/**
 * @brief Cho phép DỜI phân vùng ngay cả khi chính ứng dụng đang chạy từ phân vùng đó (vd ứng dụng nằm ở D:\ALLinONE và người dùng muốn dời ổ D:).
 *
 * Vấn đề: ổ chứa exe/DLL của một tiến trình đang chạy KHÔNG khóa + tháo được (FSCTL_LOCK_VOLUME thất bại, mã 5) - và nếu ép tháo thì chính ứng dụng sập.
 * Cách làm: ứng dụng tự CHÉP bộ chạy tối thiểu (exe + DLL + thư mục plugin, bỏ qua yt-dlp/scrcpy/wintun/profiles) sang một thư mục tạm trên ổ khác,
 * khởi chạy bản sao đó với quyền Administrator (một lần UAC) ở chế độ "--run-partition-plan", rồi ĐÓNG chính mình để nhả ổ. Bản sao hiện một cửa sổ nhỏ
 * chạy kế hoạch (PlanWorker) với tiến độ + Tạm dừng; xong thì nút "Mở lại One for ALL" khởi chạy bản gốc (đã được dời cùng ổ) bằng quyền thường.
 *
 * Kế hoạch đi qua dòng lệnh (base64url của JSON) nên KHÔNG phụ thuộc thư mục dữ liệu của tài khoản; bên nhận kiểm tra lại mọi thứ từ Windows trước khi ghi
 * (PartitionMover) và cửa sổ yêu cầu bấm "Bắt đầu" - không có gì chạy tự động chỉ vì có ai đó gọi exe với tham số.
 */
namespace PartitionRelocator
{
struct Plan
{
    bool resume{false};       // chạy tiếp lần dời dở dang theo nhật ký (steps rỗng)
    int disk{-1};
    int partition{-1};
    QString letter;           // "D" hoặc rỗng
    QList<DiskLayout::PlanStep> steps;
    QString originalExe;      // đường dẫn exe GỐC để "Mở lại"
    QString description;      // mô tả kế hoạch cho người đọc
};

QByteArray toJson(const Plan& plan);
/// false nếu không phải kế hoạch hợp lệ (JSON hỏng, số vô lý, bước lạ) - khi đó tuyệt đối không chạy.
bool fromJson(const QByteArray& json, Plan* out, QString* error = nullptr);
QString encodeArgument(const Plan& plan);                                    // base64url(JSON) - dán được vào dòng lệnh
bool decodeArgument(const QString& argument, Plan* out, QString* error = nullptr);

/// Chép bộ chạy tối thiểu từ srcDir sang dstDir (tạo nếu chưa có): mọi tệp ở cấp trên cùng (exe, DLL) + các thư mục plugin; BỎ QUA yt-dlp, scrcpy, wintun,
/// profiles, opengl32sw.dll, D3Dcompiler_47.dll (không cần để chạy giao diện Widgets). false + error nếu có tệp không chép được (khi đó dstDir bị xóa).
bool copyRuntime(const QString& srcDir, const QString& dstDir, QString* error, qint64* bytesCopied = nullptr);
/// Thư mục tạm mới (chưa tạo) dạng "%TEMP%\\OneForAll_diskmove_<thời gian>_<số ngẫu nhiên>".
QString newRuntimeDir();
/// true nếu đường dẫn là thư mục bản sao do newRuntimeDir() sinh ra (nằm ĐÚNG trong %TEMP%, tên đúng mẫu) - chỉ thư mục như vậy mới bị xóa tự động.
bool isRuntimeDir(const QString& path);
/// Xóa các thư mục bản sao cũ còn sót (best-effort, bỏ qua cái đang được dùng). Gọi lúc khởi động.
void cleanupStaleRuntimes();

/// Khởi chạy 'runnerExe' (bản sao trong thư mục tạm) với quyền Administrator (UAC) ở chế độ chạy kế hoạch. Thư mục làm việc = runtimeDir (không nằm trên ổ
/// sắp tháo). false + error nếu người dùng từ chối UAC hoặc không khởi chạy được.
bool launchElevatedRunner(const QString& runnerExe, const QString& runtimeDir, const Plan& plan, QString* error);

/// Chờ tiến trình 'pid' kết thúc (tối đa timeoutMs; 0 = chỉ kiểm tra). true nếu nó đã thoát hoặc không tồn tại.
bool processHasExited(quint32 pid, int timeoutMs);
} // namespace PartitionRelocator
