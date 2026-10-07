#include "CommandLauncher.h"

#include "../../autoclick/engine/InputController.h"

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
    // Đặt Clipboard TRƯỚC khi mở cửa sổ mới - QClipboard chỉ dùng được an toàn trên luồng giao diện.
    QGuiApplication::clipboard()->setText(command);

    qint64 pid = 0;
    const bool started = QProcess::startDetached("powershell.exe", {}, QString(), &pid);
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
            emit finished(false, "Đã mở PowerShell nhưng không tìm thấy cửa sổ để dán lệnh vào trong 3 giây "
                                  "- có thể đã bị Windows chặn SetForegroundWindow, vui lòng tự dán (Ctrl+V).");
        }
        return;
    }

    m_pollTimer->stop();
    SetForegroundWindow(hwnd);

    // Đợi cửa sổ thật sự nhận focus rồi mới gửi Ctrl+V - gửi ngay lúc vừa SetForegroundWindow đôi khi
    // bị console bỏ lỡ vì chưa kịp bắt đầu đọc input.
    QTimer::singleShot(200, this, [this]() {
        InputController input;
        input.hotkey(/*ctrl=*/true, /*alt=*/false, /*shift=*/false, /*win=*/false, 'V');
        emit finished(true, QString());
    });
}
