#include "ScrcpyLauncher.h"

#include "AdbController.h"

#include <QProcess>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>

#include <utility>
#include <vector>

namespace
{
/// scrcpy.exe tự khởi chạy "adb.exe shell ..." làm tiến trình CON trong suốt phiên gương (để giữ server
/// trên điện thoại sống + đọc log đầu ra) - nhưng trên Windows, buộc dừng một tiến trình cha
/// (TerminateProcess, chính là những gì QProcess::kill() gọi) KHÔNG đệ quy xuống dừng luôn các tiến
/// trình con của nó (khác hẳn gửi SIGTERM cho cả group tiến trình trên Linux). Nếu không tự quét và dừng
/// tiếp, adb.exe con này mồ côi (orphan) và treo lại NGẦM vô thời hạn - đúng triệu chứng người dùng báo:
/// "adb.exe (PID ...) vẫn không dừng lại kể cả khi đã tắt ứng dụng". Phải tự liệt kê toàn bộ tiến trình
/// đang chạy trên máy và tìm mọi hậu duệ (con, cháu, ...) của PID scrcpy.exe để tự tay dừng từng cái.
void terminateProcessTree(DWORD rootPid)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);

    // Lấy toàn bộ (pid, ppid) một lần - danh sách tiến trình hệ thống không lớn tới mức dò lại nhiều
    // vòng (tìm hậu duệ nhiều cấp) gây vấn đề hiệu năng, và tránh phải mở lại snapshot mỗi vòng lặp.
    std::vector<std::pair<DWORD, DWORD>> all; // (pid, ppid)
    if (Process32FirstW(snapshot, &entry))
    {
        do { all.emplace_back(entry.th32ProcessID, entry.th32ParentProcessID); }
        while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    // BFS tìm mọi hậu duệ của rootPid - không chỉ con trực tiếp, phòng trường hợp scrcpy/adb tự phân
    // tầng thêm tiến trình trung gian ở phiên bản khác với bản đang đóng gói.
    std::vector<DWORD> toKill;
    std::vector<DWORD> frontier{rootPid};
    while (!frontier.empty())
    {
        const DWORD parent = frontier.back();
        frontier.pop_back();
        for (const auto& pair : all)
        {
            if (pair.second != parent)
                continue;
            bool already = false;
            for (DWORD pid : toKill)
                if (pid == pair.first) { already = true; break; }
            if (already)
                continue;
            toKill.push_back(pair.first);
            frontier.push_back(pair.first);
        }
    }

    for (DWORD pid : toKill)
    {
        HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (h)
        {
            TerminateProcess(h, 1);
            CloseHandle(h);
        }
    }
}
} // namespace

ScrcpyLauncher::ScrcpyLauncher(QObject* parent)
    : QObject(parent)
{
}

ScrcpyLauncher::~ScrcpyLauncher()
{
    stop();
}

bool ScrcpyLauncher::start(const ScrcpyOptions& options, QString* error)
{
    if (isRunning())
    {
        if (error) *error = "Đang có một phiên điều khiển chạy rồi.";
        return false;
    }

    const QString scrcpyPath = AdbController::scrcpyExecutablePath();
    if (scrcpyPath.isEmpty())
    {
        if (error) *error = "Không tìm thấy scrcpy.exe (thư mục scrcpy/ cạnh file chạy bị thiếu).";
        return false;
    }

    if (!m_process)
    {
        m_process = new QProcess(this);
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](int exitCode, QProcess::ExitStatus status) {
                    // scrcpy.exe thoát KHÔNG bình thường (crash, bị dừng từ bên ngoài như Task Manager,
                    // rút cáp/mất kết nối giữa chừng gây lỗi...) có thể không kịp tự dọn tiến trình
                    // adb.exe con của nó - quét dọn phòng hờ y hệt lý do trong stop() (không tốn gì nếu
                    // không còn tiến trình con nào phải dọn). Thoát BÌNH THƯỜNG thì bỏ qua: scrcpy tự dọn
                    // đúng con của nó trong luồng thoát chuẩn của chính nó.
                    if (status != QProcess::NormalExit && m_lastPid > 0)
                        terminateProcessTree(static_cast<DWORD>(m_lastPid));
                    emit finished(exitCode);
                });
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            emit errorOccurred(m_process->errorString());
        });
    }

    // scrcpy.exe cần adb.exe cùng thư mục (đã đóng gói chung) - không cần set thêm biến môi trường gì,
    // scrcpy tự tìm "adb" cạnh chính nó trước khi tìm trong PATH hệ thống.
    m_process->setProgram(scrcpyPath);
    m_process->setArguments(ScrcpyLauncherInternal::buildArguments(options));
    m_process->start();

    if (!m_process->waitForStarted(5000))
    {
        if (error) *error = "Không khởi chạy được scrcpy.exe.";
        return false;
    }

    m_lastPid = m_process->processId();

    emit started();
    return true;
}

void ScrcpyLauncher::stop()
{
    if (!m_process || m_process->state() == QProcess::NotRunning)
        return;
    m_process->terminate();
    if (!m_process->waitForFinished(3000))
        m_process->kill();

    // Ta vừa CHỦ ĐỘNG buộc dừng scrcpy.exe (terminate()/kill() ở trên) thay vì để nó tự thoát theo luồng
    // chuẩn của chính nó - scrcpy không có cơ hội tự dọn tiến trình adb.exe con, nên LUÔN quét dọn ở đây
    // (xem terminateProcessTree() vì sao Windows không tự làm việc này thay ta).
    if (m_lastPid > 0)
        terminateProcessTree(static_cast<DWORD>(m_lastPid));
}

bool ScrcpyLauncher::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

namespace ScrcpyLauncherInternal
{

QStringList buildArguments(const ScrcpyOptions& options)
{
    QStringList args;

    if (options.switchToWireless)
        args << "--tcpip"; // tự dò IP thiết bị hiện tại rồi chuyển sang không dây trước khi gương
    if (!options.serial.isEmpty())
        args << "-s" << options.serial;

    if (options.maxSize > 0)
        args << QString("--max-size=%1").arg(options.maxSize);
    if (options.bitRateMbps > 0)
        args << QString("--video-bit-rate=%1M").arg(options.bitRateMbps);
    if (options.maxFps > 0)
        args << QString("--max-fps=%1").arg(options.maxFps);

    if (options.stayAwake)
        args << "--stay-awake";
    if (options.turnScreenOff)
        args << "--turn-screen-off";
    if (options.alwaysOnTop)
        args << "--always-on-top";
    if (options.fullscreen)
        args << "--fullscreen";
    if (options.disableAudio)
        args << "--no-audio";
    if (!options.recordFilePath.isEmpty())
        args << QString("--record=%1").arg(options.recordFilePath);
    if (!options.windowTitle.isEmpty())
        args << QString("--window-title=%1").arg(options.windowTitle);

    return args;
}

} // namespace ScrcpyLauncherInternal
