#pragma once

#include <QtGlobal>

/// Dùng chung cho mọi nơi trong ứng dụng khởi chạy một tiến trình ngoài DÀI HẠN mà bản thân tiến trình
/// đó lại tự sinh thêm tiến trình CON của riêng nó (vd `scrcpy.exe` -> `adb.exe shell ...`, `yt-dlp.exe`
/// -> `ffmpeg.exe` lúc ghép video+âm thanh) - xem ScrcpyLauncher.cpp/YtDlpDownloadWorker.cpp.
///
/// Trên Windows, buộc dừng một tiến trình cha (`QProcess::terminate()`/`kill()`, tức `TerminateProcess`)
/// KHÔNG đệ quy dừng luôn các tiến trình con của nó (khác hẳn gửi tín hiệu cho cả process group trên
/// Linux) - nếu không tự quét và dừng tiếp, tiến trình con mồ côi (orphan) sẽ treo lại NGẦM vô thời hạn.
/// Phải tự liệt kê toàn bộ tiến trình đang chạy trên máy (CreateToolhelp32Snapshot) và tìm mọi hậu duệ
/// (con, cháu, ...) của PID tiến trình cha để tự tay dừng từng cái.
namespace WinProcessTree
{
/// Tìm và TerminateProcess mọi hậu duệ (không bao gồm chính rootPid) của tiến trình có PID rootPid - gọi
/// SAU KHI đã tự buộc dừng (terminate()/kill()) chính tiến trình cha, vì bản thân nó không có cơ hội tự
/// dọn con của mình khi bị buộc dừng từ bên ngoài. An toàn gọi kể cả khi rootPid đã không còn tồn tại
/// hoặc không còn tiến trình con nào (không làm gì, không báo lỗi).
void terminateDescendants(qint64 rootPid);
}
