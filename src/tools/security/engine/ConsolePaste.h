#pragma once

#include <QString>

/// Dán lệnh vào ĐÚNG cửa sổ PowerShell vừa mở mà KHÔNG gửi phím thật nào và KHÔNG chạy lệnh.
///
/// Cách làm (đã đo thật trên Windows 10 + PSReadLine 2.0, xem PROJECT_OVERVIEW.md mục 4i): gắn tạm vào console
/// của tiến trình PowerShell đó (AttachConsole), ghi một phím Ctrl+V vào BỘ ĐỆM NHẬP của chính console ấy
/// (WriteConsoleInputW) - PSReadLine nhận phím này như người dùng bấm và chạy chức năng Paste của nó: chèn
/// nội dung Clipboard vào dòng lệnh, kể cả lệnh nhiều dòng, KHÔNG bấm Enter. Rồi đọc lại màn hình console
/// (ReadConsoleOutputCharacterW) để xác nhận lệnh đã hiện ra thật - báo đúng kết quả thay vì "đã gửi phím,
/// chắc là được".
///
/// So với cách cũ (SetForegroundWindow + SendInput Ctrl+V): không phụ thuộc cửa sổ nào đang ở phía trước nên
/// không thể dán nhầm vào cửa sổ khác, không bị cơ chế chống cướp focus của Windows chặn, và phím được gửi
/// sớm (trước khi PowerShell hiện dấu nhắc) vẫn được xử lý khi PSReadLine sẵn sàng.
///
/// Phím Ctrl+V được ghi KHÔNG kèm ký tự (uChar = 0): PSReadLine vẫn nhận đúng là Ctrl+V, còn PowerShell
/// không có PSReadLine (chế độ đọc dòng thường của console) thì bỏ qua hẳn - không chèn rác "^V", không chạy
/// gì (đã đo thật cả hai trường hợp).
///
/// AttachConsole đổi console của CẢ tiến trình - vì vậy việc này chạy trong một tiến trình phụ riêng
/// ("OneForAll.exe --console-paste <pid>", lệnh cần đối chiếu truyền qua stdin), không chạy trong tiến trình
/// giao diện: trong lúc gắn, tiến trình con do luồng khác tạo sẽ dùng chung console của người dùng, và đóng
/// cửa sổ PowerShell đúng lúc đó sẽ gửi CTRL_CLOSE_EVENT làm tiến trình đang gắn bị kết thúc.
namespace ConsolePaste
{

/// Kết quả - cũng là MÃ THOÁT của tiến trình phụ "--console-paste".
enum class Outcome : int
{
    Pasted = 0,         ///< Đã đọc lại màn hình và thấy lệnh nằm ở dòng lệnh PowerShell
    SentUnverified = 1, ///< Đã gửi Ctrl+V; lệnh không có chữ/số nào để đối chiếu nên không tự xác nhận được
    AttachFailed = 2,   ///< Không gắn được vào console của tiến trình PowerShell đó
    NotSeen = 3,        ///< Đã gửi Ctrl+V, hết thời gian chờ vẫn không thấy lệnh hiện ra
    PromptIdle = 4,     ///< PowerShell đứng ở dấu nhắc vài giây mà lệnh không được dán (không có PSReadLine,
                        ///< hoặc phím Ctrl+V đã bị gán chức năng khác)
    ProcessExited = 5,  ///< Tiến trình PowerShell đã thoát (cửa sổ bị đóng) trước khi dán xong
    BadArguments = 6,   ///< Gọi tiến trình phụ sai tham số / không đọc được lệnh từ stdin
    WriteFailed = 7,    ///< Gắn được nhưng không ghi được phím vào bộ đệm nhập của console
};

/// Tên tham số dòng lệnh của tiến trình phụ.
inline constexpr const char* kHelperSwitch = "--console-paste";

/// Gắn vào console của `pid`, gửi Ctrl+V, chờ tối đa `timeoutMs` để thấy `expected` (văn bản đã nằm trong
/// Clipboard) hiện ra. Tiến trình gọi hàm này sẽ RỜI console hiện tại của nó (FreeConsole) - chỉ gọi trong
/// tiến trình phụ không có console riêng.
Outcome pasteAndVerify(quint32 pid, const QString& expected, int timeoutMs);

/// Điểm vào của tiến trình phụ: argv = [exe, "--console-paste", "<pid>"], lệnh cần đối chiếu (UTF-8) đọc
/// từ stdin tới hết. Trả về mã thoát (Outcome). Không cần QCoreApplication.
int runHelperMode(int argc, char** argv);

/// Thời gian chờ mặc định của tiến trình phụ (PowerShell nạp profile chậm vẫn kịp).
inline constexpr int kDefaultTimeoutMs = 20000;

namespace internal
{
/// Chuỗi chỉ gồm chữ cái và chữ số ASCII (A-Z, a-z, 0-9) của `text`. So khớp trên chuỗi này thì không bị ảnh
/// hưởng bởi chỗ console tự xuống dòng, dấu nhắc nối dòng ">> " của PSReadLine, tab bị đổi thành khoảng trắng
/// khi dán - và chữ có dấu bị VẼ thành "?": PSReadLine 2.0 (Windows PowerShell 5.1) vẽ dòng lệnh theo bảng mã
/// của console, ký tự ngoài bảng mã đó (vd "ế", "ệ" với bảng mã 1258) hiện thành "?" dù nội dung lệnh bên
/// trong vẫn đúng (đo thật: chạy lệnh ghi mã ký tự ra tệp cho đúng 7871/7879).
QString asciiLettersAndDigits(const QString& text);

/// "Dấu vân tay" để nhận ra lệnh trên màn hình: tối đa `maxChars` chữ/số ASCII đầu tiên của dòng KHÔNG trống
/// đầu tiên. Rỗng nếu dòng đó không có chữ/số ASCII nào - khi đó không đối chiếu được.
QString verificationKey(const QString& text, int maxChars = 40);

/// Số lần `needle` xuất hiện (không chồng lấn) trong `haystack`; needle rỗng -> 0.
int countOccurrences(const QString& haystack, const QString& needle);

/// Đoạn dòng hiện tại TRƯỚC con trỏ trông như dấu nhắc đang chờ nhập ("PS C:\>" / "PS C:\Users> " / ">> ").
bool looksLikeIdlePrompt(const QString& cursorLineBeforeCursor);
} // namespace internal

} // namespace ConsolePaste
