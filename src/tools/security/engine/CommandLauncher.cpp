#include "CommandLauncher.h"

#include "../../autoclick/engine/InputController.h"
#include "core/AppPaths.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QProcess>
#include <QTimer>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace
{
struct FindByPidData
{
    DWORD pid{0};
    HWND found{nullptr};
};

BOOL CALLBACK enumWindowsProc(HWND hwnd, LPARAM lParam)
{
    auto* data = reinterpret_cast<FindByPidData*>(lParam);
    if (!IsWindowVisible(hwnd))
        return TRUE;
    if (GetWindow(hwnd, GW_OWNER) != nullptr) // chỉ xét cửa sổ cấp cao nhất thật sự (không phải cửa sổ con/sở hữu)
        return TRUE;

    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid == data->pid)
    {
        data->found = hwnd;
        return FALSE; // dừng duyệt, đã tìm thấy
    }
    return TRUE;
}

HWND findTopLevelWindowByPid(DWORD pid)
{
    FindByPidData data;
    data.pid = pid;
    EnumWindows(enumWindowsProc, reinterpret_cast<LPARAM>(&data));
    return data.found;
}

const char* kManualPasteHint =
    " Lệnh đã nằm sẵn trong Clipboard - hãy bấm vào cửa sổ PowerShell rồi tự dán (Ctrl+V hoặc bấm chuột phải).";
} // namespace

CommandLauncher::CommandLauncher(QObject* parent)
    : QObject(parent)
    , m_pollTimer(new QTimer(this))
{
    m_pollTimer->setInterval(100);
    connect(m_pollTimer, &QTimer::timeout, this, &CommandLauncher::pollForWindow);
}

CommandLauncher::~CommandLauncher() = default;

void CommandLauncher::launchAndPaste(const QString& command)
{
    if (m_pollTimer->isActive() || m_foregroundAttemptsLeft > 0)
        return; // lần mở trước chưa xong - không mở chồng thêm cửa sổ thứ hai

    const QString text = CommandLauncherInternal::prepareForPaste(command);
    if (text.isEmpty())
    {
        emit finished(false, "Lệnh trống.");
        return;
    }

    // Đặt Clipboard TRƯỚC khi mở cửa sổ mới - QClipboard chỉ dùng được an toàn trên luồng giao diện.
    QGuiApplication::clipboard()->setText(text);

    // Đường dẫn tuyệt đối trong System32 - không để Windows tự tìm "powershell.exe" theo thư mục ứng
    // dụng/CWD/PATH (xem AppPaths::systemExecutable).
    qint64 pid = 0;
    const bool started = QProcess::startDetached(AppPaths::powershellExecutable(), {}, QString(), &pid);
    if (!started || pid == 0)
    {
        emit finished(false, "Không mở được powershell.exe.");
        return;
    }

    m_targetPid = pid;
    m_attemptsLeft = 30; // tối đa 30 x 100ms = 3 giây chờ cửa sổ console xuất hiện
    m_pollTimer->start();
}

void CommandLauncher::pollForWindow()
{
    HWND hwnd = findTopLevelWindowByPid(static_cast<DWORD>(m_targetPid));
    if (!hwnd)
    {
        if (--m_attemptsLeft <= 0)
        {
            m_pollTimer->stop();
            emit finished(false, QString("Đã mở PowerShell nhưng không tìm thấy cửa sổ của nó trong 3 giây (vd "
                                         "Windows Terminal làm trình console mặc định) - không tự dán.") +
                                     kManualPasteHint);
        }
        return;
    }

    m_pollTimer->stop();
    m_targetWindow = hwnd;
    m_foregroundAttemptsLeft = 10; // tối đa 10 x 150ms = 1,5 giây chờ cửa sổ thật sự lên phía trước
    SetForegroundWindow(hwnd);

    // Đợi cửa sổ thật sự nhận focus rồi mới gửi Ctrl+V - gửi ngay lúc vừa SetForegroundWindow đôi khi
    // bị console bỏ lỡ vì chưa kịp bắt đầu đọc input.
    QTimer::singleShot(150, this, &CommandLauncher::pasteWhenForeground);
}

void CommandLauncher::pasteWhenForeground()
{
    HWND hwnd = static_cast<HWND>(m_targetWindow);
    if (!IsWindow(hwnd))
    {
        m_foregroundAttemptsLeft = 0;
        emit finished(false, "Cửa sổ PowerShell vừa mở đã đóng trước khi kịp dán lệnh.");
        return;
    }

    // SetForegroundWindow có thể bị Windows từ chối (chống cướp focus) mà không báo gì hữu ích - kiểm
    // tra KẾT QUẢ THẬT ngay trước khi gửi phím: SendInput gửi vào cửa sổ đang nhận bàn phím, dù là cửa
    // sổ nào.
    if (GetForegroundWindow() == hwnd)
    {
        m_foregroundAttemptsLeft = 0;
        InputController input;
        input.hotkey(/*ctrl=*/true, /*alt=*/false, /*shift=*/false, /*win=*/false, 'V');
        emit finished(true, QString());
        return;
    }

    if (--m_foregroundAttemptsLeft <= 0)
    {
        emit finished(false, QString("Đã mở PowerShell nhưng Windows không cho đưa cửa sổ đó lên phía trước - "
                                     "KHÔNG tự dán, để tránh dán lệnh nhầm vào một cửa sổ khác.") +
                                 kManualPasteHint);
        return;
    }
    SetForegroundWindow(hwnd);
    QTimer::singleShot(150, this, &CommandLauncher::pasteWhenForeground);
}

namespace CommandLauncherInternal
{

QString prepareForPaste(const QString& command)
{
    QString text = command;
    text.replace("\r\n", "\n");
    text.replace('\r', '\n');
    text.replace(QChar(0x2028), '\n'); // LINE SEPARATOR
    text.replace(QChar(0x2029), '\n'); // PARAGRAPH SEPARATOR

    int end = text.size();
    while (end > 0 && text[end - 1].isSpace())
        --end;
    text.truncate(end);

    // Bỏ các dòng trắng ở đầu (giữ thụt lề của dòng lệnh đầu tiên nếu có).
    int start = 0;
    for (int i = 0; i < text.size() && text[i].isSpace(); ++i)
        if (text[i] == '\n')
            start = i + 1;
    return text.mid(start);
}

int lineCount(const QString& prepared)
{
    return prepared.isEmpty() ? 0 : static_cast<int>(prepared.count('\n')) + 1;
}

QStringList pasteWarnings(const QString& prepared, bool appIsElevated)
{
    QStringList warnings;
    const int lines = lineCount(prepared);
    if (lines > 1)
    {
        warnings << QString("Lệnh có %1 dòng. Khi dán nhiều dòng vào console, tùy cấu hình PowerShell trên máy "
                            "(không có PSReadLine, console kiểu cũ, chế độ trình đọc màn hình...) các dòng TRƯỚC "
                            "dòng cuối có thể TỰ CHẠY NGAY lúc dán, không chờ bạn bấm Enter.")
                        .arg(lines);
    }
    if (appIsElevated)
    {
        warnings << "Ứng dụng đang chạy với quyền Administrator nên cửa sổ PowerShell mở ra CŨNG có quyền "
                    "Administrator - lệnh dán vào đó sẽ chạy với toàn quyền trên máy.";
    }
    return warnings;
}

} // namespace CommandLauncherInternal
