#pragma once

#include <QString>

/// Ứng dụng đang chạy từ gói MSIX (bản Microsoft Store) hay từ bản cài đặt thường (Setup.exe/.msi/thư mục).
///
/// Khác biệt cần xử lý khi chạy trong gói (PROJECT_OVERVIEW.md mục 4zzi):
///  - Không tự cập nhật qua GitHub: Store tự cập nhật gói; chạy OneForAll_Setup.exe từ bên trong gói sẽ cài
///    THÊM một bản thường thứ hai, không nâng cấp gói (và chính sách Store không cho cập nhật ngoài Store).
///  - Thư mục cài đặt (WindowsApps) chỉ đọc; AppData/HKCU được Windows ảo hóa riêng cho gói.
namespace PackageIdentity
{
/// true khi tiến trình có danh tính gói (GetCurrentPackageFullName thành công). Kết quả được nhớ lại.
bool isPackaged();

/// Tên đầy đủ của gói (vd "OneForAll_1.30.0.0_x64__abc..."), rỗng khi không chạy trong gói.
QString packageFullName();

/// CHỈ dành cho test: ép kết quả isPackaged() (-1 = đọc thật, 0 = không trong gói, 1 = trong gói).
void setPackagedOverrideForTest(int value);
} // namespace PackageIdentity
