#include "ScrcpyLauncher.h"

#include "AdbController.h"
#include "core/WinProcessTree.h"

#include <QProcess>

#include <vector>

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
                        WinProcessTree::terminateDescendants(m_lastPid);
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

    // Liệt kê hậu duệ TRƯỚC khi buộc dừng - lúc này m_lastPid CHẮC CHẮN vẫn là chính scrcpy.exe của ta
    // (chưa ai kịp tái sử dụng PID), tránh rủi ro quét SAU khi nó đã thoát có thể khớp nhầm hậu duệ của
    // một tiến trình không liên quan lỡ được Windows cấp trùng đúng PID đó (xem core/WinProcessTree.h).
    const std::vector<qint64> descendants =
        m_lastPid > 0 ? WinProcessTree::findDescendants(m_lastPid) : std::vector<qint64>{};

    m_process->terminate();
    if (!m_process->waitForFinished(3000))
        m_process->kill();

    // Ta vừa CHỦ ĐỘNG buộc dừng scrcpy.exe (terminate()/kill() ở trên) thay vì để nó tự thoát theo luồng
    // chuẩn của chính nó - scrcpy không có cơ hội tự dọn tiến trình adb.exe con, nên LUÔN dừng tiếp danh
    // sách đã chụp sẵn ở trên (dùng chung lớp lỗi với YtDlpDownloadWorker: yt-dlp.exe cũng tự sinh
    // ffmpeg.exe làm con lúc ghép video).
    WinProcessTree::terminateProcessList(descendants);
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
