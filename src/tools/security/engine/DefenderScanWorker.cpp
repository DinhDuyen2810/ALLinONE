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
    // có thể mất rất lâu, timeout mặc định sẽ hủy quét giữa chừng một cách vô lý.
    PowerShellRunner::run(script, &ok, &error, /*timeoutMs=*/60 * 60 * 1000); // tối đa 1 giờ
    emit scanFinished(ok, error);
}
