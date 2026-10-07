#pragma once

#include <QString>

/// Kiểm tra/khởi chạy lại chính ứng dụng này với quyền Administrator - dùng CHUNG cho mọi module cần
/// quyền cao hơn (PartitionManager để đổi kích thước phân vùng, VpnController để thêm/xóa kết nối VPN
/// hệ thống...). Tách riêng khỏi từng module để không lặp lại cùng một đoạn Win32
/// (OpenProcessToken/GetTokenInformation/ShellExecuteExW) ở nhiều nơi.
namespace WinElevation
{
/// Tiến trình hiện tại có đang chạy với quyền Administrator không (token "Administrators" đã kích
/// hoạt qua UAC "Run as administrator").
bool isElevated();

/// Khởi chạy LẠI chính ứng dụng này với quyền Administrator (ShellExecuteW verb "runas" - hiện hộp
/// thoại UAC chuẩn của Windows, người dùng tự xác nhận/từ chối, KHÔNG có gì diễn ra âm thầm). Gọi
/// xong, nếu thành công, nơi gọi tự quyết định có thoát tiến trình hiện tại hay không. Trả về false
/// nếu người dùng bấm "No" trên hộp thoại UAC hoặc có lỗi khác.
bool relaunchElevated(QString* error = nullptr);
} // namespace WinElevation
