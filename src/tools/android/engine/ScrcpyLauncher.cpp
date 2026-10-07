#include "ScrcpyLauncher.h"

#include "AdbController.h"

#include <QProcess>

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
                [this](int exitCode, QProcess::ExitStatus) { emit finished(exitCode); });
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
