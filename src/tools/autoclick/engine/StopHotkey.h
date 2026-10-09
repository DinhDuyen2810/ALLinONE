#pragma once

#include <QString>
#include <QtGui/qwindowdefs.h> // WId

/**
 * @brief Phím tắt TOÀN CỤC để dừng khẩn cấp chuỗi Auto Click đang chạy (mục 65 tài liệu thiết kế -
 * RegisterHotKey). Trước đây chỉ dừng được bằng cách BẤM CHUỘT vào nút Dừng trên HUD/cửa sổ chính, trong
 * khi chính chuỗi đang chạy lại liên tục giành con trỏ chuột (SetCursorPos) - với chuỗi lặp vô hạn có thời
 * gian chờ ngắn thì gần như không bấm trúng được.
 *
 * Tổ hợp CỐ ĐỊNH Ctrl+Alt+F8 (không phải F8 trần): F1-F12 nằm trong danh sách phím mà chính chuỗi hành
 * động có thể gửi đi ("Nhấn phím"), và RegisterHotKey khớp cả phím do SendInput tiêm vào - dùng phím đơn
 * thì một chuỗi có bước "Nhấn F8" sẽ tự dừng chính nó.
 *
 * Chỉ ĐĂNG KÝ trong lúc chuỗi đang chạy rồi hủy ngay khi dừng (không giữ phím tắt của hệ thống khi không
 * cần). Gom toàn bộ lời gọi Win32 vào đây theo Rule 3 (Win32 chỉ nằm ở tầng engine/capture).
 */
namespace StopHotkey
{
/// Nhãn hiển thị cho người dùng ("Ctrl+Alt+F8").
QString label();

/// Đăng ký phím tắt, gắn với cửa sổ có handle `windowId` (QWidget::winId()) - WM_HOTKEY sẽ được gửi tới
/// đúng cửa sổ đó (kể cả khi nó đang thu nhỏ). Trả về false nếu Windows từ chối, thường vì một ứng dụng
/// khác đã đăng ký đúng tổ hợp này. PHẢI gọi từ luồng sở hữu cửa sổ (luồng GUI).
bool registerFor(WId windowId);

void unregisterFor(WId windowId);

/// `message` là con trỏ MSG* nhận được trong QWidget::nativeEvent() - true nếu đó là WM_HOTKEY của
/// đúng phím dừng đã đăng ký ở trên.
bool isStopMessage(const void* message);
} // namespace StopHotkey
