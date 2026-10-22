#pragma once

#include <QObject>
#include <QPointer>
#include <QString>
#include <QTemporaryDir>

class QTcpSocket;
class QTimer;

/// Kết nối VPN bằng OpenVPN dùng `openvpn.exe` + `wintun.dll` ĐÓNG GÓI KÈM (thư mục `openvpn/` cạnh file
/// chạy, giống adb/scrcpy/yt-dlp). Dùng cho máy chủ công cộng VPN Gate khi L2TP native của Windows bị
/// hỏng/chặn (OpenVPN chạy trên TCP/UDP 443 với chứng chỉ CA nhúng sẵn trong .ovpn, không đụng tầng IPsec).
///
/// openvpn.exe cần quyền Administrator để tạo adapter ảo (wintun) nên được chạy qua UAC (ShellExecute
/// "runas"). Tiến trình nâng quyền KHÔNG kill được từ tiến trình thường, nên việc điều khiển (đọc trạng
/// thái, NGẮT sạch) đi qua **management interface** của OpenVPN: openvpn mở một cổng TCP trên 127.0.0.1,
/// ứng dụng (thường) nối vào đó để đọc ">STATE:...,CONNECTED,..." và gửi "signal SIGTERM" để ngắt.
class OpenVpnRunner : public QObject
{
    Q_OBJECT

public:
    explicit OpenVpnRunner(QObject* parent = nullptr);
    ~OpenVpnRunner() override;

    static QString exePath();     ///< `<thư mục chạy>/openvpn/openvpn.exe` (rỗng nếu không có)
    static QString wintunPath();  ///< `<thư mục chạy>/openvpn/wintun.dll`
    static bool isAvailable(QString* missing = nullptr);

    /// Kết nối với nội dung .ovpn (của VPN Gate) + tài khoản. Bất đồng bộ; kết quả qua connected()/failed().
    void connectConfig(const QString& ovpnConfig, const QString& user, const QString& pass);
    /// Ngắt sạch (gửi SIGTERM qua management). An toàn gọi khi chưa kết nối.
    void disconnectVpn();
    bool isActive() const { return m_active; }

signals:
    void stateChanged(QString human);     ///< mô tả trạng thái cho giao diện (vd "Đang kết nối...")
    void connected();                     ///< ">STATE ... CONNECTED"
    void failed(QString reason);          ///< không kết nối được / lỗi cấu hình
    void disconnected();                  ///< tunnel đã dừng (sau khi từng connected)

    // ---- Lõi THUẦN (không tiến trình/mạng) - test được ----
public:
    struct Internal
    {
        /// Dựng cấu hình .ovpn CHẠY ĐƯỢC từ cấu hình gốc của VPN Gate: bỏ các chỉ thị tương tác/không an
        /// toàn (auth-user-pass không tệp, management cũ...), thêm auth-user-pass theo tệp, management trên
        /// 127.0.0.1:<port> có mật khẩu, windows-driver wintun. Thuần - test được.
        static QString buildConfig(const QString& ovpnConfig, quint16 mgmtPort, const QString& authFileName,
                                   const QString& mgmtPwFileName);
        /// Tên trạng thái trong một dòng management ">STATE:...,<NAME>,..." (rỗng nếu không phải dòng STATE).
        static QString stateName(const QString& line);
    };

private:
    void cleanup();
    void onMgmtReadyRead();
    void fail(const QString& reason);

    QTemporaryDir m_dir;
    QPointer<QTcpSocket> m_mgmt;
    QByteArray m_mgmtBuf;
    QTimer* m_timeout{nullptr};
    quint16 m_port{0};
    QString m_mgmtPassword;
    bool m_authSent{false};
    bool m_active{false};
    bool m_everConnected{false};
    void* m_process{nullptr}; // HANDLE của openvpn.exe nâng quyền (chỉ để chờ/kiểm tra, không terminate được)
};
