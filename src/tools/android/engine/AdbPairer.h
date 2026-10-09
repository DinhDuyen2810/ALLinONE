#pragma once

#include <QObject>
#include <QString>

class QProcess;
class QTimer;

/**
 * @brief Chạy `adb pair <IP:cổng> <mã>` BẤT ĐỒNG BỘ (QProcess + tín hiệu), cùng mẫu với AdbDeviceLister.
 *
 * Trước đây DevicesTab gọi AdbController::pairWireless() ĐỒNG BỘ ngay trên luồng giao diện: adb phải bắt
 * tay TLS với điện thoại qua mạng, sai địa chỉ/điện thoại đã tắt màn hình ghép đôi thì lệnh chỉ trả về khi
 * hết 20 giây chờ (+5 giây chờ khởi chạy) - TOÀN BỘ ứng dụng đứng hình suốt thời gian đó.
 *
 * Không bao giờ chồng lệnh: pair() khi lần trước chưa xong chỉ trả về false.
 */
class AdbPairer : public QObject
{
    Q_OBJECT

public:
    explicit AdbPairer(QObject* parent = nullptr);
    ~AdbPairer() override;

    /// Bắt đầu một lần ghép đôi. Trả về false (không làm gì) nếu lần trước chưa xong. Kết quả - kể cả lỗi
    /// định dạng (kiểm bằng AdbController::isValidIpAndPort/isValidPairingCode TRƯỚC khi gọi adb, y như
    /// AdbController::pairWireless), "không tìm thấy adb.exe", "không khởi chạy được" - luôn tới qua tín
    /// hiệu finished(); ba lỗi đầu được phát NGAY trong lời gọi này.
    bool pair(const QString& ipAndPairPort, const QString& pairingCode);
    bool isBusy() const;

    /// Hủy lần ghép đôi đang chạy (nếu có) - KHÔNG phát finished() cho lần bị hủy.
    void cancel();

signals:
    /// `ok` = adb báo "Successfully paired"; khi thất bại `error` là lý do để hiện cho người dùng.
    void finished(bool ok, const QString& error);

private:
    QProcess* m_process{nullptr};
    QTimer* m_timeoutTimer{nullptr};
    bool m_timedOut{false};
    bool m_cancelled{false};
};
