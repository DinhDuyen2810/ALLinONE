#pragma once

#include <QSet>
#include <QString>

/// Các truy vấn/thao tác hệ thống nhỏ dùng cho tab dọn dẹp theo hạng mục (v1.32.0).
namespace CleanupSystem
{
/// Tên tệp chạy (chữ thường, vd "chrome.exe") của mọi tiến trình đang chạy. Chỉ đọc (CreateToolhelp32Snapshot).
QSet<QString> runningExeNamesLower();

/// Đường dẫn tệp ngủ đông trên ổ hệ thống (vd "C:/hiberfil.sys"); rỗng nếu systemDrive rỗng.
QString hibernationFilePath(const QString& systemDrive);
/// Kích thước hiberfil.sys (đọc mục thư mục - không cần quyền Administrator); 0 nếu ngủ đông đang tắt.
qint64 hibernationFileSize(const QString& systemDrive);

/// Tắt ngủ đông bằng chính công cụ của Windows: powercfg.exe /hibernate off, chạy NÂNG QUYỀN (hộp thoại UAC).
/// Windows tự gỡ hiberfil.sys; tắt luôn "Khởi động nhanh". Bật lại: powercfg /hibernate on (Administrator).
/// false + *error nếu người dùng từ chối UAC hoặc powercfg báo lỗi. Chặn tới khi powercfg thoát (gọi từ luồng nền).
bool turnOffHibernation(QString* error = nullptr);
} // namespace CleanupSystem
