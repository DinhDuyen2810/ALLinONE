#pragma once

#include "DefenderController.h"

#include <QString>
#include <QThread>
#include <atomic>

/// Chạy Start-MpScan trên QThread riêng - cmdlet này ĐỒNG BỘ/CHẶN tới khi quét xong (đã xác nhận qua
/// tài liệu API Win32 MpScanStart mà PowerShell bọc lại), FullScan có thể mất rất lâu (hàng chục phút
/// tới hàng giờ tùy dung lượng ổ đĩa) nên TUYỆT ĐỐI không được gọi trực tiếp từ luồng giao diện - cùng
/// mẫu đã dùng cho CleanupScanner/LargeFileScanner/PartitionResizer.
///
/// Hỗ trợ HỦY GIỮA CHỪNG qua requestCancel() (xem PowerShellRunner::runCancelable) - bắt buộc phải có
/// vì nếu không, cửa sổ chứa worker này phải chờ cả tiếng mới đóng được an toàn, hoặc tệ hơn là bị đóng
/// trong lúc QThread vẫn đang thực sự chạy (hành vi KHÔNG XÁC ĐỊNH theo tài liệu Qt) - lỗi này đã được
/// chủ động phát hiện khi tự rà soát toàn bộ ứng dụng, không phải do người dùng báo.
class DefenderScanWorker : public QThread
{
    Q_OBJECT

public:
    explicit DefenderScanWorker(QObject* parent = nullptr);

    void setScan(DefenderController::ScanType type, const QString& customPath = QString());

    /// Yêu cầu dừng quét sớm - an toàn (tương đương bấm "Hủy" trên Windows Security, Defender tự dừng
    /// gọn). Gọi xong vẫn phải wait() để chắc chắn luồng đã thoát hẳn trước khi hủy đối tượng này.
    void requestCancel() { m_cancelRequested = true; }

signals:
    /// Đổi tên khỏi `finished` - trùng tín hiệu có sẵn của QThread (cùng bài học đã áp dụng cho
    /// CleanupExecutor/VpnConnector).
    void scanFinished(bool ok, QString error);

protected:
    void run() override;

private:
    DefenderController::ScanType m_type{DefenderController::ScanType::Quick};
    QString m_customPath;
    std::atomic_bool m_cancelRequested{false};
};
