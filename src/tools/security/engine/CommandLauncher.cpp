#include "CommandLauncher.h"

#include "ConsolePaste.h"
#include "core/AppPaths.h"
#include "core/Logger.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
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

    // Cửa sổ console do conhost vẽ nhưng Windows báo chủ sở hữu là tiến trình đầu tiên gắn vào console (ở đây
    // là powershell.exe) - đo thật: tìm thấy sau ~125 ms. Dùng Windows Terminal làm trình console mặc định thì
    // cửa sổ thuộc WindowsTerminal.exe, không tìm thấy - chỉ mất bước đưa lên trước, việc dán không ảnh hưởng.
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid == data->pid)
    {
        data->found = hwnd;
        return FALSE;
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

/// Mã thoát khi tiến trình bị kết thúc do console của nó đóng (CTRL_CLOSE_EVENT) hoặc Ctrl+C.
constexpr int kStatusControlCExit = static_cast<int>(0xC000013Au);
} // namespace

CommandLauncher::CommandLauncher(QObject* parent)
    : QObject(parent)
    , m_watchdog(new QTimer(this))
    , m_windowTimer(new QTimer(this))
{
    m_watchdog->setSingleShot(true);
    connect(m_watchdog, &QTimer::timeout, this, [this] {
        if (m_helper)
        {
            m_helper->disconnect(this);
            m_helper->kill();
        }
        Logger::instance().warning("SecurityGateway", "Tiến trình phụ dán lệnh không phản hồi - đã dừng");
        finish(false, QString("Đã mở PowerShell nhưng bước dán tự động không phản hồi.") +
                          CommandLauncherInternal::manualPasteHint(m_lines));
    });

    m_windowTimer->setInterval(100);
    connect(m_windowTimer, &QTimer::timeout, this, &CommandLauncher::bringWindowToFront);
}

CommandLauncher::~CommandLauncher()
{
    // Cửa sổ PowerShell là của người dùng - KHÔNG đóng nó. Tiến trình phụ (nếu còn) là con của QObject này,
    // QProcess tự kết thúc nó khi bị hủy.
    if (m_helper)
        m_helper->disconnect(this);
    closeProcessHandle();
}

void CommandLauncher::closeProcessHandle()
{
    if (m_process)
    {
        CloseHandle(static_cast<HANDLE>(m_process));
        m_process = nullptr;
    }
}

void CommandLauncher::launchAndPaste(const QString& command)
{
    if (m_busy)
        return; // lần mở trước chưa xong - không mở chồng thêm cửa sổ thứ hai

    const QString text = CommandLauncherInternal::prepareForPaste(command);
    if (text.isEmpty())
    {
        emit finished(false, "Lệnh trống.");
        return;
    }
    const int lines = CommandLauncherInternal::lineCount(text);
    m_lines = lines;
    m_hasTabs = text.contains('\t');

    m_busy = true;
    closeProcessHandle();
    m_pid = 0;

    // Đặt Clipboard TRƯỚC khi mở cửa sổ mới - QClipboard chỉ dùng được an toàn trên luồng giao diện.
    QGuiApplication::clipboard()->setText(text);

    // Đường dẫn tuyệt đối trong System32 - không để Windows tự tìm "powershell.exe" theo thư mục ứng
    // dụng/CWD/PATH (xem AppPaths::systemExecutable).
    const QString exe = QDir::toNativeSeparators(AppPaths::powershellExecutable());
    QString commandLine = CommandLauncherInternal::buildCommandLine(exe, m_test.powershellArgs);
    // Mở ở thư mục người dùng như khi mở PowerShell từ menu Start - không mở trong thư mục cài ứng dụng.
    const QString workDir = QDir::toNativeSeparators(QDir::homePath());

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESHOWWINDOW;
    startup.wShowWindow = m_test.minimizedNoActivate ? SW_SHOWMINNOACTIVE : SW_SHOWNORMAL;
    PROCESS_INFORMATION info{};
    // Không kế thừa handle nào của ứng dụng; CREATE_NEW_CONSOLE = console MỚI có cửa sổ riêng.
    const DWORD flags = CREATE_UNICODE_ENVIRONMENT | (m_test.hiddenConsole ? CREATE_NO_WINDOW : CREATE_NEW_CONSOLE);
    std::wstring cmdBuffer = commandLine.toStdWString();
    const std::wstring exeW = exe.toStdWString();
    const std::wstring dirW = workDir.toStdWString();
    if (!CreateProcessW(exeW.c_str(), cmdBuffer.data(), nullptr, nullptr, FALSE, flags, nullptr,
                        QDir(workDir).exists() ? dirW.c_str() : nullptr, &startup, &info))
    {
        const DWORD error = GetLastError();
        Logger::instance().warning("SecurityGateway", QString("Không mở được powershell.exe (mã lỗi %1)").arg(error));
        m_busy = false;
        emit finished(false, QString("Không mở được powershell.exe (mã lỗi Windows %1).").arg(error));
        return;
    }
    CloseHandle(info.hThread);
    m_process = info.hProcess;
    m_pid = info.dwProcessId;
    // Không ghi nội dung lệnh vào log (có thể chứa bí mật/mã độc) - chỉ ghi độ dài.
    Logger::instance().info("SecurityGateway", QString("Đã mở PowerShell (PID %1), dán lệnh %2 dòng / %3 ký tự")
                                                   .arg(m_pid).arg(lines).arg(text.size()));

    if (!m_test.hiddenConsole && !m_test.minimizedNoActivate)
    {
        m_windowAttemptsLeft = 30; // tối đa 3 giây chờ cửa sổ console xuất hiện để đưa lên trước
        m_windowTimer->start();
    }

    const int timeoutMs = m_test.helperTimeoutMs > 0 ? m_test.helperTimeoutMs : ConsolePaste::kDefaultTimeoutMs;
    const QString helperProgram =
        m_test.helperProgram.isEmpty() ? QCoreApplication::applicationFilePath() : m_test.helperProgram;

    m_helper = new QProcess(this);
    m_helper->setProgram(helperProgram);
    m_helper->setArguments({QString::fromLatin1(ConsolePaste::kHelperSwitch), QString::number(m_pid),
                            QString::number(timeoutMs)});
    m_helper->setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_helper->setStandardOutputFile(QProcess::nullDevice());
    const QByteArray payload = text.toUtf8();
    connect(m_helper, &QProcess::started, this, [this, payload] {
        // Lệnh cần đối chiếu đi qua stdin, KHÔNG qua dòng lệnh (dòng lệnh hiện trong danh sách tiến trình/log
        // của phần mềm bảo mật - lệnh khả nghi nằm ở đó dễ bị báo động nhầm).
        m_helper->write(payload);
        m_helper->closeWriteChannel();
    });
    connect(m_helper, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        onHelperFinished(exitCode, status == QProcess::CrashExit);
    });
    connect(m_helper, &QProcess::errorOccurred, this, [this, lines](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return; // các lỗi khác đi kèm finished()
        Logger::instance().warning("SecurityGateway",
                                   QString("Không chạy được tiến trình phụ dán lệnh: %1").arg(m_helper->errorString()));
        finish(false, QString("Đã mở PowerShell nhưng không chạy được bước dán tự động (%1).")
                              .arg(m_helper->errorString()) +
                          CommandLauncherInternal::manualPasteHint(lines));
    });
    m_watchdog->start(timeoutMs + 10000);
    m_helper->start();
}

void CommandLauncher::onHelperFinished(int exitCode, bool crashed)
{
    if (!m_busy)
        return;
    const int timeoutSeconds =
        (m_test.helperTimeoutMs > 0 ? m_test.helperTimeoutMs : ConsolePaste::kDefaultTimeoutMs) / 1000;
    const auto result = CommandLauncherInternal::helperResult(exitCode, crashed, m_lines, timeoutSeconds, m_hasTabs);
    Logger::instance().info("SecurityGateway", QString("Kết quả dán vào PowerShell (PID %1): mã %2%3")
                                                   .arg(m_pid).arg(exitCode).arg(crashed ? " (bị kết thúc)" : ""));
    finish(result.ok, result.message);
}

void CommandLauncher::finish(bool ok, const QString& message)
{
    if (!m_busy)
        return;
    m_busy = false;
    m_watchdog->stop();
    if (m_helper)
    {
        m_helper->disconnect(this);
        m_helper->deleteLater(); // QProcess đã thoát thì xóa ngay; còn chạy (lỗi khởi động/quá giờ) thì nó tự dừng
        m_helper = nullptr;
    }
    emit finished(ok, message);
}

void CommandLauncher::bringWindowToFront()
{
    if (m_pid == 0 || --m_windowAttemptsLeft < 0)
    {
        m_windowTimer->stop();
        return;
    }
    if (HWND hwnd = findTopLevelWindowByPid(static_cast<DWORD>(m_pid)))
    {
        m_windowTimer->stop();
        // Chỉ để tiện cho người dùng - Windows có thể từ chối (chống cướp focus) nhưng việc dán không cần cửa
        // sổ ở phía trước (ConsolePaste ghi thẳng vào bộ đệm nhập của console).
        SetForegroundWindow(hwnd);
    }
}

bool CommandLauncher::terminateLastPowerShellForTest()
{
    if (!m_process)
        return true;
    auto* handle = static_cast<HANDLE>(m_process);
    if (WaitForSingleObject(handle, 0) != WAIT_OBJECT_0)
    {
        TerminateProcess(handle, 0);
        WaitForSingleObject(handle, 5000);
    }
    closeProcessHandle();
    return true;
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
    Q_UNUSED(prepared);
    QStringList warnings;
    if (appIsElevated)
    {
        warnings << "Ứng dụng đang chạy với quyền Administrator nên cửa sổ PowerShell mở ra CŨNG có quyền "
                    "Administrator - lệnh dán vào đó sẽ chạy với toàn quyền trên máy.";
    }
    return warnings;
}

QString manualPasteHint(int lines)
{
    QString hint = " Lệnh đã nằm sẵn trong Clipboard - bấm vào cửa sổ PowerShell rồi tự dán bằng Ctrl+V.";
    if (lines > 1)
        hint += " Lệnh nhiều dòng: đừng dán bằng chuột phải - cách đó có thể chạy ngay các dòng trước dòng cuối.";
    return hint;
}

HelperResult helperResult(int exitCode, bool crashed, int lines, int timeoutSeconds, bool hasTabs)
{
    using ConsolePaste::Outcome;
    const QString hint = manualPasteHint(lines);
    const QString tabNote = hasTabs ? " Lưu ý: PowerShell đã đổi mỗi ký tự Tab trong lệnh thành 4 dấu cách khi dán." : "";
    if (crashed || exitCode == kStatusControlCExit)
        return {false, "Cửa sổ PowerShell đã bị đóng trong lúc đang dán lệnh."};

    switch (static_cast<Outcome>(exitCode))
    {
        case Outcome::Pasted:
            return {true, "✓ Đã mở PowerShell và dán lệnh vào (đã đọc lại cửa sổ PowerShell để xác nhận) - lệnh "
                          "CHƯA chạy: tự xem lại rồi bấm Enter nếu muốn chạy." + tabNote};
        case Outcome::SentUnverified:
            return {true, "Đã mở PowerShell và gửi lệnh dán (lệnh không có chữ/số nào nên không tự xác nhận được) - "
                          "xem cửa sổ PowerShell; chưa thấy lệnh thì tự dán." + tabNote + hint};
        case Outcome::AttachFailed:
            return {false, "Đã mở PowerShell nhưng không gắn được vào cửa sổ của nó để dán lệnh." + hint};
        case Outcome::NotSeen:
            return {false, QString("Đã mở PowerShell và gửi lệnh dán nhưng sau %1 giây vẫn chưa thấy lệnh hiện ra "
                                   "(PowerShell nạp profile quá chậm?) - nếu lát nữa lệnh tự hiện thì không cần dán lại.")
                                   .arg(timeoutSeconds) +
                               hint};
        case Outcome::PromptIdle:
            return {false, "Đã mở PowerShell nhưng nó không xử lý phím dán Ctrl+V (PowerShell không dùng PSReadLine, "
                           "hoặc Ctrl+V đã được gán chức năng khác)." + hint};
        case Outcome::ProcessExited:
            return {false, "Cửa sổ PowerShell đã đóng trước khi kịp dán lệnh."};
        case Outcome::BadArguments:
        case Outcome::WriteFailed:
            break;
    }
    return {false, QString("Đã mở PowerShell nhưng bước dán tự động gặp lỗi (mã %1).").arg(exitCode) + hint};
}

QString buildCommandLine(const QString& exePath, const QStringList& args)
{
    QString line = '"' + exePath + '"';
    for (const QString& arg : args)
    {
        line += ' ';
        const bool needsQuotes = arg.isEmpty() || arg.contains(' ') || arg.contains('\t') || arg.contains('"');
        if (!needsQuotes)
        {
            line += arg;
            continue;
        }
        // Quy tắc tách tham số của Windows: dấu \ chỉ đặc biệt khi đứng ngay trước dấu " (hoặc trước dấu "
        // đóng) - nhân đôi chúng ở đó, còn " bên trong viết thành \".
        line += '"';
        int backslashes = 0;
        for (const QChar c : arg)
        {
            if (c == '\\')
            {
                ++backslashes;
                continue;
            }
            if (c == '"')
            {
                line += QString(backslashes * 2 + 1, '\\');
                line += '"';
            }
            else
            {
                line += QString(backslashes, '\\');
                line += c;
            }
            backslashes = 0;
        }
        line += QString(backslashes * 2, '\\');
        line += '"';
    }
    return line;
}

} // namespace CommandLauncherInternal
