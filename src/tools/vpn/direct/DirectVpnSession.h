#pragma once

#include "DirectVpnTypes.h"

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QPointer>
#include <QString>

class QLocalServer;
class QLocalSocket;
class QTimer;

/// Phía ỨNG DỤNG CHÍNH của kết nối VPN trực tiếp: khởi chạy tiến trình phụ nâng quyền (UAC, một lần mỗi
/// phiên), nói chuyện với nó qua named pipe, chuyển tiếp trạng thái/OTP/xác nhận chứng chỉ cho giao diện.
///
/// An toàn: tên pipe + mã một lần ngẫu nhiên 128-bit; chỉ gửi việc (gồm mật khẩu) cho kết nối đã gửi đúng mã.
/// Mật khẩu chỉ nằm trong bộ nhớ tới khi đường hầm lên (hoặc thất bại) rồi bị xóa trắng; không ghi đĩa/log.
/// Chứng chỉ người dùng đã chấp nhận được ghim theo "host:port" trong profiles/vpn_trusted_certs.json.
class DirectVpnSession : public QObject
{
    Q_OBJECT

public:
    struct Request
    {
        DirectProtocol protocol{DirectProtocol::None};
        QString server;
        QString username;
        QString password;
        QString psk; ///< khóa chia sẻ trước - chỉ FortiGate IPsec (IKEv1)
    };

    explicit DirectVpnSession(QObject* parent = nullptr);
    ~DirectVpnSession() override;

    /// Bắt đầu kết nối (khởi chạy tiến trình phụ nếu chưa có - hiện UAC).
    void connectTo(const Request& req);
    void sendOtp(const QString& code);
    /// Sau certificateUntrusted(): người dùng chấp nhận -> ghim vân tay rồi thử lại (không cần UAC lại).
    void trustAndRetry(const QString& hostPort, const QString& sha256);
    /// Ngắt kết nối và cho tiến trình phụ thoát (nó tự gỡ tuyến/adapter trước khi thoát).
    void disconnectVpn();
    /// Dừng hẳn, chờ tối đa waitMs để tiến trình phụ dọn dẹp - gọi khi đóng cửa sổ/thoát ứng dụng.
    void shutdown(int waitMs);

    bool isActive() const { return m_active; }
    bool isConnected() const { return m_connected; }

    static QMap<QString, QString> trustedPins();
    static bool savePin(const QString& hostPort, const QString& sha256);

signals:
    void statusChanged(QString text);
    void otpRequested(QString prompt, bool pushAvailable);
    void certificateUntrusted(QString hostPort, QString sha256, QString subject, QString issuer);
    void connected(QString summary);
    void ended(bool wasConnected, QString message);

private:
    bool launchHelper(QString* error);
    void sendJob();
    void send(const QByteArray& json);
    void onNewConnection();
    void onReadyRead();
    void finish(bool wasConnected, const QString& message);
    void wipePassword();

    QLocalServer* m_server{nullptr};
    QPointer<QLocalSocket> m_sock;
    QByteArray m_token;
    QByteArray m_buf;
    QTimer* m_launchTimeout{nullptr};
    Request m_req;
    bool m_active{false};
    bool m_verified{false};
    bool m_connected{false};
    bool m_jobPending{false};
    struct PendingCert
    {
        QString host, sha256, subject, issuer;
        bool valid{false};
    } m_cert;
};
