#pragma once

#include "DirectVpnTypes.h"
#include "FortinetProtocol.h"
#include "GlobalProtectProtocol.h"
#include "PppSession.h"

#include <QElapsedTimer>
#include <QMap>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>

#include <functional>
#include <memory>

class QNetworkAccessManager;
class QTcpSocket;
class QTimer;
class WintunAdapter;
class NetConfigurator;

/// Bộ máy MỘT phiên VPN kết nối trực tiếp (FortiGate SSL-VPN / GlobalProtect): đăng nhập HTTPS -> lấy cấu
/// hình -> mở đường hầm TLS -> nối với adapter wintun + đặt IP/tuyến/DNS -> chuyển gói hai chiều -> khi ngắt
/// thì gỡ tuyến, xóa adapter, đăng xuất. Chạy trong tiến trình phụ NÂNG QUYỀN (VpnHelperMain) vì tạo adapter
/// và đổi bảng định tuyến cần quyền Administrator.
///
/// Chứng chỉ máy chủ: tin theo kho chứng chỉ Windows; nếu không tin được (FortiGate hay dùng chứng chỉ mặc
/// định tự ký) thì CHỈ chấp nhận khi vân tay SHA-256 trùng vân tay người dùng đã xác nhận (Job::pins) -
/// ngược lại phát certificateUntrusted() và dừng, để giao diện hỏi người dùng (giống FortiClient).
class TunnelEngine : public QObject
{
    Q_OBJECT

public:
    struct Job
    {
        DirectProtocol protocol{DirectProtocol::None};
        QString server;              ///< host[:port] lấy từ hồ sơ FortiClient/GlobalProtect
        QString username;
        QString password;
        QString realm;               ///< realm FortiGate (thường rỗng)
        QMap<QString, QString> pins; ///< "host:port" -> SHA-256 (hex thường) của chứng chỉ người dùng đã chấp nhận
        bool plainForTest{false};    ///< CHỈ test: HTTP/TCP trần thay TLS (máy chủ giả trên 127.0.0.1)
        bool noAdapterForTest{false};///< CHỈ test: không tạo adapter/không đổi tuyến - gói đi qua tín hiệu test
    };

    explicit TunnelEngine(QObject* parent = nullptr);
    ~TunnelEngine() override;

    void start(const Job& job);
    /// Trả lời otpRequired(). code rỗng với FortiToken push = chờ xác nhận trên điện thoại.
    void provideOtp(const QString& code);
    /// Ngắt (an toàn gọi nhiều lần) - finished() phát khi đã gỡ xong.
    void stop(const QString& reason);
    bool isRunning() const { return m_phase != Phase::Idle && m_phase != Phase::Done; }
    bool isUp() const { return m_phase == Phase::Up; }

    /// CHỈ test (noAdapterForTest): giả lập máy gửi một gói IP vào adapter.
    void injectFromAdapterForTest(const QByteArray& ip);

signals:
    void status(QString text);
    void otpRequired(QString prompt, bool pushAvailable);
    void certificateUntrusted(QString hostPort, QString sha256, QString subject, QString issuer);
    void connected(QString summary);
    void finished(bool wasConnected, QString message);
    void logMessage(QString text);
    /// CHỈ test (noAdapterForTest): gói từ đường hầm lẽ ra được đưa vào adapter.
    void packetToAdapterForTest(QByteArray ip);

private:
    enum class Phase { Idle, Auth, Config, Tunnel, Up, Stopping, Done };
    enum class OtpContext { None, Fortinet, GpPortal, GpGateway };

    struct HttpResult
    {
        int status{0};
        QByteArray body;
        QList<QNetworkCookie> cookies;
        QNetworkReply::NetworkError error{QNetworkReply::NoError};
        QString errorString;
        bool untrustedCert{false};
    };
    using HttpCallback = std::function<void(const HttpResult&)>;
    void http(const QByteArray& method, const QString& hostPort, const QString& pathAndQuery, const QByteArray& body,
              const QByteArray& userAgent, const QByteArray& cookieHeader, HttpCallback cb);
    bool handleHttpFailure(const HttpResult& r, const QString& what);

    // Fortinet
    void fortiLogin();
    void onFortiLogin(const HttpResult& r);
    void fortiConfig();

    // GlobalProtect
    void gpPortalPrelogin();
    void gpPortalConfig(const QString& otpCode);
    void gpGatewayLogin(const QString& otpCode);
    void gpGetConfig();
    /// Kiểm tra + (nếu cần) nộp báo cáo HIP; done() được gọi khi xong (thành công hoặc bỏ qua) để đi tiếp.
    void gpHipCheck(std::function<void()> done);

    // Đường hầm
    void resolveAndOpenTunnel();
    void openTunnel(quint32 serverIp);
    void onTunnelConnected();
    void onTunnelReadyRead();
    void onTunnelUp();
    void onAdapterPacket(const QByteArray& ip);
    void deliverToAdapter(const QByteArray& ip);
    void writeTunnel(const QByteArray& data);
    void onTick();

    void fail(const QString& message);
    void teardown(const QString& message);
    void finishNow(const QString& message);
    void log(const QString& text);
    qint64 now() const { return m_clock.elapsed(); }

    Job m_job;
    QString m_host;      ///< host đang làm việc (portal/gateway/FortiGate)
    int m_port{443};
    Phase m_phase{Phase::Idle};
    bool m_wasUp{false};
    QString m_endMessage;

    QNetworkAccessManager* m_nam{nullptr};
    QList<QPointer<QNetworkReply>> m_replies;
    QPointer<QTcpSocket> m_tunnel;
    QTimer* m_tick{nullptr};
    QElapsedTimer m_clock;
    qint64 m_lastRx{0};
    qint64 m_lastKeepalive{0};

    // Fortinet
    QByteArray m_svpnCookie;
    FortinetProtocol::LoginOutcome m_fortiPending;
    std::unique_ptr<PppSession> m_ppp;
    FortinetFraming::Deframer m_fortiDeframer;

    // GlobalProtect
    GlobalProtectProtocol::PortalConfig m_portal;
    GlobalProtectProtocol::GatewayAuth m_gpAuth;
    QString m_gpInputStr;
    QString m_gpTunnelPath;
    QByteArray m_gpHandshake; ///< gom 12 byte "START_TUNNEL" đầu tiên
    bool m_gpStarted{false};
    GpFraming::Deframer m_gpDeframer;
    QString m_gpHipMd5;         ///< định danh HIP của phiên (ổn định)
    qint64 m_gpLastHipMs{0};    ///< mốc nộp/kiểm HIP gần nhất
    qint64 m_gpHipIntervalMs{3600000}; ///< chu kỳ kiểm lại HIP (mặc định 1 giờ, như client chính thức)

    OtpContext m_otp{OtpContext::None};
    TunnelConfig m_cfg;
    quint32 m_serverIp{0};

    std::unique_ptr<WintunAdapter> m_adapter;
    std::unique_ptr<NetConfigurator> m_net;
};
