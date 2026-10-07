#pragma once

#include <QByteArray>
#include <QString>

/// Chạy script PowerShell từ C++ - dùng CHUNG cho mọi module cần gọi PowerShell (PartitionManager để
/// đổi kích thước phân vùng, VpnController để quản lý kết nối VPN hệ thống...). Tách riêng để không
/// lặp lại (và không vô tình tái phát) 2 lỗi THẬT đã tìm ra khi kiểm tra tay trên máy thật - xem ghi
/// chú trong PowerShellRunner.cpp.
namespace PowerShellRunner
{
/// Chạy script PowerShell, trả về stdout; *ok = (exitCode == 0). -NoProfile/-NonInteractive: không
/// nạp profile người dùng, không chờ nhập liệu.
QByteArray run(const QString& script, bool* ok, QString* error = nullptr, int timeoutMs = 20000);
} // namespace PowerShellRunner
