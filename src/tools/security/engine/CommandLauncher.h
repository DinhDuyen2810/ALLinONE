#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;
class QTimer;

/// Mở một cửa sổ PowerShell THẬT, riêng biệt (powershell.exe mới, có cửa sổ console của riêng nó) và DÁN (không
/// tự chạy) lệnh đã được CommandAnalyzer xem xét vào đó, để người dùng tự xem lại lần cuối và tự bấm Enter.
/// Ứng dụng KHÔNG BAO GIỜ tự thực thi lệnh người dùng nhập - đây chỉ là bước "dán hộ" để đỡ phải gõ lại,
/// quyết định chạy hay không luôn thuộc về người dùng trên cửa sổ PowerShell thật.
///
/// Cách làm từ v1.28.1 (bản cũ KHÔNG BAO GIỜ dán được - xem PROJECT_OVERVIEW.md mục 4i):
///  1. Đặt lệnh lên Clipboard (PSReadLine đọc từ đây; người dùng cũng tự dán được nếu bước 3 không thành).
///  2. Tạo powershell.exe bằng CreateProcessW + CREATE_NEW_CONSOLE, không kế thừa handle, thư mục làm việc là
///     thư mục người dùng. KHÔNG dùng QProcess::startDetached: với ứng dụng GUI, Qt 6.11 tạo console KHÔNG CÓ
///     CỬA SỔ (đo thật: GetConsoleWindow() = NULL) - bấm nút không thấy gì, còn sót một powershell.exe ẩn.
///  3. Chạy tiến trình phụ "OneForAll.exe --console-paste <pid>" (ConsolePaste): ghi phím Ctrl+V vào bộ đệm
///     nhập của ĐÚNG console đó rồi đọc lại màn hình để xác nhận lệnh đã hiện ra. Không SendInput, không cần
///     cửa sổ nào ở phía trước - không thể dán nhầm sang cửa sổ khác.
///  4. Đưa cửa sổ PowerShell lên phía trước nếu tìm được (chỉ để tiện, không ảnh hưởng việc dán).
///
/// Lệnh nhiều dòng được dán qua chức năng Paste của PSReadLine nên KHÔNG dòng nào tự chạy (đo thật); PowerShell
/// không có PSReadLine thì bỏ qua phím dán (không chạy gì) và người dùng được báo để tự dán.
///
/// Phát tín hiệu finished(ok, message) đúng MỘT lần cho mỗi lần launchAndPaste() được nhận. Lệnh VẪN nằm trong
/// Clipboard sau khi dán - không tự khôi phục nội dung Clipboard cũ (PSReadLine đọc Clipboard lúc nó XỬ LÝ
/// phím, có thể vài giây sau nếu profile nạp chậm; khôi phục sớm sẽ dán nhầm nội dung cũ).
class CommandLauncher : public QObject
{
    Q_OBJECT

public:
    explicit CommandLauncher(QObject* parent = nullptr);
    ~CommandLauncher() override;

    /// Bắt đầu mở + dán. Đang bận với lần trước thì bỏ qua (không mở chồng cửa sổ thứ hai, không phát tín hiệu).
    void launchAndPaste(const QString& command);

    bool isBusy() const { return m_busy; }

    /// CHỈ cho test tự động: chạy tiến trình phụ khác (chính tệp test), thêm tham số cho powershell.exe (vd
    /// -NoProfile), và ẩn cửa sổ console (CREATE_NO_WINDOW) để test không hiện gì lên màn hình người dùng.
    struct TestOptions
    {
        QString helperProgram;      ///< rỗng = chính ứng dụng (QCoreApplication::applicationFilePath())
        QStringList powershellArgs; ///< thêm sau powershell.exe
        bool hiddenConsole{false};  ///< CREATE_NO_WINDOW: console không có cửa sổ
        bool minimizedNoActivate{false}; ///< có cửa sổ thật nhưng thu nhỏ, không lấy focus, không đưa lên trước
        int helperTimeoutMs{0};     ///< 0 = ConsolePaste::kDefaultTimeoutMs
    };
    void setTestOptions(const TestOptions& options) { m_test = options; }
    /// PID của powershell.exe mở lần gần nhất (0 nếu chưa mở được).
    quint32 lastPowerShellPid() const { return m_pid; }
    /// CHỈ cho test: đóng tiến trình powershell.exe mà chính test đã cho mở (giữ handle từ lúc tạo nên không
    /// thể nhầm sang tiến trình khác dùng lại PID). Trả về true nếu đã đóng/đã thoát sẵn.
    bool terminateLastPowerShellForTest();

signals:
    void finished(bool ok, QString message);

private:
    void finish(bool ok, const QString& message);
    void onHelperFinished(int exitCode, bool crashed);
    void bringWindowToFront();
    void closeProcessHandle();

    TestOptions m_test;
    bool m_busy{false};
    quint32 m_pid{0};
    void* m_process{nullptr}; ///< HANDLE của powershell.exe (void* để header không kéo theo windows.h)
    QProcess* m_helper{nullptr};
    QTimer* m_watchdog{nullptr};
    QTimer* m_windowTimer{nullptr};
    int m_windowAttemptsLeft{0};
    int m_lines{0};       ///< số dòng của lệnh đang dán (cho thông báo)
    bool m_hasTabs{false}; ///< lệnh có ký tự Tab (PSReadLine đổi thành dấu cách khi dán)
};

namespace CommandLauncherInternal
{
/// Văn bản thật sự được đặt lên Clipboard: thống nhất ký tự xuống dòng về '\n', BỎ các dòng trắng ở đầu
/// và mọi khoảng trắng/dòng trắng ở CUỐI (thụt lề của dòng đầu tiên được giữ nguyên). Một ký tự xuống dòng ở
/// cuối có thể khiến lệnh chạy ngay khi người dùng tự dán bằng chuột phải (tương đương bấm Enter hộ). Thuần
/// chuỗi, test được.
QString prepareForPaste(const QString& command);

/// Số dòng của văn bản đã qua prepareForPaste().
int lineCount(const QString& prepared);

/// Các cảnh báo phải cho người dùng xem và xác nhận TRƯỚC khi mở (rỗng = mở được ngay): ứng dụng đang chạy
/// quyền Administrator (cửa sổ PowerShell mở ra thừa hưởng đúng quyền đó). Lệnh nhiều dòng KHÔNG còn cần
/// cảnh báo: dán qua PSReadLine không chạy dòng nào (đo thật). Thuần, test được.
QStringList pasteWarnings(const QString& prepared, bool appIsElevated);

/// Câu hướng dẫn tự dán (kèm lưu ý lệnh nhiều dòng khi dán bằng chuột phải).
QString manualPasteHint(int lines);

struct HelperResult
{
    bool ok{false};
    QString message;
};
/// Diễn giải kết quả tiến trình phụ "--console-paste" thành thông báo cho người dùng. `crashed` = tiến trình
/// phụ không thoát bình thường (bị kết thúc khi cửa sổ PowerShell đóng lúc nó đang gắn vào). `hasTabs`: lệnh
/// có ký tự Tab - chức năng Paste của PSReadLine đổi Tab thành 4 dấu cách (đo thật), nói rõ khi dán xong. Thuần,
/// test được.
HelperResult helperResult(int exitCode, bool crashed, int lines, int timeoutSeconds, bool hasTabs = false);

/// Dòng lệnh cho CreateProcessW: đường dẫn exe trong nháy kép + các tham số (thêm nháy khi có khoảng trắng/
/// nháy, theo quy tắc tách tham số của Windows). Thuần, test được.
QString buildCommandLine(const QString& exePath, const QStringList& args);
} // namespace CommandLauncherInternal
