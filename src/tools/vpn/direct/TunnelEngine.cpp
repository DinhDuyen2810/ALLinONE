#include "TunnelEngine.h"

#include "WintunAdapter.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QHostInfo>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QSysInfo>
#include <QTcpSocket>
#include <QTimer>

namespace
{
const QString kAdapterName = QStringLiteral("OneForAll VPN");
const QByteArray kFortiUa = "Mozilla/5.0 SV1";       // tác nhân FortiGate quen (theo openfortivpn/OpenConnect)
const QByteArray kGpUa = "PAN GlobalProtect";          // tác nhân client GlobalProtect chính thức
constexpr qint64 kGpKeepaliveMs = 10000;               // như client Windows chính thức
constexpr qint64 kDeadPeerMs = 60000;                  // không nhận gì 60 giây -> coi như mất kết nối
constexpr qint64 kMaxQueuedBytes = 4 * 1024 * 1024;    // nghẽn: bỏ gói thay vì dồn bộ nhớ vô hạn

/// Thông báo tiếng Việt cho lỗi đăng nhập GlobalProtect thường gặp.
QString gpMessage(const QString& raw)
{
    if (raw.contains(QStringLiteral("Invalid username or password"), Qt::CaseInsensitive) ||
        raw.contains(QStringLiteral("Authentication failed"), Qt::CaseInsensitive))
        return QStringLiteral("Sai tên đăng nhập hoặc mật khẩu.");
    return raw;
}
} // namespace

TunnelEngine::TunnelEngine(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_tick(new QTimer(this))
{
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, &TunnelEngine::onTick);
    m_clock.start();
}

TunnelEngine::~TunnelEngine()
{
    // Lưới an toàn: tiến trình phụ thoát giữa chừng vẫn phải gỡ tuyến + xóa adapter.
    if (m_adapter)
        m_adapter->close();
    if (m_net)
        m_net->revert();
}

void TunnelEngine::log(const QString& text)
{
    emit logMessage(text);
}

void TunnelEngine::start(const Job& job)
{
    if (isRunning())
        return;
    m_job = job;
    m_wasUp = false;
    m_endMessage.clear();
    m_svpnCookie.clear();
    m_otp = OtpContext::None;
    m_cfg = TunnelConfig{};
    m_portal = GlobalProtectProtocol::PortalConfig{};
    m_gpAuth = GlobalProtectProtocol::GatewayAuth{};
    m_gpStarted = false;
    m_gpHandshake.clear();
    m_fortiDeframer = FortinetFraming::Deframer{};
    m_gpDeframer = GpFraming::Deframer{};
    m_ppp.reset();

    const int defaultPort = 443;
    if (!DirectVpn::splitHostPort(job.server, defaultPort, &m_host, &m_port))
    {
        m_phase = Phase::Auth;
        fail(QStringLiteral("Địa chỉ máy chủ \"%1\" không hợp lệ.").arg(job.server));
        return;
    }
    if (job.protocol == DirectProtocol::FortinetSsl)
        fortiLogin();
    else if (job.protocol == DirectProtocol::GlobalProtect)
        gpPortalPrelogin();
    else
    {
        m_phase = Phase::Auth;
        fail(QStringLiteral("Hồ sơ này không hỗ trợ kết nối trực tiếp."));
    }
}

// ---- HTTP(S) -------------------------------------------------------------------------------------------

void TunnelEngine::http(const QByteArray& method, const QString& hostPort, const QString& pathAndQuery,
                        const QByteArray& body, const QByteArray& userAgent, const QByteArray& cookieHeader, HttpCallback cb)
{
    const QString scheme = m_job.plainForTest ? QStringLiteral("http") : QStringLiteral("https");
    QNetworkRequest req(QUrl(scheme + QStringLiteral("://") + hostPort + pathAndQuery));
    req.setHeader(QNetworkRequest::UserAgentHeader, userAgent);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    // Cookie tự quản (gửi/nhận tường minh) - không để cookie phiên VPN lẫn vào kho cookie dùng chung.
    req.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    req.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    req.setTransferTimeout(30000);
    if (!cookieHeader.isEmpty())
        req.setRawHeader("Cookie", cookieHeader);
    QNetworkReply* reply = nullptr;
    if (method == "POST")
    {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
        reply = m_nam->post(req, body);
    }
    else
        reply = m_nam->get(req);
    m_replies << reply;

    auto untrusted = std::make_shared<QSslCertificate>();
    connect(reply, &QNetworkReply::sslErrors, this, [this, reply, hostPort, untrusted](const QList<QSslError>&) {
        const QSslCertificate cert = reply->sslConfiguration().peerCertificate();
        const QString sha = QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex());
        if (!cert.isNull() && !m_job.pins.value(hostPort).isEmpty() && m_job.pins.value(hostPort) == sha)
        {
            reply->ignoreSslErrors(); // đúng chứng chỉ người dùng đã xác nhận
            return;
        }
        *untrusted = cert;
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, hostPort, untrusted, cb]() {
        reply->deleteLater();
        m_replies.removeAll(reply);
        HttpResult r;
        r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        r.body = reply->readAll();
        r.cookies = reply->header(QNetworkRequest::SetCookieHeader).value<QList<QNetworkCookie>>();
        r.error = reply->error();
        r.errorString = reply->errorString();
        if (!untrusted->isNull() && r.status == 0)
        {
            r.untrustedCert = true;
            emit certificateUntrusted(hostPort, QString::fromLatin1(untrusted->digest(QCryptographicHash::Sha256).toHex()),
                                      untrusted->subjectDisplayName(), untrusted->issuerDisplayName());
        }
        cb(r);
    });
}

bool TunnelEngine::handleHttpFailure(const HttpResult& r, const QString& what)
{
    if (r.untrustedCert)
    {
        fail(QStringLiteral("Chứng chỉ của máy chủ chưa được tin cậy - cần bạn xác nhận vân tay chứng chỉ."));
        return true;
    }
    if (r.status == 0)
    {
        fail(QStringLiteral("%1: %2").arg(what, r.errorString));
        return true;
    }
    return false;
}

// ---- FortiGate SSL-VPN ---------------------------------------------------------------------------------

void TunnelEngine::fortiLogin()
{
    m_phase = Phase::Auth;
    emit status(QStringLiteral("Đang đăng nhập FortiGate %1:%2...").arg(m_host).arg(m_port));
    const QString hp = m_host + ':' + QString::number(m_port);
    http("POST", hp, QStringLiteral("/remote/logincheck"),
         FortinetProtocol::loginBody(m_job.username, m_job.password, m_job.realm), kFortiUa, {},
         [this](const HttpResult& r) { onFortiLogin(r); });
}

void TunnelEngine::onFortiLogin(const HttpResult& r)
{
    if (m_phase != Phase::Auth)
        return;
    if (handleHttpFailure(r, QStringLiteral("Không đăng nhập được FortiGate")))
        return;
    QByteArray cookie;
    for (const QNetworkCookie& c : r.cookies)
        if (c.name() == "SVPNCOOKIE" && !c.value().isEmpty())
            cookie = c.value();
    const FortinetProtocol::LoginOutcome o = FortinetProtocol::interpretLogin(r.status, r.body, !cookie.isEmpty());
    switch (o.kind)
    {
        case FortinetProtocol::LoginOutcome::Kind::Success:
            m_svpnCookie = cookie;
            m_job.password.fill(QChar(0));
            m_job.password.clear();
            fortiConfig();
            return;
        case FortinetProtocol::LoginOutcome::Kind::NeedToken:
            m_fortiPending = o;
            m_otp = OtpContext::Fortinet;
            emit status(QStringLiteral("Máy chủ yêu cầu mã OTP..."));
            emit otpRequired(o.message, o.ftmPush);
            return;
        default:
            fail(o.message);
            return;
    }
}

void TunnelEngine::fortiConfig()
{
    m_phase = Phase::Config;
    emit status(QStringLiteral("Đang nhận cấu hình mạng từ FortiGate..."));
    const QString hp = m_host + ':' + QString::number(m_port);
    http("GET", hp, QStringLiteral("/remote/fortisslvpn_xml"), {}, kFortiUa, "SVPNCOOKIE=" + m_svpnCookie,
         [this](const HttpResult& r) {
             if (m_phase != Phase::Config)
                 return;
             if (handleHttpFailure(r, QStringLiteral("Không nhận được cấu hình")))
                 return;
             QString err;
             if (r.status != 200 || !FortinetProtocol::parseConfigXml(r.body, &m_cfg, &err))
             {
                 fail(err.isEmpty() ? QStringLiteral("FortiGate từ chối cấp cấu hình (HTTP %1).").arg(r.status) : err);
                 return;
             }
             log(QStringLiteral("FortiGate cấp IP %1, %2 DNS, %3 tuyến riêng")
                     .arg(m_cfg.ipv4.isEmpty() ? QStringLiteral("(qua PPP)") : m_cfg.ipv4)
                     .arg(m_cfg.dns.size())
                     .arg(m_cfg.includeRoutes.size()));
             resolveAndOpenTunnel();
         });
}

// ---- GlobalProtect -------------------------------------------------------------------------------------

void TunnelEngine::gpPortalPrelogin()
{
    m_phase = Phase::Auth;
    emit status(QStringLiteral("Đang liên hệ portal GlobalProtect %1...").arg(m_host));
    const QString hp = m_host + ':' + QString::number(m_port);
    http("POST", hp, QStringLiteral("/global-protect/prelogin.esp?tmp=tmp&clientVer=4100&clientos=Windows"),
         "cas-support=yes", kGpUa, {}, [this, hp](const HttpResult& r) {
             if (m_phase != Phase::Auth)
                 return;
             if (handleHttpFailure(r, QStringLiteral("Không liên hệ được portal")))
                 return;
             GlobalProtectProtocol::Prelogin p;
             QString err;
             if (!GlobalProtectProtocol::parsePrelogin(r.body, &p, &err))
             {
                 if (err.contains(QStringLiteral("does not exist"), Qt::CaseInsensitive))
                 {
                     // Máy chủ chỉ là gateway (không phải portal) - đăng nhập thẳng vào gateway.
                     m_portal.gateways = {hp};
                     gpGatewayLogin(QString());
                     return;
                 }
                 fail(err);
                 return;
             }
             if (p.saml)
             {
                 fail(QStringLiteral("Portal yêu cầu đăng nhập SAML qua trình duyệt - chưa hỗ trợ kết nối trực tiếp. "
                                     "Hãy dùng \"Mở phần mềm\"."));
                 return;
             }
             gpPortalConfig(QString());
         });
}

void TunnelEngine::gpPortalConfig(const QString& otpCode)
{
    const QString hp = m_host + ':' + QString::number(m_port);
    GlobalProtectProtocol::LoginParams lp;
    lp.server = m_host;
    lp.computer = QSysInfo::machineHostName();
    lp.username = m_job.username;
    lp.password = otpCode.isEmpty() ? m_job.password : otpCode;
    lp.inputStr = otpCode.isEmpty() ? QString() : m_gpInputStr;
    emit status(QStringLiteral("Đang đăng nhập portal GlobalProtect..."));
    http("POST", hp, QStringLiteral("/global-protect/getconfig.esp"), GlobalProtectProtocol::loginBody(lp), kGpUa, {},
         [this, hp](const HttpResult& r) {
             if (m_phase != Phase::Auth)
                 return;
             if (handleHttpFailure(r, QStringLiteral("Không đăng nhập được portal")))
                 return;
             const auto rep = GlobalProtectProtocol::classify(r.body);
             if (rep.kind == GlobalProtectProtocol::ServerReply::Kind::Challenge)
             {
                 m_gpInputStr = rep.inputStr;
                 m_otp = OtpContext::GpPortal;
                 emit otpRequired(rep.message, false);
                 return;
             }
             if (rep.kind == GlobalProtectProtocol::ServerReply::Kind::Error)
             {
                 if (rep.message.contains(QStringLiteral("portal does not exist"), Qt::CaseInsensitive))
                 {
                     m_portal.gateways = {hp};
                     gpGatewayLogin(QString());
                     return;
                 }
                 fail(gpMessage(rep.message));
                 return;
             }
             QString err;
             if (!GlobalProtectProtocol::parsePortalConfig(r.body, &m_portal, &err))
             {
                 fail(err);
                 return;
             }
             log(QStringLiteral("Portal liệt kê %1 gateway, chọn %2").arg(m_portal.gateways.size()).arg(m_portal.gateways.first()));
             QString gwHost;
             int gwPort = 443;
             if (!DirectVpn::splitHostPort(m_portal.gateways.first(), 443, &gwHost, &gwPort))
             {
                 fail(QStringLiteral("Địa chỉ gateway không hợp lệ: %1").arg(m_portal.gateways.first()));
                 return;
             }
             m_host = gwHost;
             m_port = gwPort;
             gpGatewayLogin(QString());
         });
}

void TunnelEngine::gpGatewayLogin(const QString& otpCode)
{
    const QString hp = m_host + ':' + QString::number(m_port);
    GlobalProtectProtocol::LoginParams lp;
    lp.server = m_host;
    lp.computer = QSysInfo::machineHostName();
    lp.username = m_job.username;
    lp.password = otpCode.isEmpty() ? m_job.password : otpCode;
    lp.inputStr = otpCode.isEmpty() ? QString() : m_gpInputStr;
    lp.portalUserAuthCookie = m_portal.userAuthCookie;
    lp.portalPrelogonCookie = m_portal.prelogonCookie;
    emit status(QStringLiteral("Đang đăng nhập gateway %1...").arg(m_host));
    http("POST", hp, QStringLiteral("/ssl-vpn/login.esp"), GlobalProtectProtocol::loginBody(lp), kGpUa, {},
         [this](const HttpResult& r) {
             if (m_phase != Phase::Auth)
                 return;
             if (handleHttpFailure(r, QStringLiteral("Không đăng nhập được gateway")))
                 return;
             const auto rep = GlobalProtectProtocol::classify(r.body);
             if (rep.kind == GlobalProtectProtocol::ServerReply::Kind::Challenge)
             {
                 m_gpInputStr = rep.inputStr;
                 m_otp = OtpContext::GpGateway;
                 emit otpRequired(rep.message, false);
                 return;
             }
             if (rep.kind == GlobalProtectProtocol::ServerReply::Kind::Error)
             {
                 fail(gpMessage(rep.message));
                 return;
             }
             QString err;
             if (!GlobalProtectProtocol::parseGatewayLogin(r.body, &m_gpAuth, &err))
             {
                 fail(err);
                 return;
             }
             m_gpAuth.computer = QSysInfo::machineHostName();
             m_job.password.fill(QChar(0));
             m_job.password.clear();
             gpGetConfig();
         });
}

void TunnelEngine::gpGetConfig()
{
    m_phase = Phase::Config;
    emit status(QStringLiteral("Đang nhận cấu hình mạng từ gateway..."));
    const QString hp = m_host + ':' + QString::number(m_port);
    http("POST", hp, QStringLiteral("/ssl-vpn/getconfig.esp"), GlobalProtectProtocol::getConfigBody(m_gpAuth, m_portal.version),
         kGpUa, {}, [this](const HttpResult& r) {
             if (m_phase != Phase::Config)
                 return;
             if (handleHttpFailure(r, QStringLiteral("Không nhận được cấu hình")))
                 return;
             const auto rep = GlobalProtectProtocol::classify(r.body);
             if (rep.kind == GlobalProtectProtocol::ServerReply::Kind::Error)
             {
                 fail(gpMessage(rep.message));
                 return;
             }
             QString err;
             if (!GlobalProtectProtocol::parseGatewayConfig(r.body, &m_cfg, &m_gpTunnelPath, &err))
             {
                 fail(err);
                 return;
             }
             log(QStringLiteral("Gateway cấp IP %1/%2, %3 DNS, %4 tuyến riêng")
                     .arg(m_cfg.ipv4)
                     .arg(m_cfg.prefixLength)
                     .arg(m_cfg.dns.size())
                     .arg(m_cfg.includeRoutes.size()));
             // Nộp HIP TRƯỚC khi mở đường hầm (như client chính thức) - gateway yêu cầu HIP thường ngừng chuyển
             // gói nếu chưa nhận báo cáo, khiến đường hầm "treo" rồi app báo mất kết nối ở mốc 60 giây.
             gpHipCheck([this]() { resolveAndOpenTunnel(); });
         });
}

void TunnelEngine::gpHipCheck(std::function<void()> done)
{
    emit status(QStringLiteral("Đang kiểm tra tình trạng máy (HIP)..."));
    if (m_gpHipMd5.isEmpty())
        m_gpHipMd5 = GlobalProtectProtocol::hipMd5(m_gpAuth);
    m_gpLastHipMs = now();
    const QString hp = m_host + ':' + QString::number(m_port);
    auto proceed = std::make_shared<std::function<void()>>(std::move(done));
    http("POST", hp, QStringLiteral("/ssl-vpn/hipreportcheck.esp"),
         GlobalProtectProtocol::hipCheckBody(m_gpAuth, m_cfg.ipv4, m_gpHipMd5), kGpUa, {}, [this, hp, proceed](const HttpResult& r) {
             if (m_phase == Phase::Stopping || m_phase == Phase::Done)
                 return;
             // Lỗi mạng/HTTP khi kiểm HIP không nên chặn kết nối: nhiều gateway KHÔNG bật HIP (báo lỗi/không cần).
             if (r.status != 200 || !GlobalProtectProtocol::hipReportNeeded(r.body))
             {
                 (*proceed)();
                 return;
             }
             log(QStringLiteral("Gateway yêu cầu báo cáo HIP - đang nộp."));
             const QByteArray xml = GlobalProtectProtocol::buildHipReportXml(m_gpAuth, m_cfg.ipv4, m_gpHipMd5, m_portal.version).toUtf8();
             http("POST", hp, QStringLiteral("/ssl-vpn/hipreport.esp"), GlobalProtectProtocol::hipReportBody(m_gpAuth, m_cfg.ipv4, xml),
                  kGpUa, {}, [this, proceed](const HttpResult&) {
                      if (m_phase == Phase::Stopping || m_phase == Phase::Done)
                          return;
                      (*proceed)(); // nộp xong (dù máy chủ phản hồi gì) thì đi tiếp
                  });
         });
}

void TunnelEngine::provideOtp(const QString& code)
{
    const OtpContext ctx = m_otp;
    m_otp = OtpContext::None;
    if (m_phase != Phase::Auth)
        return;
    switch (ctx)
    {
        case OtpContext::Fortinet:
        {
            emit status(QStringLiteral("Đang gửi mã OTP..."));
            const QString hp = m_host + ':' + QString::number(m_port);
            http("POST", hp, QStringLiteral("/remote/logincheck"),
                 FortinetProtocol::tokenBody(m_job.username, code, m_job.realm, m_fortiPending.tokenFields, m_fortiPending.ftmPush),
                 kFortiUa, {}, [this](const HttpResult& r) { onFortiLogin(r); });
            return;
        }
        case OtpContext::GpPortal:
            gpPortalConfig(code);
            return;
        case OtpContext::GpGateway:
            gpGatewayLogin(code);
            return;
        default:
            return;
    }
}

// ---- Đường hầm -----------------------------------------------------------------------------------------

void TunnelEngine::resolveAndOpenTunnel()
{
    m_phase = Phase::Tunnel;
    quint32 ip = 0;
    if (DirectVpn::parseIpv4(m_host, &ip))
    {
        openTunnel(ip);
        return;
    }
    QHostInfo::lookupHost(m_host, this, [this](const QHostInfo& info) {
        if (m_phase != Phase::Tunnel)
            return;
        for (const QHostAddress& a : info.addresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol)
            {
                openTunnel(a.toIPv4Address());
                return;
            }
        fail(QStringLiteral("Không phân giải được tên máy chủ %1.").arg(m_host));
    });
}

void TunnelEngine::openTunnel(quint32 serverIp)
{
    m_serverIp = serverIp;
    if (!m_job.noAdapterForTest)
    {
        emit status(QStringLiteral("Đang tạo adapter mạng ảo..."));
        QString err;
        m_adapter = std::make_unique<WintunAdapter>();
        if (!m_adapter->open(kAdapterName, &err))
        {
            fail(err);
            return;
        }
        m_net = std::make_unique<NetConfigurator>();
        if (!m_net->captureUnderlay(serverIp, &err))
        {
            fail(err);
            return;
        }
    }
    emit status(QStringLiteral("Đang mở đường hầm tới %1:%2...").arg(m_host).arg(m_port));
    const QString hp = m_host + ':' + QString::number(m_port);
    QTcpSocket* sock = nullptr;
    if (m_job.plainForTest)
    {
        sock = new QTcpSocket(this);
        connect(sock, &QTcpSocket::connected, this, &TunnelEngine::onTunnelConnected);
    }
    else
    {
        auto* ssl = new QSslSocket(this);
        connect(ssl, &QSslSocket::sslErrors, this, [this, ssl, hp](const QList<QSslError>&) {
            const QSslCertificate cert = ssl->peerCertificate();
            const QString sha = QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex());
            if (!cert.isNull() && !m_job.pins.value(hp).isEmpty() && m_job.pins.value(hp) == sha)
                ssl->ignoreSslErrors();
        });
        connect(ssl, &QSslSocket::encrypted, this, &TunnelEngine::onTunnelConnected);
        sock = ssl;
    }
    m_tunnel = sock;
    connect(sock, &QTcpSocket::readyRead, this, &TunnelEngine::onTunnelReadyRead);
    connect(sock, &QTcpSocket::errorOccurred, this, [this, sock](QAbstractSocket::SocketError) {
        if (m_tunnel != sock || m_phase == Phase::Stopping || m_phase == Phase::Done)
            return;
        fail(m_phase == Phase::Up ? QStringLiteral("Mất kết nối tới máy chủ VPN (%1).").arg(sock->errorString())
                                  : QStringLiteral("Không mở được đường hầm: %1").arg(sock->errorString()));
    });
    if (m_job.plainForTest)
        sock->connectToHost(m_host, static_cast<quint16>(m_port));
    else
        static_cast<QSslSocket*>(sock)->connectToHostEncrypted(m_host, static_cast<quint16>(m_port));
}

void TunnelEngine::writeTunnel(const QByteArray& data)
{
    if (m_tunnel && m_tunnel->state() == QAbstractSocket::ConnectedState)
        m_tunnel->write(data);
}

void TunnelEngine::onTunnelConnected()
{
    m_lastRx = now();
    m_tick->start();
    if (m_job.protocol == DirectProtocol::FortinetSsl)
    {
        const QString hostHeader = m_host + ':' + QString::number(m_port);
        writeTunnel(FortinetProtocol::tunnelRequest(hostHeader, m_svpnCookie));
        PppSession::Callbacks cb;
        cb.sendFrame = [this](const QByteArray& f) { writeTunnel(FortinetFraming::encapsulate(f)); };
        cb.ipReceived = [this](const QByteArray& ip) { deliverToAdapter(ip); };
        // Hoãn sang vòng sự kiện kế tiếp: opened() được gọi từ BÊN TRONG PppSession::handleFrame, còn
        // onTunnelUp() có thể fail() -> teardown() -> hủy m_ppp ngay trong lúc nó đang chạy (dùng sau khi giải phóng).
        cb.opened = [this]() {
            QTimer::singleShot(0, this, [this]() {
                if (m_phase == Phase::Tunnel && m_ppp && m_ppp->isOpened())
                    onTunnelUp();
            });
        };
        cb.terminated = [this](const QString& reason) {
            QTimer::singleShot(0, this, [this, reason]() { teardown(m_endMessage.isEmpty() ? reason : m_endMessage); });
        };
        cb.log = [this](const QString& m) { log(m); };
        m_ppp = std::make_unique<PppSession>(cb);
        quint32 ip = 0;
        if (DirectVpn::parseIpv4(m_cfg.ipv4, &ip))
            m_ppp->setRequestedIpv4(ip);
        m_ppp->setRequestDns(m_cfg.dns.isEmpty());
        m_ppp->start(now());
    }
    else
    {
        writeTunnel(GlobalProtectProtocol::tunnelRequest(m_gpTunnelPath, m_gpAuth));
    }
}

void TunnelEngine::onTunnelReadyRead()
{
    if (!m_tunnel)
        return;
    const QByteArray data = m_tunnel->readAll();
    if (data.isEmpty())
        return;
    m_lastRx = now();
    if (m_job.protocol == DirectProtocol::FortinetSsl)
    {
        m_fortiDeframer.feed(data);
        for (;;)
        {
            QByteArray frame;
            QString err;
            const auto res = m_fortiDeframer.next(&frame, &err);
            if (res == FortinetFraming::Deframer::Result::NeedMore)
                break;
            if (res == FortinetFraming::Deframer::Result::Error)
            {
                fail(err.startsWith(QStringLiteral("HTTP/"))
                         ? QStringLiteral("FortiGate từ chối mở đường hầm (%1) - phiên đăng nhập có thể đã hết hạn.").arg(err)
                         : err);
                return;
            }
            if (m_ppp)
                m_ppp->handleFrame(frame, now());
            if (m_phase == Phase::Stopping || m_phase == Phase::Done)
                return;
        }
        return;
    }

    // GlobalProtect: 12 byte "START_TUNNEL" rồi mới tới gói.
    QByteArray rest = data;
    if (!m_gpStarted)
    {
        m_gpHandshake += data;
        if (m_gpHandshake.size() < 12)
            return;
        if (!m_gpHandshake.startsWith("START_TUNNEL"))
        {
            const QByteArray line = m_gpHandshake.left(m_gpHandshake.indexOf('\n')).trimmed();
            fail(line.contains(" 502")
                     ? QStringLiteral("Gateway từ chối phiên (cookie không hợp lệ/hết hạn).")
                     : QStringLiteral("Gateway không mở đường hầm: %1").arg(QString::fromLatin1(line.left(120))));
            return;
        }
        m_gpStarted = true;
        rest = m_gpHandshake.mid(12);
        m_gpHandshake.clear();
        onTunnelUp();
        if (m_phase != Phase::Up)
            return;
    }
    m_gpDeframer.feed(rest);
    for (;;)
    {
        QByteArray ip;
        QString err;
        const auto res = m_gpDeframer.next(&ip, &err);
        if (res == GpFraming::Deframer::Result::NeedMore)
            break;
        if (res == GpFraming::Deframer::Result::Error)
        {
            fail(err);
            return;
        }
        if (res == GpFraming::Deframer::Result::Packet)
            deliverToAdapter(ip);
    }
}

void TunnelEngine::onTunnelUp()
{
    int mtu = 1400;
    if (m_job.protocol == DirectProtocol::FortinetSsl && m_ppp)
    {
        m_cfg.ipv4 = DirectVpn::ipv4ToString(m_ppp->localIpv4());
        m_cfg.prefixLength = 32;
        if (m_cfg.dns.isEmpty())
            for (quint32 d : m_ppp->dnsServers())
                m_cfg.dns << DirectVpn::ipv4ToString(d);
        const int peer = m_ppp->peerMru();
        mtu = (peer >= 576 && peer <= 1500) ? peer : 1354;
    }
    else if (m_cfg.mtu >= 576 && m_cfg.mtu <= 1500)
        mtu = m_cfg.mtu;

    if (!m_job.noAdapterForTest && m_adapter && m_net)
    {
        emit status(QStringLiteral("Đang cấu hình địa chỉ và tuyến mạng..."));
        QString err;
        if (!m_net->apply(m_adapter->luid(), m_cfg, m_serverIp, mtu, &err))
        {
            fail(err);
            return;
        }
        m_adapter->startReader([this](QByteArray ip) {
            // Chạy trên luồng đọc của wintun -> chuyển về luồng của bộ máy (socket chỉ dùng ở luồng này).
            QMetaObject::invokeMethod(this, [this, ip]() { onAdapterPacket(ip); }, Qt::QueuedConnection);
        });
    }
    m_phase = Phase::Up;
    m_wasUp = true;
    m_lastKeepalive = now();
    m_gpLastHipMs = now();

    QString routes = m_cfg.fullTunnel() ? QStringLiteral("toàn bộ lưu lượng qua VPN")
                                        : QStringLiteral("%1 mạng nội bộ qua VPN").arg(m_cfg.includeRoutes.size());
    const QString summary = QStringLiteral("IP %1 · %2%3")
                                .arg(m_cfg.ipv4, routes,
                                     m_cfg.dns.isEmpty() ? QString() : QStringLiteral(" · DNS %1").arg(m_cfg.dns.join(", ")));
    log(QStringLiteral("Đường hầm đã lên: %1 (MTU %2)").arg(summary).arg(mtu));
    emit connected(summary);
}

void TunnelEngine::deliverToAdapter(const QByteArray& ip)
{
    if (m_job.noAdapterForTest)
        emit packetToAdapterForTest(ip);
    else if (m_adapter)
        m_adapter->sendPacket(ip);
}

void TunnelEngine::onAdapterPacket(const QByteArray& ip)
{
    if (m_phase != Phase::Up || !m_tunnel || m_tunnel->bytesToWrite() > kMaxQueuedBytes)
        return;
    if (m_job.protocol == DirectProtocol::FortinetSsl)
    {
        if (m_ppp)
            m_ppp->sendIp(ip);
    }
    else
        writeTunnel(GpFraming::encapsulateIpv4(ip));
}

void TunnelEngine::injectFromAdapterForTest(const QByteArray& ip)
{
    onAdapterPacket(ip);
}

void TunnelEngine::onTick()
{
    const qint64 t = now();
    if (m_job.protocol == DirectProtocol::FortinetSsl)
    {
        if (m_ppp)
            m_ppp->tick(t); // PPP tự thăm dò (LCP echo) và tự kết thúc khi máy chủ im lặng quá lâu
        return;
    }
    if (m_phase == Phase::Up)
    {
        if (t - m_lastKeepalive >= kGpKeepaliveMs)
        {
            m_lastKeepalive = t;
            writeTunnel(GpFraming::keepalive());
        }
        // Nộp lại HIP định kỳ (qua kênh HTTPS riêng, không qua đường hầm) - gateway kiểm lại theo chu kỳ.
        if (t - m_gpLastHipMs >= m_gpHipIntervalMs)
            gpHipCheck([]() {});
        if (t - m_lastRx >= kDeadPeerMs)
            fail(QStringLiteral("Gateway GlobalProtect không còn phản hồi (mất kết nối)."));
    }
}

// ---- Kết thúc ------------------------------------------------------------------------------------------

void TunnelEngine::stop(const QString& reason)
{
    if (m_phase == Phase::Idle || m_phase == Phase::Done || m_phase == Phase::Stopping)
        return;
    if (m_phase == Phase::Up && m_ppp && !m_ppp->isDead() && m_endMessage.isEmpty())
    {
        // Kết thúc PPP lịch sự (LCP Terminate-Request) rồi mới đóng - quá 2,5 giây thì đóng luôn.
        m_endMessage = reason;
        m_ppp->close(reason, now());
        QTimer::singleShot(2500, this, [this]() { teardown(m_endMessage); });
        return;
    }
    teardown(reason);
}

void TunnelEngine::fail(const QString& message)
{
    log(QStringLiteral("Lỗi: %1").arg(message));
    teardown(message);
}

void TunnelEngine::teardown(const QString& message)
{
    if (m_phase == Phase::Stopping || m_phase == Phase::Done)
        return;
    const bool hadSession = !m_svpnCookie.isEmpty() || !m_gpAuth.authcookie.isEmpty();
    m_phase = Phase::Stopping;
    m_otp = OtpContext::None;
    m_tick->stop();
    for (const QPointer<QNetworkReply>& r : m_replies)
        if (r)
        {
            r->disconnect(this);
            r->abort();
            r->deleteLater();
        }
    m_replies.clear();
    if (m_tunnel)
    {
        m_tunnel->disconnect(this);
        m_tunnel->abort();
        m_tunnel->deleteLater();
        m_tunnel = nullptr;
    }
    m_ppp.reset();
    // Gỡ tuyến TRƯỚC khi xóa adapter (tuyến /32 tới máy chủ nằm trên card mạng thật, không tự mất).
    if (m_net)
        m_net->revert();
    m_net.reset();
    if (m_adapter)
        m_adapter->close();
    m_adapter.reset();
    m_job.password.fill(QChar(0));
    m_job.password.clear();

    if (!hadSession || m_job.plainForTest)
    {
        finishNow(message);
        return;
    }
    // Đăng xuất phiên trên máy chủ (best-effort, tối đa ~4 giây) để không để phiên treo/chiếm giấy phép.
    const QString hp = m_host + ':' + QString::number(m_port);
    auto done = std::make_shared<bool>(false);
    auto finish = [this, message, done]() {
        if (*done)
            return;
        *done = true;
        finishNow(message);
    };
    QTimer::singleShot(4000, this, finish);
    if (m_job.protocol == DirectProtocol::FortinetSsl)
        http("GET", hp, QStringLiteral("/remote/logout"), {}, kFortiUa, "SVPNCOOKIE=" + m_svpnCookie,
             [finish](const HttpResult&) { finish(); });
    else
        http("POST", hp, QStringLiteral("/ssl-vpn/logout.esp"), m_gpAuth.cookieQuery() + "&clientos=Windows", kGpUa, {},
             [finish](const HttpResult&) { finish(); });
    m_svpnCookie.clear();
    m_gpAuth.authcookie.clear();
}

void TunnelEngine::finishNow(const QString& message)
{
    if (m_phase == Phase::Done)
        return;
    m_phase = Phase::Done;
    emit finished(m_wasUp, message);
}
