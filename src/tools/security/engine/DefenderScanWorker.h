#pragma once

#include "DefenderController.h"

#include <QString>
#include <QThread>

/// Chạy Start-MpScan trên QThread riêng - cmdlet này ĐỒNG BỘ/CHẶN tới khi quét xong (đã xác nhận qua
/// tài liệu API Win32 MpScanStart mà PowerShell bọc lại), FullScan có thể mất rất lâu (hàng chục phút
/// tùy dung lượng ổ đĩa) nên TUYỆT ĐỐI không được gọi trực tiếp từ luồng giao diện - cùng mẫu đã dùng
/// cho CleanupScanner/LargeFileScanner/PartitionResizer.
class DefenderScanWorker : public QThread
{
    Q_OBJECT

public:
    explicit DefenderScanWorker(QObject* parent = nullptr);

    void setScan(DefenderController::ScanType type, const QString& customPath = QString());

signals:
    /// Đổi tên khỏi `finished` - trùng tín hiệu có sẵn của QThread (cùng bài học đã áp dụng cho
    /// CleanupExecutor/VpnConnector).
    void scanFinished(bool ok, QString error);

protected:
    void run() override;

private:
    DefenderController::ScanType m_type{DefenderController::ScanType::Quick};
    QString m_customPath;
};
