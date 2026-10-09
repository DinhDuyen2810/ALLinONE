#include "DefenderScanWorker.h"

#include "core/PowerShellRunner.h"

DefenderScanWorker::DefenderScanWorker(QObject* parent)
    : QThread(parent)
{
}

void DefenderScanWorker::setScan(DefenderController::ScanType type, const QString& customPath)
{
    m_type = type;
    m_customPath = customPath;
    m_cancelRequested = false; // xem ghi chú ở DefenderScanWorker.h
}

void DefenderScanWorker::run()
{
    const QString script = DefenderController::internal::buildStartScanScript(m_type, m_customPath);
    bool ok = false;
    QString error;
    // Thời gian chờ theo loại quét (xem scanTimeoutMs) - KHÔNG dùng mặc định 20s của các lệnh PowerShell
    // nhanh khác. Dùng runCancelable (thay vì run()) để requestCancel() dừng chờ sớm được thay vì phải
    // đợi hết thời gian chờ.
    PowerShellRunner::runCancelable(script, &m_cancelRequested, &ok, &error,
                                    DefenderController::internal::scanTimeoutMs(m_type));
    emit scanFinished(ok, error);
}
