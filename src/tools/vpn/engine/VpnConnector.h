#pragma once

#include <QString>
#include <QThread>

/**
 * @brief Kết nối/ngắt kết nối một VPN đã khai báo trong Windows, qua `rasdial.exe` (công cụ dòng lệnh
 * có sẵn trong Windows từ thời RAS - KHÔNG cần cài gì thêm) trên QThread riêng vì có thể mất vài giây
 * (bắt tay giao thức VPN qua mạng) - không được chặn giao diện.
 *
 * Mật khẩu CHỈ tồn tại trong bộ nhớ của tiến trình `rasdial.exe` con trong lúc kết nối - KHÔNG được
 * ứng dụng này ghi ra đĩa/log dưới bất kỳ hình thức nào.
 */
class VpnConnector : public QThread
{
    Q_OBJECT

public:
    explicit VpnConnector(QObject* parent = nullptr);

    /// password rỗng hợp lệ nếu kết nối VPN không cần mật khẩu (vd xác thực bằng chứng chỉ máy đã cấu
    /// hình sẵn trong Windows) hoặc Windows đã nhớ sẵn thông tin đăng nhập từ lần trước.
    void setConnectTarget(const QString& connectionName, const QString& username, const QString& password);
    void setDisconnectTarget(const QString& connectionName);

signals:
    // Đặt tên khác "finished" vì QThread đã có sẵn signal finished() không tham số - trùng tên sẽ gây
    // xung đột overload (đã gặp lỗi này thật khi xây CleanupExecutor, chủ động tránh lại ở đây).
    void operationFinished(bool success, QString message);

protected:
    void run() override;

private:
    enum class Mode { Connect, Disconnect } m_mode{Mode::Connect};
    QString m_connectionName;
    QString m_username;
    QString m_password;
};
