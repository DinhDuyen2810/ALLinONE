#pragma once

#include <QtGlobal>

#include <vector>

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
/// Liệt kê (KHÔNG dừng gì) PID mọi hậu duệ hiện tại (con, cháu, ...) của rootPid. Trả về rỗng nếu
/// rootPid <= 0, không tồn tại, hoặc không có hậu duệ nào.
///
/// QUAN TRỌNG VỀ THỨ TỰ GỌI: gọi hàm này TRƯỚC khi buộc dừng rootPid, ngay khi còn kiểm soát được thời
/// điểm dừng (vd trong `stop()`/`cancel()` tự ứng dụng gọi) - không gọi SAU khi rootPid đã thoát. Lý do:
/// PID trên Windows là định danh CÓ THỂ TÁI SỬ DỤNG - một khi rootPid đã thoát hẳn, Windows có thể cấp
/// lại đúng số PID đó cho một tiến trình HOÀN TOÀN KHÔNG LIÊN QUAN được tạo ra ngay sau đó; nếu quét lúc
/// này, `th32ParentProcessID` khớp rootPid trong snapshot mới có thể thuộc về tiến trình không liên quan
/// kia, khiến ta vô tình liệt kê (rồi dừng) nhầm tiến trình con THẬT của nó. Quét khi rootPid CHẮC CHẮN
/// vẫn là tiến trình của chính mình (vừa tự khởi chạy qua QProcess, chưa ai kịp tái sử dụng PID) loại bỏ
/// hẳn rủi ro này cho chính rootPid.
std::vector<qint64> findDescendants(qint64 rootPid);

/// TerminateProcess từng PID trong danh sách đã có sẵn (thường lấy từ findDescendants() ở trên, gọi
/// TRƯỚC khi dừng tiến trình cha - xem ghi chú trên). An toàn gọi với PID đã không còn tồn tại (bỏ qua
/// lặng lẽ, không báo lỗi) hoặc danh sách rỗng.
void terminateProcessList(const std::vector<qint64>& pids);

/// Tiện ích gộp findDescendants()+terminateProcessList() - CHỈ dùng khi KHÔNG CÓ CÁCH NÀO quét trước lúc
/// tiến trình cha còn sống (vd tiến trình cha tự thoát BẤT THƯỜNG - crash/bị Task Manager kill/rút cáp -
/// ta chỉ biết được SAU KHI nó đã thoát, qua tín hiệu `finished()` của QProcess). Vẫn còn nguyên rủi ro
/// tái sử dụng PID mô tả ở trên cho chính rootPid trong trường hợp này - chấp nhận được vì đây là lưới
/// an toàn cho tình huống ngoài tầm kiểm soát, không phải đường dừng chính do ứng dụng tự khởi xướng.
void terminateDescendants(qint64 rootPid);
}
