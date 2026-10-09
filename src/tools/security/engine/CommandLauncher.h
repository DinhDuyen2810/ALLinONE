#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

/// Mở một cửa sổ PowerShell THẬT, riêng biệt (powershell.exe mới, không phải tiến trình con bị ẩn) và
/// DÁN (không tự chạy) lệnh đã được CommandAnalyzer xem xét vào đó, để người dùng tự xem lại lần cuối
/// và tự bấm Enter. Ứng dụng KHÔNG BAO GIỜ tự thực thi lệnh người dùng nhập - đây chỉ là bước "dán hộ"
/// để đỡ phải gõ lại, quyết định chạy hay không luôn thuộc về người dùng trên cửa sổ PowerShell thật.
///
/// Giới hạn của cam kết "không tự chạy" (xem CommandLauncherInternal::pasteWarnings): dán văn bản NHIỀU
/// DÒNG vào console có thể khiến các dòng trước dòng cuối chạy ngay, tùy cấu hình PowerShell trên máy -
/// nơi gọi (CommandGatewayTab) phải cảnh báo và hỏi xác nhận trước khi gọi launchAndPaste() với lệnh
/// nhiều dòng.
class CommandLauncher : public QObject
{
    Q_OBJECT

public:
    explicit CommandLauncher(QObject* parent = nullptr);
    ~CommandLauncher() override;

    /// Đặt lên Clipboard, mở powershell.exe mới, rồi gửi Ctrl+V vào đúng cửa sổ đó - CHỈ KHI cửa sổ đó
    /// thật sự đang ở phía trước và nhận bàn phím. Không đưa được nó lên trước thì KHÔNG gửi phím nào
    /// (phím gửi qua SendInput đi vào bất kỳ cửa sổ nào đang nhận bàn phím - bản trước có thể dán lệnh
    /// khả nghi vào một cửa sổ khác rồi vẫn báo thành công) và báo người dùng tự dán.
    /// Phát tín hiệu finished(ok, error) khi xong. Lệnh VẪN nằm trong Clipboard sau khi dán - không tự
    /// khôi phục nội dung Clipboard cũ: PowerShell (PSReadLine) đọc Clipboard lúc nó XỬ LÝ phím Ctrl+V,
    /// có thể vài giây sau khi phím được gửi nếu profile nạp chậm; khôi phục sớm sẽ dán nhầm nội dung cũ.
    void launchAndPaste(const QString& command);

signals:
    void finished(bool ok, QString error);

private:
    void pollForWindow();
    void pasteWhenForeground();

    QTimer* m_pollTimer{nullptr};
    qint64 m_targetPid{0};
    int m_attemptsLeft{0};
    void* m_targetWindow{nullptr}; ///< HWND cửa sổ PowerShell vừa mở (void* để header không kéo theo windows.h)
    int m_foregroundAttemptsLeft{0};
};

namespace CommandLauncherInternal
{
/// Văn bản thật sự được đặt lên Clipboard: thống nhất ký tự xuống dòng về '\n' và BỎ mọi dòng trắng/
/// khoảng trắng ở đầu và cuối. Một ký tự xuống dòng ở cuối đủ để console chạy ngay cả lệnh MỘT dòng khi
/// dán (tương đương bấm Enter hộ người dùng). Thuần chuỗi, test được.
QString prepareForPaste(const QString& command);

/// Số dòng của văn bản đã qua prepareForPaste().
int lineCount(const QString& prepared);

/// Các cảnh báo phải cho người dùng xem và xác nhận TRƯỚC khi dán (rỗng = dán được ngay): lệnh nhiều
/// dòng (các dòng trước dòng cuối có thể tự chạy khi dán), và ứng dụng đang chạy quyền Administrator
/// (cửa sổ PowerShell mở ra thừa hưởng đúng quyền đó). Thuần, test được.
QStringList pasteWarnings(const QString& prepared, bool appIsElevated);
} // namespace CommandLauncherInternal
