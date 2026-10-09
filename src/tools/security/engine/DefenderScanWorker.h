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

    /// Gọi TRƯỚC start() cho mỗi lần quét. Đồng thời xóa cờ hủy của lần quét trước - đối tượng này được
    /// TÁI DÙNG cho mọi lần quét, nếu không xóa thì chỉ cần hủy một lần là mọi lần quét sau đều bị dừng
    /// ngay sau ~200ms và báo "Đã hủy" (lỗi thật tìm thấy khi rà soát). Xóa ở đây (luồng giao diện, trước
    /// khi luồng quét chạy) chứ không phải đầu run(): xóa trong run() có thể nuốt mất một requestCancel()
    /// gọi ngay sau start() nhưng trước khi run() kịp bắt đầu.
    void setScan(DefenderController::ScanType type, const QString& customPath = QString());

    /// Yêu cầu dừng chờ sớm: tiến trình powershell.exe đang gọi Start-MpScan bị kết thúc và run() thoát
    /// trong vòng ~200ms. Gọi xong vẫn phải wait() để chắc chắn luồng đã thoát hẳn trước khi hủy đối
    /// tượng này. LƯU Ý: việc quét thật do dịch vụ Windows Defender thực hiện, không phải powershell.exe -
    /// CHƯA xác nhận được trên máy thật rằng kết thúc powershell.exe cũng dừng luôn lượt quét trong dịch
    /// vụ; Defender có thể vẫn quét tiếp trong nền cho tới khi xong (vô hại, chỉ tốn CPU).
    void requestCancel() { m_cancelRequested = true; }
    bool isCancelRequested() const { return m_cancelRequested.load(); }

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
