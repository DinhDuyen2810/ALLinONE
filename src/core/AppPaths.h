#pragma once

#include <QString>

/// Nơi DUY NHẤT quyết định dữ liệu người dùng (hồ sơ JSON, log) nằm ở đâu trên đĩa, và đường dẫn tuyệt
/// đối tới các tệp thực thi hệ thống mà ứng dụng khởi chạy.
///
/// Trước đây mọi module tự ghi đường dẫn TƯƠNG ĐỐI ("profiles/x.json", "logs/app.log") - tức phụ thuộc
/// thư mục làm việc (CWD) của tiến trình lúc khởi động. Hệ quả thật đã thấy khi rà soát:
///  - Cài "cho mọi người dùng" (Program Files) thì thư mục cạnh exe không ghi được, lưu thất bại im lặng.
///  - Trình cài đặt chép đè `profiles\default.json` MẪU lên đúng tệp người dùng đang lưu chuỗi Auto Click
///    của họ ở mỗi lần cập nhật.
///  - Chạy bản dev từ `OneForAll_Release\` ghi khóa ghép đôi/lịch sử cá nhân vào chính thư mục sẽ được
///    đóng gói thành bộ cài đặt.
/// Dữ liệu người dùng nay nằm ở `%LOCALAPPDATA%\OneForAll\` (không cần quyền Administrator, không bị
/// trình cài đặt/gỡ cài đặt đụng tới), tự chép sang từ vị trí cũ ở lần chạy đầu.
namespace AppPaths
{
/// Thư mục gốc dữ liệu người dùng (đã tạo sẵn), luôn là đường dẫn TUYỆT ĐỐI. Thứ tự ưu tiên:
/// setDataDirOverride() (dành cho test) → biến môi trường ONEFORALL_DATA_DIR (giá trị tương đối được phân
/// giải theo thư mục làm việc ở lần đầu dùng tới rồi giữ nguyên) → `%LOCALAPPDATA%\OneForAll`.
QString dataDir();

/// `dataDir()/profiles` và `dataDir()/logs` (đã tạo sẵn).
QString profilesDir();
QString logsDir();

/// Đường dẫn tuyệt đối tới một tệp hồ sơ (vd "default.json", "qr_history.json"). Nếu tệp CHƯA có ở vị
/// trí mới nhưng còn ở vị trí cũ (`<thư mục exe>/profiles/` rồi tới `<CWD>/profiles/`) thì chép sang
/// trước khi trả về - vừa là bước di trú dữ liệu của bản cũ, vừa là cách nạp tệp mẫu đóng gói kèm
/// (`default.json`) ở lần chạy đầu. Tệp cũ được giữ nguyên, không xóa.
QString profileFile(const QString& fileName);

/// Xóa các bản CŨ của một tệp hồ sơ ở vị trí trước đây (`<thư mục exe>/profiles/`, `<CWD>/profiles/`) -
/// KHÔNG đụng tới bản đang dùng trong profilesDir(). Chỉ dành cho tệp chứa bí mật đã được ghi lại ở dạng
/// bảo vệ tốt hơn (vd khóa ghép đôi Connect Together chuyển từ Base64 thô sang DPAPI): giữ bản cũ nằm lại
/// thì việc bảo vệ bản mới vô nghĩa. Tệp không nhạy cảm (chuỗi Auto Click, lịch sử QR...) thì KHÔNG gọi -
/// bản cũ được giữ nguyên làm lối lùi. Không làm gì khi đang ép thư mục dữ liệu (test).
void removeLegacyProfileCopies(const QString& fileName);

/// Đường dẫn tuyệt đối tới một tệp log (vd "app.log").
QString logFile(const QString& fileName);

/// CHỈ dành cho bộ kiểm thử: ép toàn bộ dữ liệu vào một thư mục tạm để test không đọc/ghi/xóa dữ liệu
/// thật của người dùng trên máy đang chạy test. Truyền chuỗi rỗng để bỏ ép. Gọi TRƯỚC khi đụng tới
/// Logger/các store (chúng đọc đường dẫn lúc khởi tạo). Khi đang ép, profileFile() KHÔNG chép tệp từ vị
/// trí cũ sang - test luôn bắt đầu từ thư mục trống.
void setDataDirOverride(const QString& dir);

/// Đường dẫn tuyệt đối tới một tệp thực thi trong thư mục hệ thống (vd "rasdial.exe" →
/// `C:\Windows\System32\rasdial.exe`). Khởi chạy bằng tên trần ("rasdial.exe") để Windows tự tìm theo
/// thứ tự thư mục ứng dụng → CWD → PATH cho phép một tệp trùng tên đặt cạnh exe chiếm chỗ - đặc biệt
/// nguy hiểm khi ứng dụng đang chạy với quyền Administrator.
QString systemExecutable(const QString& fileName);

/// `...\System32\WindowsPowerShell\v1.0\powershell.exe`.
QString powershellExecutable();
} // namespace AppPaths
