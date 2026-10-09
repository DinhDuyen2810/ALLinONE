#include "AdbDeviceLister.h"

#include "AdbController.h"

#include <QProcess>
#include <QTimer>

namespace
{
// Cùng giới hạn với AdbController::listDevices() (runAdb mặc định 15 giây).
constexpr int kListTimeoutMs = 15000;
}

AdbDeviceLister::AdbDeviceLister(QObject* parent)
    : QObject(parent)
{
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(kListTimeoutMs);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        // adb treo - buộc dừng; tín hiệu finished() của QProcess (CrashExit) sẽ tới ngay sau và báo lỗi.
        m_timedOut = true;
        if (m_process)
            m_process->kill();
    });
}

AdbDeviceLister::~AdbDeviceLister()
{
    cancel();
}

bool AdbDeviceLister::isBusy() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool AdbDeviceLister::refresh()
{
    if (isBusy())
        return false; // không chồng lệnh - lần đang chạy sẽ tự báo kết quả

    const QString adbPath = AdbController::adbExecutablePath();
    if (adbPath.isEmpty())
    {
        emit listed({}, "Không tìm thấy adb.exe (thư mục scrcpy/ cạnh file chạy bị thiếu).");
        return true;
    }

    if (!m_process)
    {
        m_process = new QProcess(this);
        connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this](int exitCode, QProcess::ExitStatus status) {
                    m_timeoutTimer->stop();
                    if (m_cancelled)
                        return; // bị cancel() - không báo kết quả

                    // CHỈ phân tích stdout - xem ghi chú ở runAdb() trong AdbController.cpp.
                    const QString out = QString::fromLocal8Bit(m_process->readAllStandardOutput());
                    const QString err = QString::fromLocal8Bit(m_process->readAllStandardError()).trimmed();
                    if (m_timedOut)
                        finishWithError("Hết thời gian chờ adb.");
                    else if (status != QProcess::NormalExit || exitCode != 0)
                        finishWithError(!err.isEmpty() ? err : QString("adb thoát với mã lỗi %1").arg(exitCode));
                    else
                        emit listed(AdbController::internal::parseDevicesOutput(out), QString());
                });
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError processError) {
            // Chỉ FailedToStart là KHÔNG kèm tín hiệu finished() - các lỗi khác (Crashed khi bị kill...)
            // đã được xử lý ở finished() phía trên.
            if (processError != QProcess::FailedToStart)
                return;
            m_timeoutTimer->stop();
            if (!m_cancelled)
                finishWithError("Không khởi chạy được adb.exe.");
        });
    }

    m_timedOut = false;
    m_cancelled = false;
    m_process->setProgram(adbPath);
    m_process->setArguments({"devices", "-l"});
    m_process->start();
    m_timeoutTimer->start();
    return true;
}

void AdbDeviceLister::cancel()
{
    m_timeoutTimer->stop();
    if (!isBusy())
        return;
    m_cancelled = true;
    m_process->kill();
    m_process->waitForFinished(1000);
}

void AdbDeviceLister::finishWithError(const QString& error)
{
    emit listed({}, error);
}
