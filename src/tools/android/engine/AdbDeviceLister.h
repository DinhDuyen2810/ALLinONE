#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include "../model/AndroidDeviceInfo.h"

class QProcess;
class QTimer;

/**
 * @brief Chạy `adb devices -l` BẤT ĐỒNG BỘ (QProcess + tín hiệu), không chặn luồng giao diện.
 *
 * Trước đây DevicesTab gọi AdbController::listDevices() ĐỒNG BỘ ngay trên luồng GUI mỗi 3 giây: mỗi lần
 * là một lần waitForStarted()+waitForFinished() - bình thường vài chục ms, nhưng lần đầu (adb phải tự
 * khởi động daemon) mất vài giây, và khi adb treo (driver USB lỗi, daemon kẹt) thì TOÀN BỘ ứng dụng đứng
 * hình tới 15-20 giây, lặp lại sau mỗi 3 giây.
 *
 * Không bao giờ chồng lệnh: refresh() khi lệnh trước chưa xong chỉ trả về false.
 */
class AdbDeviceLister : public QObject
{
    Q_OBJECT

public:
    explicit AdbDeviceLister(QObject* parent = nullptr);
    ~AdbDeviceLister() override;

    /// Bắt đầu một lần liệt kê. Trả về false (không làm gì) nếu lần trước chưa xong. Kết quả - kể cả
    /// lỗi "không tìm thấy adb.exe"/"không khởi chạy được" - luôn tới qua tín hiệu listed().
    bool refresh();
    bool isBusy() const;

    /// Hủy lần liệt kê đang chạy (nếu có) - KHÔNG phát listed() cho lần bị hủy.
    void cancel();

signals:
    /// `error` rỗng = thành công (danh sách rỗng vẫn là kết quả hợp lệ: không có thiết bị nào).
    void listed(const QList<AndroidDeviceInfo>& devices, const QString& error);

private:
    void finishWithError(const QString& error);

    QProcess* m_process{nullptr};
    QTimer* m_timeoutTimer{nullptr};
    bool m_timedOut{false};
    bool m_cancelled{false};
};
