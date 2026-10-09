#pragma once

#include <QString>
#include <QStringList>
#include <atomic>
#include <functional>

/**
 * @brief Các phép kiểm tra hệ thống tệp "không đi theo liên kết" dùng chung cho mọi thao tác QUÉT/XÓA
 * của Disk Cleanup - lớp bảo vệ chống xóa nhầm, tách riêng để test được.
 *
 * Lý do tồn tại (rà soát toàn bộ module): Qt (QFileInfo/QDirIterator) tự động ĐI THEO lối tắt .lnk và
 * không cho biết chắc một thư mục có phải junction hay không - các lớp quét trước đây vì thế có thể
 * (1) tính kích thước/duyệt xuyên qua một junction sang thư mục hoàn toàn khác, (2) coi cùng MỘT tệp
 * vật lý (thấy qua 2 đường dẫn nhờ junction/hardlink) là "2 bản trùng lặp" rồi đề nghị xóa một bản -
 * tức xóa bản duy nhất, (3) báo "đã giải phóng" cho một lối tắt hỏng chưa hề bị xóa. Mọi hàm ở đây
 * đọc thẳng thuộc tính NTFS của CHÍNH đường dẫn được hỏi (FindFirstFileW/GetFileInformationByHandle).
 */
namespace FsSafety
{
struct RawInfo
{
    bool exists{false};
    bool isDir{false};
    /// Symlink/junction (reparse point loại "thay tên" - IsReparseTagNameSurrogate). KHÔNG tính các
    /// reparse point khác như tệp giữ chỗ OneDrive, vốn vẫn là chính tệp/thư mục đó.
    bool isLink{false};
    qint64 sizeBytes{0};
    qint64 lastWriteTime{0}; // FILETIME (đơn vị 100ns từ 1601) - chỉ dùng để SO SÁNH, không hiển thị
};

/// Thông tin của CHÍNH đường dẫn này, không đi theo .lnk/symlink/junction.
RawInfo rawInfo(const QString& path);

/// Đường dẫn còn tồn tại không - khác QFileInfo::exists() (trả về false cho một lối tắt/symlink còn
/// đó nhưng ĐÍCH đã mất).
bool existsNoFollow(const QString& path);

/// TỆP này hiện không thể bị xóa vì đang bị chương trình khác giữ (không cho chia sẻ quyền xóa), bị khóa
/// vùng, hoặc người dùng không có quyền xóa. Dùng để bỏ qua trước các tệp đang mở thay vì giao cho Shell:
/// đo thật cho thấy MỖI lần SHFileOperationW gặp một tệp đang bị khóa mất ~1,2 giây (Shell tự thử lại)
/// rồi mới báo lỗi. Chỉ có nghĩa với tệp - mở một thư mục luôn được dù bên trong có tệp đang bị khóa.
bool lockedAgainstDelete(const QString& path);

/// Danh tính tệp vật lý (số sê-ri ổ + chỉ số tệp NTFS): hai đường dẫn cùng danh tính là CÙNG một tệp
/// trên đĩa (hardlink, hoặc thấy qua junction) - xóa "bản kia" không giải phóng gì hoặc xóa mất tệp.
struct FileIdentity
{
    quint32 volumeSerial{0};
    quint64 fileIndex{0};
    bool operator==(const FileIdentity& o) const { return volumeSerial == o.volumeSerial && fileIndex == o.fileIndex; }
};
bool fileIdentity(const QString& path, FileIdentity* out);

struct WalkEntry
{
    QString path; // tuyệt đối, dấu '/'
    qint64 sizeBytes{0};
    qint64 lastWriteTime{0};
};

/// Duyệt đệ quy MỌI tệp thường bên dưới 'root' (kể cả tệp ẩn/hệ thống). KHÔNG đi vào thư mục là
/// symlink/junction và bỏ qua tệp là symlink/lối tắt .lnk - không bao giờ "lạc" ra ngoài cây thư mục
/// thật của root. Trả về false nếu bị dừng giữa chừng qua stopFlag (kiểm tra mỗi mục).
bool walkFiles(const QString& root, const std::atomic_bool& stopFlag, const std::function<void(const WalkEntry&)>& onFile);

/// Thời điểm hiện tại theo cùng đơn vị với RawInfo::lastWriteTime.
qint64 nowFileTime();

/// Thư mục Windows thật theo API (GetWindowsDirectoryW), dấu '/'; rỗng nếu không lấy được.
QString windowsDirectory();

/// Các thư mục không bao giờ được coi là "thư mục rác xóa hết nội dung": hồ sơ người dùng và các thư
/// mục dữ liệu chuẩn bên trong, Program Files, ProgramData, thư mục Windows... (đọc từ máy thật).
QStringList systemProtectedDirs();

/// Rỗng nếu 'path' đủ an toàn để làm THƯ MỤC GỐC DỌN DẸP (xóa nội dung bên trong); ngược lại là lý do
/// từ chối: rỗng, tương đối/không có ký tự ổ, là gốc ổ đĩa, hoặc trùng/chứa một thư mục được bảo vệ.
/// Chặn tình huống biến môi trường TEMP/WINDIR bị thiếu hoặc trỏ sai (vd "D:\") biến cả ổ đĩa thành
/// "tệp tạm an toàn, tick sẵn".
/// Cũng từ chối các dạng mà Windows hiểu KHÁC với chuỗi được viết: tiền tố thiết bị "\\?\" / "\\.\", tên
/// kết thúc bằng dấu chấm/khoảng trắng, ký tự đại diện, bí danh chia sẻ quản trị ("\\máy\C$\...") và tên
/// ngắn 8.3 của một thư mục được bảo vệ.
QString unsafeCleanupRootReason(const QString& path, const QStringList& protectedDirs);

/// Rỗng nếu 'path' có thể giao cho Windows Shell (SHFileOperationW) mà Shell chắc chắn thao tác trên ĐÚNG
/// đối tượng đó; ngược lại là lý do từ chối. Gồm mọi điều kiện của unsafeCleanupRootReason(path, {}) cộng
/// thêm: chuỗi phải bất biến qua phép chuẩn hóa đường dẫn của Win32. Lỗi thật đã xác nhận bằng chạy thử:
/// yêu cầu xóa "thư mục\tệp." (tên có dấu chấm cuối) làm Shell xóa "thư mục\tệp" - một tệp KHÁC - và báo
/// thành công. Đường dẫn truyền vào phải đã qua QDir::cleanPath.
QString unsafeShellPathReason(const QString& path);
} // namespace FsSafety
