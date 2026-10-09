#include "AdbPairer.h"

#include "AdbController.h"

#include <QProcess>
#include <QTimer>

namespace
{
// Cùng giới hạn với AdbController::pairWireless() (runAdb 20 giây).
constexpr int kPairTimeoutMs = 20000;
}

AdbPairer::AdbPairer(QObject* parent)
    : QObject(parent)
{
    m_timeoutTimer = new QTimer(this);
    m_timeoutTimer->setSingleShot(true);
    m_timeoutTimer->setInterval(kPairTimeoutMs);
    connect(m_timeoutTimer, &QTimer::timeout, this, [this]() {
        // adb treo - buộc dừng; tín hiệu finished() của QProcess (CrashExit) sẽ tới ngay sau và báo lỗi.
        m_timedOut = true;
        if (m_process)
            m_process->kill();
    });
}

AdbPairer::~AdbPairer()
{
    cancel();
}

bool AdbPairer::isBusy() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

bool AdbPairer::pair(const QString& ipAndPairPort, const QString& pairingCode)
{
    if (isBusy())
        return false; // không chồng lệnh - lần đang chạy sẽ tự báo kết quả

    // Hai chuỗi này trở thành ĐỐI SỐ dòng lệnh của adb.exe - kiểm định dạng trước khi gọi, xem
    // AdbController::isValidIpAndPort(). Cùng thông báo với AdbController::pairWireless().
    if (!AdbController::isValidIpAndPort(ipAndPairPort))
    {
        emit finished(false, "Địa chỉ ghép đôi không hợp lệ - cần dạng IP:Cổng, vd 192.168.1.23:41234.");
        return true;
    }
    if (!AdbController::isValidPairingCode(pairingCode))
    {
        emit finished(false, "Mã ghép đôi không hợp lệ - cần đúng 6 chữ số hiển thị trên điện thoại.");
        return true;
    }

    const QString adbPath = AdbController::adbExecutablePath();
    if (adbPath.isEmpty())
    {
        emit finished(false, "Không tìm thấy adb.exe (thư mục scrcpy/ cạnh file chạy bị thiếu).");
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

                    const QString out = QString::fromLocal8Bit(m_process->readAllStandardOutput());
                    const QString err = QString::fromLocal8Bit(m_process->readAllStandardError());
                    if (m_timedOut)
                    {
                        emit finished(false, "Hết thời gian chờ adb.");
                        return;
                    }
                    QString error;
                    const bool ok = AdbController::internal::interpretPairResult(status == QProcess::NormalExit, exitCode,
                                                                                 out, err, &error);
                    emit finished(ok, error);
                });
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError processError) {
            // Chỉ FailedToStart là KHÔNG kèm tín hiệu finished() - các lỗi khác (Crashed khi bị kill...)
            // đã được xử lý ở finished() phía trên.
            if (processError != QProcess::FailedToStart)
                return;
            m_timeoutTimer->stop();
            if (!m_cancelled)
                emit finished(false, "Không khởi chạy được adb.exe.");
        });
    }

    m_timedOut = false;
    m_cancelled = false;
    m_process->setProgram(adbPath);
    m_process->setArguments({"pair", ipAndPairPort, pairingCode});
    m_process->start();
    m_timeoutTimer->start();
    return true;
}

void AdbPairer::cancel()
{
    m_timeoutTimer->stop();
    if (!isBusy())
        return;
    m_cancelled = true;
    m_process->kill();
    m_process->waitForFinished(1000);
}
