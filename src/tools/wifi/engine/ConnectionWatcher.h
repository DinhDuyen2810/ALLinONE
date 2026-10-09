#pragma once

#include <QObject>
#include <QString>

class QTimer;
class WlanController;

/**
 * @brief Theo dõi kết quả của MỘT yêu cầu kết nối bằng hồ sơ đã lưu (WlanConnect chỉ "gửi yêu cầu", thành
 * hay bại diễn ra sau đó). Polling WlanController::currentConnection() bằng QTimer - cùng cách ConnectDialog
 * và NetworksTab đã làm, không đăng ký callback WLAN.
 *
 * CHỈ quan sát: lớp này không bao giờ tự gửi yêu cầu kết nối, không thử lại, không đụng tới mật khẩu.
 */
class ConnectionWatcher : public QObject
{
    Q_OBJECT

public:
    explicit ConnectionWatcher(WlanController* controller, QObject* parent = nullptr);

    /// Bắt đầu theo dõi (thay cho lần theo dõi đang dở nếu có). Coi là thành công khi adapter báo đã kết
    /// nối và SSID trùng ssid HOẶC tên hồ sơ đang dùng trùng profileName (mạng ẩn chưa biết SSID lúc quét).
    void watch(const QString& adapterGuid, const QString& ssid, const QString& profileName);
    void cancel();
    bool isActive() const;

    /// Quy tắc dừng dùng chung (cũng dùng trong ConnectDialog), tách riêng để kiểm thử không cần phần cứng:
    /// chờ tối thiểu softLimitMs; quá mốc đó mà adapter vẫn đang liên kết/xác thực thì chờ tiếp, nhưng
    /// không quá hardLimitMs.
    static bool shouldGiveUp(int elapsedMs, bool stillConnecting, int softLimitMs = kSoftLimitMs,
                             int hardLimitMs = kHardLimitMs)
    {
        if (elapsedMs >= hardLimitMs)
            return true;
        return elapsedMs >= softLimitMs && !stillConnecting;
    }

    static constexpr int kPollIntervalMs = 500;
    static constexpr int kSoftLimitMs = 15000;
    static constexpr int kHardLimitMs = 30000;

signals:
    void succeeded(const QString& ssid);
    void failed(const QString& ssid);

private:
    void onTick();

    WlanController* m_controller;
    QTimer* m_timer;
    QString m_adapterGuid;
    QString m_ssid;
    QString m_profileName;
    int m_elapsedMs{0};
};
