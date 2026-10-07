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
}

void DefenderScanWorker::run()
{
    const QString script = DefenderController::internal::buildStartScanScript(m_type, m_customPath);
    bool ok = false;
    QString error;
    // Không đặt timeout ngắn ở đây (khác các lệnh PowerShell nhanh khác dùng mặc định 20s) - FullScan
    // có thể mất rất lâu, timeout mặc định sẽ hủy quét giữa chừng một cách vô lý. Dùng runCancelable (thay
    // vì run()) để requestCancel() có thể dừng sớm một cách an toàn thay vì phải đợi hết 1 giờ.
    PowerShellRunner::runCancelable(script, &m_cancelRequested, &ok, &error, /*timeoutMs=*/60 * 60 * 1000);
    emit scanFinished(ok, error);
}
