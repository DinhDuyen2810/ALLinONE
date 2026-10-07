#pragma once

#include "../model/VideoInfo.h"

#include <QString>

/// Lớp duy nhất liên quan tới đường dẫn/khả dụng của `yt-dlp.exe` + `ffmpeg.exe`/`ffprobe.exe` đóng gói
/// kèm theo ứng dụng (thư mục `yt-dlp/` cạnh file exe - xem THIRD_PARTY.md), dùng để tải video từ các
/// nền tảng (YouTube/Facebook/TikTok/...). KHÔNG tự viết lại trình trích xuất video từng trang - đây là
/// bài toán cực lớn (hàng nghìn trang khác nhau, thay đổi liên tục) mà yt-dlp (MIT/Unlicense, cập nhật
/// rất thường xuyên, dùng rộng rãi) đã giải quyết.
///
/// ffmpeg/ffprobe CẦN THIẾT để GHÉP luồng video+âm thanh tải riêng (YouTube chất lượng cao hiện đại hầu
/// hết KHÔNG còn định dạng ghép sẵn - đã xác nhận THẬT khi kiểm tra tay: `-f best` báo lỗi "Requested
/// format is not available" trên một video thật) - không có ffmpeg, chỉ tải được chất lượng thấp (định
/// dạng ghép sẵn cũ, nếu nền tảng còn hỗ trợ).
namespace YtDlpController
{
QString ytDlpExecutablePath();
QString ffmpegDirectoryPath(); // thư mục chứa ffmpeg.exe/ffprobe.exe - truyền cho --ffmpeg-location
bool isBundleAvailable(QString* missingWhat = nullptr);
} // namespace YtDlpController
