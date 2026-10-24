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
#include <QUdpSocket>

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
    if (m_esp)
        m_esp->stop();
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
    m_espCfg = GlobalProtectProtocol::EspConfig{};
    m_netApplied = false;
    m_ikeNatT = false;
    m_rxBytes = 0;
    m_rxPackets = 0;
    m_keepalivesSent = 0;

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
    else if (job.protocol == DirectProtocol::FortinetIpsec)
        startIke();
    else
    {
        m_phase = Phase::Auth;
        fail(QStringLiteral("Hồ sơ này không hỗ trợ kết nối trực tiếp."));
    }
}

// ---- FortiGate IPsec (IKEv1 + ESP) ---------------------------------------------------------------------

void TunnelEngine::startIke()
{
    m_phase = Phase::Auth;
    emit status(QStringLiteral("Đang thương lượng IPsec/IKE tới %1...").arg(m_host));
    quint32 ip = 0;
    if (DirectVpn::parseIpv4(m_host, &ip))
    {
        beginIke(ip);
        return;
    }
    QHostInfo::lookupHost(m_host, this, [this](const QHostInfo& info) {
        if (m_phase != Phase::Auth)
            return;
        for (const QHostAddress& a : info.addresses())
            if (a.protocol() == QAbstractSocket::IPv4Protocol)
            {
                beginIke(a.toIPv4Address());
                return;
            }
        fail(QStringLiteral("Không phân giải được tên máy chủ %1.").arg(m_host));
    });
}

void TunnelEngine::beginIke(quint32 gatewayIp)
{
    m_ikeGateway = gatewayIp;
    m_serverIp = gatewayIp;
    if (m_job.psk.isEmpty())
    {
        fail(QStringLiteral("Hồ sơ IPsec cần khóa chia sẻ trước (PSK) - chưa nhập."));
        return;
    }
    // FortiClient làm TOÀN BỘ IKE+ESP trên UDP 4500<->4500 (0 gói cổng 500). Bắt chước: ưu tiên bind cổng 4500
    // (nếu bận thì dùng cổng tạm) + ép NAT-T ngay từ msg1. Giúp NAT/gateway đưa đúng ESP về.
    m_ikeSock = new QUdpSocket(this);
    bool bound4500 = m_ikeSock->bind(QHostAddress(QHostAddress::AnyIPv4), 4500);
    if (!bound4500 && !m_ikeSock->bind(QHostAddress(QHostAddress::AnyIPv4), 0))
    {
        fail(QStringLiteral("Không mở được socket UDP cho IKE: %1").arg(m_ikeSock->errorString()));
        return;
    }
    connect(m_ikeSock, &QUdpSocket::readyRead, this, &TunnelEngine::onIkeDatagrams);
    // IP cục bộ tuyến tới gateway (cho NAT-D).
    quint32 localIp = 0;
    {
        QUdpSocket probe;
        probe.connectToHost(QHostAddress(gatewayIp), 500);
        if (probe.waitForConnected(500))
            localIp = probe.localAddress().toIPv4Address();
    }

    m_ike = new IkeSession(this);
    IkeSession::Config c;
    c.gatewayIp = gatewayIp;
    c.psk = m_job.psk.toUtf8();
    c.xauthUser = m_job.username;
    c.xauthPass = m_job.password;
    c.localIdType = Isakmp::ID_IPV4_ADDR; // FortiGate dialup: ID mặc định (chưa có group id) - dò lại nếu cần
    // Hồ sơ CVTLT DHGroup=512 = nhóm 20 (ECP-384): ĐÃ xác nhận bằng dò thật (gateway chỉ trả lời nhóm 20).
    c.dhGroup = IkeCrypto::DhGroup::Ecp384;
    c.encKeyBits = 256;               // chào nhiều transform; transform gateway chọn được đọc lại trong msg2
    c.hash = IkeCrypto::Hash::Sha256;
    c.natT = true;
    m_ike->configure(c);
    m_ike->setLocalEndpoint(localIp, m_ikeSock->localPort());
    if (bound4500) // đã có cổng 4500 -> ép NAT-T ngay từ đầu như FortiClient
    {
        m_ikeNatT = true;
        m_ike->setNatActive(true);
    }
    connect(m_ike, &IkeSession::sendDatagram, this, &TunnelEngine::sendIkeDatagram);
    connect(m_ike, &IkeSession::switchToNatT, this, [this]() { m_ikeNatT = true; log(QStringLiteral("IKE: chuyển NAT-T (UDP 4500).")); });
    connect(m_ike, &IkeSession::established, this, &TunnelEngine::onIkeEstablished);
    connect(m_ike, &IkeSession::failed, this, [this](const QString& r) { fail(r); });
    connect(m_ike, &IkeSession::log, this, [this](const QString& m) { log(m); });

    m_phase = Phase::Tunnel;
    m_ikeStartMs = now();
    m_ikeLastRetrans = now();
    m_lastRx = now();
    m_tick->start();
    m_ike->start();
}

void TunnelEngine::sendIkeDatagram(const QByteArray& isakmp, bool natT)
{
    if (!m_ikeSock)
        return;
    const quint16 port = natT ? 4500 : 500;
    QByteArray dg = isakmp;
    if (natT)
        dg.prepend(QByteArray(4, '\0')); // dấu non-ESP (phân biệt với gói ESP có SPI != 0)
    m_ikeSock->writeDatagram(dg, QHostAddress(m_ikeGateway), port);
}

void TunnelEngine::onIkeDatagrams()
{
    while (m_ikeSock && m_ikeSock->hasPendingDatagrams())
    {
        QByteArray dg;
        dg.resize(static_cast<int>(m_ikeSock->pendingDatagramSize()));
        QHostAddress from;
        quint16 fromPort = 0;
        m_ikeSock->readDatagram(dg.data(), dg.size(), &from, &fromPort);
        if (dg.size() < 4)
            continue;
        const bool nonEspMarker = (static_cast<quint8>(dg[0]) == 0 && static_cast<quint8>(dg[1]) == 0 &&
                                   static_cast<quint8>(dg[2]) == 0 && static_cast<quint8>(dg[3]) == 0);
        if (fromPort == 4500 || m_ikeNatT)
        {
            if (nonEspMarker)
            {
                if (m_ike)
                    m_ike->onDatagram(dg.mid(4)); // bóc dấu
            }
            else if (m_ikeEsp && m_ikeEsp->ready())
            {
                QByteArray ip;
                quint8 nh = 0;
                if (m_ikeEsp->open(dg, &ip, &nh) && !ip.isEmpty())
                {
                    m_lastRx = now();
                    m_rxBytes += dg.size();
                    ++m_rxPackets;
                    deliverToAdapter(ip);
                }
            }
            continue;
        }
        // Cổng 500 (trước NAT-T): toàn bộ là IKE thô.
        if (m_ike)
            m_ike->onDatagram(dg);
    }
}

void TunnelEngine::onIkeEstablished(const IkeSession::Result& r)
{
    // Dựng TunnelConfig từ kết quả mode-config.
    m_cfg = TunnelConfig{};
    m_cfg.ipv4 = DirectVpn::ipv4ToString(r.assignedIp);
    m_cfg.prefixLength = 32;
    for (quint32 d : r.dns)
        m_cfg.dns << DirectVpn::ipv4ToString(d);
    for (int i = 0; i < r.subnetsBase.size() && i < r.subnetsMask.size(); ++i)
    {
        const int pfx = DirectVpn::maskToPrefix(DirectVpn::ipv4ToString(r.subnetsMask[i]));
        if (pfx >= 0)
            m_cfg.includeRoutes.append(IpRoute{r.subnetsBase[i] & r.subnetsMask[i], pfx});
    }
    m_espCfg = r.esp;

    if (!m_job.noAdapterForTest)
    {
        emit status(QStringLiteral("Đang tạo adapter mạng ảo..."));
        QString err;
        m_adapter = std::make_unique<WintunAdapter>();
        if (!m_adapter->open(kAdapterName, &err)) { fail(err); return; }
        m_net = std::make_unique<NetConfigurator>();
        if (!m_net->captureUnderlay(m_serverIp, &err)) { fail(err); return; }
        const int base = (m_cfg.mtu >= 576 && m_cfg.mtu <= 1500) ? m_cfg.mtu : 1400;
        if (!m_net->apply(m_adapter->luid(), m_cfg, m_serverIp, base - 80, &err)) { fail(err); return; }
        m_netApplied = true;
    }

    m_ikeEsp = std::make_unique<GpEspCrypto>();
    QString err;
    if (!m_ikeEsp->init(m_espCfg, &err)) { fail(QStringLiteral("Nạp khóa ESP thất bại: %1").arg(err)); return; }

    if (!m_job.noAdapterForTest && m_adapter)
        m_adapter->startReader([this](QByteArray ip) {
            QMetaObject::invokeMethod(this, [this, ip]() { onAdapterPacket(ip); }, Qt::QueuedConnection);
        });

    m_phase = Phase::Up;
    m_wasUp = true;
    m_lastRx = now();
    m_lastKeepalive = now();
    m_natKeepaliveMs = now();
    const QString routes = m_cfg.fullTunnel() ? QStringLiteral("toàn bộ lưu lượng qua VPN")
                                              : QStringLiteral("%1 mạng nội bộ qua VPN").arg(m_cfg.includeRoutes.size());
    const QString summary = QStringLiteral("IP %1 · %2 · IPsec/ESP").arg(m_cfg.ipv4, routes);
    log(QStringLiteral("Đường hầm IPsec đã lên: %1").arg(summary));
    emit connected(summary);
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
             // Kênh dữ liệu ESP (IPsec/UDP): nhiều gateway BẮT BUỘC - chỉ dùng SSL để thiết lập rồi gửi dữ liệu
             // qua ESP. Nạp cấu hình ESP (khóa/SPI gateway cấp sẵn) để dùng; thiếu/không có thì rơi về SSL.
             m_espCfg = GlobalProtectProtocol::EspConfig{};
             const bool hasEsp = !m_job.noEspForTest && GlobalProtectProtocol::parseEspConfig(r.body, &m_espCfg);
             log(QStringLiteral("GP config: ESP=%1%2").arg(hasEsp ? "CÓ (dùng ESP)" : "không (dùng SSL)",
                     hasEsp ? QStringLiteral(" udp=%1 enc=%2 hmac=%3").arg(m_espCfg.udpPort).arg(m_espCfg.encAlgo, m_espCfg.hmacAlgo)
                            : QString()));
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
             m_hipStatus = r.status;
             m_hipNeeded = r.status == 200 && GlobalProtectProtocol::hipReportNeeded(r.body);
             log(QStringLiteral("HIP check: HTTP %1, cần nộp=%2").arg(r.status).arg(m_hipNeeded ? "có" : "không"));
             // Lỗi mạng/HTTP khi kiểm HIP không nên chặn kết nối: nhiều gateway KHÔNG bật HIP (báo lỗi/không cần).
             if (!m_hipNeeded)
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
    // GlobalProtect có ESP -> dùng kênh dữ liệu ESP/UDP (KHÔNG mở đường hầm SSL: việc GET /ssl-tunnel-connect
    // sẽ làm khóa ESP vô hiệu). Chỉ rơi về SSL nếu ESP không lên.
    if (m_job.protocol == DirectProtocol::GlobalProtect && m_espCfg.present)
    {
        startGpEsp();
        return;
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

void TunnelEngine::startGpEsp()
{
    emit status(QStringLiteral("Đang thiết lập kênh dữ liệu ESP (IPsec/UDP %1)...").arg(m_espCfg.udpPort));
    quint32 clientIp = 0;
    if (!DirectVpn::parseIpv4(m_cfg.ipv4, &clientIp))
    {
        fail(QStringLiteral("Địa chỉ gateway cấp không hợp lệ cho ESP."));
        return;
    }
    // Cấu hình adapter + tuyến TRƯỚC (trừ chế độ test): để gói trả về được định tuyến vào adapter ngay khi ESP
    // lên. MTU trong trừ chi phí ESP (SPI+seq 8 + IV 16 + đệm 16 + HMAC 12 + IP ngoài 20 + UDP 8 ≈ 80 byte).
    if (!m_job.noAdapterForTest && m_adapter && m_net && !m_netApplied)
    {
        const int base = (m_cfg.mtu >= 576 && m_cfg.mtu <= 1500) ? m_cfg.mtu : 1400;
        QString err;
        if (!m_net->apply(m_adapter->luid(), m_cfg, m_serverIp, base - 80, &err))
        {
            fail(err);
            return;
        }
        m_netApplied = true;
    }

    m_esp = new GpEspChannel(this);
    GpEspChannel::Callbacks cb;
    cb.ipReceived = [this](const QByteArray& ip) { deliverToAdapter(ip); };
    cb.established = [this]() { onGpEspEstablished(); };
    cb.activity = [this]() { m_lastRx = now(); };
    cb.log = [this](const QString& m) { log(m); };
    QString err;
    if (!m_esp->start(m_espCfg, clientIp, QHostAddress(m_serverIp), cb, &err))
    {
        fail(err);
        return;
    }
    m_phase = Phase::Tunnel;
    m_lastRx = now();
    m_espStartMs = now();
    m_lastProbeMs = now();
    m_tick->start();
}

void TunnelEngine::onGpEspEstablished()
{
    if (m_phase != Phase::Tunnel)
        return;
    if (!m_job.noAdapterForTest && m_adapter)
        m_adapter->startReader([this](QByteArray ip) {
            QMetaObject::invokeMethod(this, [this, ip]() { onAdapterPacket(ip); }, Qt::QueuedConnection);
        });
    m_phase = Phase::Up;
    m_wasUp = true;
    m_lastKeepalive = now();
    m_gpLastHipMs = now();
    const QString routes = m_cfg.fullTunnel() ? QStringLiteral("toàn bộ lưu lượng qua VPN")
                                              : QStringLiteral("%1 mạng nội bộ qua VPN").arg(m_cfg.includeRoutes.size());
    const QString summary = QStringLiteral("IP %1 · %2 · ESP").arg(m_cfg.ipv4, routes);
    log(QStringLiteral("Đường hầm ESP đã lên: %1").arg(summary));
    emit connected(summary);
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
    m_rxBytes += data.size();
    ++m_rxPackets;
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
    if (m_phase != Phase::Up)
        return;
    // FortiGate IPsec: gói IP -> ESP -> UDP 4500 (cùng socket IKE).
    if (m_job.protocol == DirectProtocol::FortinetIpsec)
    {
        if (m_ikeEsp && m_ikeEsp->ready() && m_ikeSock)
        {
            const QByteArray esp = m_ikeEsp->seal(ip, 4);
            if (!esp.isEmpty())
                m_ikeSock->writeDatagram(esp, QHostAddress(m_ikeGateway), 4500);
        }
        return;
    }
    // GlobalProtect qua ESP: gói IP -> ESP/UDP.
    if (m_esp && m_esp->isEstablished())
    {
        m_esp->sendIp(ip);
        return;
    }
    if (!m_tunnel || m_tunnel->bytesToWrite() > kMaxQueuedBytes)
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
    if (m_job.protocol == DirectProtocol::FortinetIpsec)
    {
        if (m_phase != Phase::Up)
        {
            // Đang thương lượng: phát lại bản tin cuối mỗi 3 giây; quá 25 giây -> thất bại.
            if (m_ike && t - m_ikeLastRetrans >= 3000)
            {
                m_ikeLastRetrans = t;
                m_ike->onRetransmit();
            }
            if (t - m_ikeStartMs >= 25000)
                fail(QStringLiteral("Thương lượng IPsec/IKE quá hạn (không phản hồi hoặc UDP 500/4500 bị chặn)."));
            return;
        }
        // Đã lên: NAT-T keepalive (1 byte 0xFF tới cổng 4500) mỗi ~20 giây để giữ ánh xạ NAT.
        if (m_ikeSock && t - m_natKeepaliveMs >= 20000)
        {
            m_natKeepaliveMs = t;
            m_ikeSock->writeDatagram(QByteArray(1, '\xFF'), QHostAddress(m_ikeGateway), 4500);
        }
        if (t - m_lastRx >= kDeadPeerMs)
            fail(QStringLiteral("Máy chủ IPsec không còn phản hồi (mất kết nối)."));
        return;
    }
    // GlobalProtect qua ESP: đang thiết lập -> gửi lại gói dò mỗi ~2 giây; quá 10 giây chưa lên -> thất bại
    // (gateway bắt buộc ESP mà UDP %1 có thể bị chặn/không tới).
    if (m_esp && !m_esp->isEstablished())
    {
        if (t - m_lastProbeMs >= 2000)
        {
            m_lastProbeMs = t;
            m_esp->sendProbe();
        }
        if (t - m_espStartMs >= 10000)
            fail(QStringLiteral("Không thiết lập được kênh dữ liệu ESP (gateway bắt buộc ESP nhưng UDP cổng %1 có thể bị "
                                "tường lửa chặn). Thử mạng khác hoặc dùng \"Mở phần mềm\".").arg(m_espCfg.udpPort));
        return;
    }
    if (m_phase == Phase::Up)
    {
        if (t - m_lastKeepalive >= kGpKeepaliveMs)
        {
            m_lastKeepalive = t;
            if (m_esp)
                m_esp->sendProbe(); // DPD/keepalive qua ESP
            else
                writeTunnel(GpFraming::keepalive());
            ++m_keepalivesSent;
        }
        // Nộp lại HIP định kỳ (qua kênh HTTPS riêng) - gateway kiểm lại theo chu kỳ.
        if (t - m_gpLastHipMs >= m_gpHipIntervalMs)
            gpHipCheck([]() {});
        if (t - m_lastRx >= kDeadPeerMs)
        {
            log(QStringLiteral("Chẩn đoán mất kết nối GP: kênh=%1, nhận %2 byte/%3 gói SSL, gửi %4 keepalive, HIP HTTP %5")
                    .arg(m_esp ? "ESP" : "SSL").arg(m_rxBytes).arg(m_rxPackets).arg(m_keepalivesSent).arg(m_hipStatus));
            fail(QStringLiteral("Gateway GlobalProtect không còn phản hồi (mất kết nối)."));
        }
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
    if (m_esp)
    {
        m_esp->stop();
        m_esp->deleteLater();
        m_esp = nullptr;
    }
    if (m_ike)
    {
        m_ike->stop();
        m_ike->disconnect(this);
        m_ike->deleteLater();
        m_ike = nullptr;
    }
    if (m_ikeSock)
    {
        m_ikeSock->disconnect(this);
        m_ikeSock->close();
        m_ikeSock->deleteLater();
        m_ikeSock = nullptr;
    }
    m_ikeEsp.reset();
    m_ikeNatT = false;
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
