#include "DirectVpnSession.h"

#include "core/AppPaths.h"
#include "core/Logger.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QThread>
#include <QTimer>

#include <windows.h>
#include <shellapi.h>

namespace
{
const QString kPinsFile = QStringLiteral("vpn_trusted_certs.json");

QByteArray randomHex(int bytes)
{
    QByteArray raw(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(raw.data()), bytes / 4);
    return raw.toHex();
}
} // namespace

DirectVpnSession::DirectVpnSession(QObject* parent)
    : QObject(parent)
    , m_launchTimeout(new QTimer(this))
{
    m_launchTimeout->setSingleShot(true);
    connect(m_launchTimeout, &QTimer::timeout, this, [this]() {
        if (m_active && !m_verified)
            finish(false, QStringLiteral("Tiến trình kết nối không phản hồi sau khi cấp quyền."));
    });
}

DirectVpnSession::~DirectVpnSession()
{
    shutdown(3000);
}

QMap<QString, QString> DirectVpnSession::trustedPins()
{
    QMap<QString, QString> pins;
    QFile f(AppPaths::profileFile(kPinsFile));
    if (!f.open(QIODevice::ReadOnly))
        return pins;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it)
        pins.insert(it.key(), it.value().toString());
    return pins;
}

bool DirectVpnSession::savePin(const QString& hostPort, const QString& sha256)
{
    QMap<QString, QString> pins = trustedPins();
    pins.insert(hostPort, sha256.toLower());
    QJsonObject o;
    for (auto it = pins.begin(); it != pins.end(); ++it)
        o.insert(it.key(), it.value());
    QSaveFile f(AppPaths::profileFile(kPinsFile));
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(o).toJson());
    return f.commit();
}

void DirectVpnSession::connectTo(const Request& req)
{
    if (m_active && m_connected)
        return;
    m_req = req;
    m_connected = false;
    m_cert = PendingCert{};
    if (m_sock && m_verified)
    {
        sendJob(); // tiến trình phụ vẫn còn (vd thử lại sau khi tin cậy chứng chỉ) - không cần UAC lại
        return;
    }
    QString err;
    if (!launchHelper(&err))
    {
        wipePassword();
        emit ended(false, err);
        return;
    }
    m_jobPending = true;
}

bool DirectVpnSession::launchHelper(QString* error)
{
    if (m_server)
    {
        m_server->close();
        m_server->deleteLater();
    }
    m_server = new QLocalServer(this);
    const QString name = QStringLiteral("OneForAll-vpn-") + QString::fromLatin1(randomHex(16));
    if (!m_server->listen(name))
    {
        *error = QStringLiteral("Không tạo được kênh điều khiển nội bộ: %1").arg(m_server->errorString());
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, &DirectVpnSession::onNewConnection);
    m_token = randomHex(16);
    m_verified = false;

    const std::wstring exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).toStdWString();
    const std::wstring params =
        (QStringLiteral("--vpn-helper ") + m_server->fullServerName() + ' ' + QString::fromLatin1(m_token)).toStdWString();
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC;
    info.lpVerb = L"runas"; // tạo adapter mạng + đổi bảng định tuyến cần quyền Administrator
    info.lpFile = exe.c_str();
    info.lpParameters = params.c_str();
    info.nShow = SW_HIDE;
    emit statusChanged(QStringLiteral("Đang xin quyền Administrator (bấm Yes ở hộp thoại UAC)..."));
    if (!ShellExecuteExW(&info))
    {
        const DWORD e = GetLastError();
        *error = e == ERROR_CANCELLED ? QStringLiteral("Bạn đã từ chối quyền Administrator - không thể tạo kết nối VPN.")
                                      : QStringLiteral("Không khởi chạy được tiến trình kết nối (mã %1).").arg(e);
        m_server->close();
        return false;
    }
    m_active = true;
    m_launchTimeout->start(30000);
    return true;
}

void DirectVpnSession::onNewConnection()
{
    while (QLocalSocket* s = m_server->nextPendingConnection())
    {
        if (m_sock)
        {
            s->abort(); // chỉ chấp nhận MỘT tiến trình phụ
            s->deleteLater();
            continue;
        }
        m_sock = s;
        m_buf.clear();
        connect(s, &QLocalSocket::readyRead, this, &DirectVpnSession::onReadyRead);
        // "hello" của tiến trình phụ có thể đã nằm sẵn trong bộ đệm (readyRead phát trước khi gắn slot) -
        // xử lý ngay ở vòng sự kiện kế tiếp thay vì chờ dữ liệu mới (có thể không bao giờ tới).
        QTimer::singleShot(0, this, &DirectVpnSession::onReadyRead);
        connect(s, &QLocalSocket::disconnected, this, [this, s]() {
            if (m_sock != s)
                return;
            m_sock = nullptr;
            s->deleteLater();
            if (m_active)
                finish(m_connected, m_connected ? QStringLiteral("Tiến trình kết nối đã dừng - VPN đã ngắt.")
                                                : QStringLiteral("Tiến trình kết nối đã dừng."));
        });
    }
    // Không mở thêm kết nối mới khi đã có tiến trình phụ.
    m_server->close();
}

void DirectVpnSession::send(const QByteArray& json)
{
    if (m_sock && m_sock->state() == QLocalSocket::ConnectedState)
    {
        m_sock->write(json + '\n');
        m_sock->flush();
    }
}

void DirectVpnSession::sendJob()
{
    m_jobPending = false;
    QJsonObject pins;
    const QMap<QString, QString> stored = trustedPins();
    for (auto it = stored.begin(); it != stored.end(); ++it)
        pins.insert(it.key(), it.value());
    QJsonObject job{{"t", "job"},
                    {"protocol", static_cast<int>(m_req.protocol)},
                    {"server", m_req.server},
                    {"user", m_req.username},
                    {"pass", m_req.password},
                    {"pins", pins}};
    QByteArray bytes = QJsonDocument(job).toJson(QJsonDocument::Compact);
    send(bytes);
    bytes.fill('\0');
    emit statusChanged(QStringLiteral("Đang kết nối tới %1...").arg(m_req.server));
}

void DirectVpnSession::onReadyRead()
{
    if (!m_sock)
        return;
    m_buf += m_sock->readAll();
    if (m_buf.size() > 1024 * 1024)
    {
        m_sock->abort();
        return;
    }
    int nl;
    while ((nl = m_buf.indexOf('\n')) >= 0)
    {
        const QJsonObject m = QJsonDocument::fromJson(m_buf.left(nl)).object();
        m_buf.remove(0, nl + 1);
        const QString t = m.value("t").toString();
        if (!m_verified)
        {
            // Mã một lần phải khớp - không thì đây không phải tiến trình phụ ta vừa khởi chạy.
            if (t != QLatin1String("hello") || m.value("token").toString().toLatin1() != m_token)
            {
                Logger::instance().warning("VPN trực tiếp", "Kết nối điều khiển sai mã - từ chối.");
                m_sock->abort();
                return;
            }
            m_verified = true;
            m_launchTimeout->stop();
            if (!m.value("elevated").toBool())
                Logger::instance().warning("VPN trực tiếp", "Tiến trình phụ KHÔNG có quyền Administrator.");
            if (m_jobPending)
                sendJob();
            continue;
        }
        if (t == QLatin1String("status"))
            emit statusChanged(m.value("text").toString());
        else if (t == QLatin1String("log"))
            Logger::instance().info("VPN trực tiếp", m.value("text").toString());
        else if (t == QLatin1String("otp"))
            emit otpRequested(m.value("prompt").toString(), m.value("push").toBool());
        else if (t == QLatin1String("cert"))
        {
            m_cert = PendingCert{m.value("host").toString(), m.value("sha256").toString(), m.value("subject").toString(),
                                 m.value("issuer").toString(), true};
        }
        else if (t == QLatin1String("up"))
        {
            m_connected = true;
            wipePassword();
            emit connected(m.value("summary").toString());
        }
        else if (t == QLatin1String("end"))
        {
            const bool wasUp = m.value("connected").toBool();
            if (!wasUp && m_cert.valid)
            {
                // Dừng vì chứng chỉ chưa tin cậy: giữ tiến trình phụ + mật khẩu trong bộ nhớ chờ người dùng quyết.
                const PendingCert c = m_cert;
                m_cert = PendingCert{};
                emit certificateUntrusted(c.host, c.sha256, c.subject, c.issuer);
                continue;
            }
            m_connected = false;
            wipePassword();
            // Phiên kết thúc: cho tiến trình phụ thoát (nó đã gỡ tuyến/adapter).
            send(QByteArrayLiteral("{\"t\":\"quit\"}"));
            finish(wasUp, m.value("message").toString());
        }
    }
}

void DirectVpnSession::sendOtp(const QString& code)
{
    QByteArray bytes = QJsonDocument(QJsonObject{{"t", "otp"}, {"code", code}}).toJson(QJsonDocument::Compact);
    send(bytes);
    bytes.fill('\0');
}

void DirectVpnSession::trustAndRetry(const QString& hostPort, const QString& sha256)
{
    savePin(hostPort, sha256);
    connectTo(m_req);
}

void DirectVpnSession::disconnectVpn()
{
    if (!m_active)
        return;
    if (m_sock)
        send(QByteArrayLiteral("{\"t\":\"quit\"}"));
    else
        finish(false, QStringLiteral("Đã hủy."));
}

void DirectVpnSession::shutdown(int waitMs)
{
    if (!m_active && !m_sock)
        return;
    wipePassword();
    if (m_sock)
    {
        QLocalSocket* s = m_sock;
        s->disconnect(this); // không xử lý thêm thông điệp nào trong lúc tắt
        send(QByteArrayLiteral("{\"t\":\"quit\"}"));
        s->waitForBytesWritten(500);
        // Chờ CHẶN (không processEvents - có thể đang chạy trong lúc hủy cửa sổ/thoát ứng dụng) để tiến trình
        // phụ gỡ tuyến/adapter rồi tự đóng pipe; hết giờ thì đóng pipe - nó vẫn tự dọn dẹp và thoát.
        if (s->state() == QLocalSocket::ConnectedState)
            s->waitForDisconnected(waitMs);
        s->abort();
        s->deleteLater();
        m_sock = nullptr;
    }
    m_active = false;
    m_connected = false;
}

void DirectVpnSession::finish(bool wasConnected, const QString& message)
{
    if (!m_active)
        return;
    m_active = false;
    m_connected = false;
    m_verified = m_sock != nullptr && m_verified;
    m_launchTimeout->stop();
    wipePassword();
    emit ended(wasConnected, message);
}

void DirectVpnSession::wipePassword()
{
    m_req.password.fill(QChar(0));
    m_req.password.clear();
}
