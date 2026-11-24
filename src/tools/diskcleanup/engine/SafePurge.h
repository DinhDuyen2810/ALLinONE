#pragma once

#include <QString>
#include <atomic>
#include <functional>

/**
 * @brief Xóa HẲN (không qua Thùng rác) dữ liệu TỰ TẠO LẠI được - bộ nhớ đệm, tệp tạm, dump, nhật ký - theo
 * TỪNG TỆP, đúng cách roomy (krezio/roomy SafeFs.cs, Nyaughh/roomy delete.rs) làm. v1.32.0.
 *
 * Vì sao không dùng Thùng rác/SHFileOperationW cho các hạng mục này:
 *  - Đưa bộ nhớ đệm vào Thùng rác KHÔNG giải phóng byte nào: Thùng rác ($Recycle.Bin) nằm trên chính ổ đó,
 *    dung lượng chỉ về khi làm trống Thùng rác - trong khi app vẫn báo "đã giải phóng".
 *  - Một tệp đang mở trong một thư mục cache làm Shell báo lỗi cho CẢ thư mục đó (mục bị coi là "không xóa
 *    được" dù 99% tệp bên trong xóa được) và mỗi tệp bị khóa tốn ~1,2 giây Shell tự thử lại.
 *
 * An toàn (mọi điều kiện kiểm trên CHÍNH handle sẽ dùng để xóa - không có khe hở giữa "kiểm" và "xóa"):
 *  - Mở mục bằng FILE_FLAG_OPEN_REPARSE_POINT (không bao giờ đi theo liên kết của chính mục đó) rồi hỏi
 *    Windows đường dẫn THẬT của handle (GetFinalPathNameByHandleW): phải nằm TRONG thư mục gốc tin cậy (cũng
 *    lấy đường dẫn thật). Một thư mục trung gian bị đổi thành junction trỏ ra ngoài giữa chừng -> từ chối.
 *  - Không đi vào junction/symlink thư mục, không xóa tệp liên kết (symlink) và tệp "chỉ có trên mây"
 *    (OneDrive - xóa tệp giữ chỗ là xóa luôn bản trên mây).
 *  - Tệp đang bị chương trình khác mở không cho xóa -> BỎ QUA (đếm riêng), không ép.
 *  - Xóa bằng SetFileInformationByHandle(FileDispositionInfoEx, DELETE | POSIX_SEMANTICS |
 *    IGNORE_READONLY_ATTRIBUTE); hệ thống tệp cũ (FAT, ổ mạng) không có lớp đó thì bỏ cờ chỉ đọc rồi dùng
 *    FileDispositionInfo (khôi phục cờ nếu vẫn không xóa được).
 *  - Thư mục con rỗng được gỡ (sâu trước); thư mục gốc tin cậy KHÔNG BAO GIỜ bị gỡ.
 */
namespace SafePurge
{
struct Result
{
    qint64 freedBytes{0};
    int deletedFiles{0};
    int inUseFiles{0};   // đang bị chương trình khác mở - giữ lại
    int failedFiles{0};  // lỗi khác (quyền truy cập...)
    int refusedFiles{0}; // từ chối vì lý do an toàn (liên kết, tệp trên mây, đích thật nằm ngoài thư mục gốc)
    bool rootRefused{false}; // không xác định được đường dẫn thật của thư mục gốc tin cậy -> không xóa gì
    bool stopped{false};
    bool removedItself{false}; // chính 'path' không còn (tệp đã xóa, hoặc thư mục đã gỡ hết)
};

/// Đường dẫn THẬT (đi theo junction/symlink ở mọi thành phần) của một tệp/thư mục đang tồn tại, dạng
/// "C:/..." dấu '/' (UNC: "//máy/..."); rỗng nếu không mở được.
QString finalPath(const QString& path);

/// 'real' là 'root' hoặc nằm bên trong 'root' (so không phân biệt hoa thường, theo ranh giới thành phần).
bool isInside(const QString& real, const QString& root);

/// Xóa hẳn 'path' - một tệp, hoặc một thư mục cùng toàn bộ nội dung - với điều kiện mọi thứ bị xóa nằm trong
/// 'trustedRoot'. 'path' trùng 'trustedRoot': chỉ xóa NỘI DUNG, giữ thư mục gốc (với TỆP gốc - vd
/// %WINDIR%\MEMORY.DMP - thì xóa chính tệp đó). onFreed(bytes) được gọi sau mỗi tệp xóa được (báo tiến độ).
Result purge(const QString& path, const QString& trustedRoot, const std::atomic_bool& stopFlag,
             const std::function<void(qint64 bytes)>& onFreed = {});
} // namespace SafePurge
