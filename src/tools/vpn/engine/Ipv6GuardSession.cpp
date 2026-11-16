#include "Ipv6GuardSession.h"

#include "Ipv6GuardHelper.h"
#include "core/Logger.h"
#include "core/WinElevation.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QRandomGenerator>
#include <QTimer>

#include <windows.h>
#include <shellapi.h>

namespace
{
QByteArray randomHex(int bytes)
{
    QByteArray raw(bytes, Qt::Uninitialized);
    QRandomGenerator::system()->fillRange(reinterpret_cast<quint32*>(raw.data()), bytes / 4);
    return raw.toHex();
}

QStringList toStringList(const QJsonArray& a)
{
    QStringList out;
    for (const QJsonValue& v : a)
        out << v.toString();
    return out;
}
} // namespace

Ipv6GuardSession::Ipv6GuardSession(QObject* parent)
    : QObject(parent)
    , m_monitor(new QTimer(this))
    , m_launchTimeout(new QTimer(this))
{
    connect(m_monitor, &QTimer::timeout, this, &Ipv6GuardSession::monitorTick);
    m_launchTimeout->setSingleShot(true);
    connect(m_launchTimeout, &QTimer::timeout, this, [this]() {
        if (m_state != State::Starting)
            return;
        dropHelper();
        setState(State::Failed, false, {}, QStringLiteral("tiến trình chặn IPv6 không phản hồi."));
        finishReady(false, QStringLiteral("Không chặn được rò rỉ IPv6: tiến trình chặn không phản hồi."));
    });
}

Ipv6GuardSession::~Ipv6GuardSession()
{
    // KHÔNG gửi "release": đóng ứng dụng khi VPN còn nối thì tiến trình phụ vẫn giữ chặn tới khi VPN ngắt.
    if (m_sock)
    {
        m_sock->disconnect(this);
        m_sock->abort();
    }
}

QString Ipv6GuardSession::describe(State state, bool tunnelHasIpv6, const QStringList& exposedInterfaces,
                                   const QString& error)
{
    switch (state)
    {
    case State::Idle:
        return QString();
    case State::NotNeeded:
        return QStringLiteral("IPv6: máy không có IPv6 ra Internet - không có IPv6 thật nào để lộ (vẫn theo dõi suốt "
                              "phiên).");
    case State::Starting:
        return QStringLiteral("IPv6: đang chặn IPv6 thật (bấm Yes ở hộp thoại UAC nếu Windows hỏi)...");
    case State::Active:
        if (tunnelHasIpv6)
            return QStringLiteral("IPv6: 🛡 IPv6 chỉ đi qua VPN (VPN có cấp IPv6) - IPv6 thật của máy bị chặn.");
        return exposedInterfaces.isEmpty()
                   ? QStringLiteral("IPv6: 🛡 đã chặn IPv6 thật - trang web chỉ thấy IP của VPN.")
                   : QStringLiteral("IPv6: 🛡 đã chặn IPv6 thật (%1) - trang web chỉ thấy IP của VPN.")
                         .arg(exposedInterfaces.join(QStringLiteral(", ")));
    case State::Failed:
        return QStringLiteral("IPv6: ⚠ KHÔNG chặn được rò rỉ IPv6 - %1")
            .arg(error.isEmpty() ? QStringLiteral("không rõ lý do.") : error);
    }
    return QString();
}

QString Ipv6GuardSession::describePublicIpv6(const PublicIpv6Info& info, bool vpnConnected, State guardState,
                                            bool tunnelHasIpv6, bool* warn)
{
    if (warn)
        *warn = false;
    if (!info.reachable)
        return vpnConnected ? QStringLiteral("IPv6: không ra được Internet bằng IPv6 - IPv6 thật không lộ ✓")
                            : QStringLiteral("IPv6: máy không ra Internet bằng IPv6.");
    const QString place = info.city.isEmpty() ? info.country : (info.city + QStringLiteral(", ") + info.country);
    const QString where = place.isEmpty() ? QString() : (QStringLiteral(" · 📍 ") + place);
    if (!vpnConnected)
        return QStringLiteral("IPv6: %1%2 (IPv6 thật của máy)").arg(info.ip, where);
    if (guardState == State::Active && tunnelHasIpv6)
        return QStringLiteral("IPv6 (qua VPN): %1%2").arg(info.ip, where);
    if (warn)
        *warn = true;
    return QStringLiteral("⚠ IPv6 vẫn lộ: %1%2 - trang web có thể thấy vị trí thật của bạn qua IPv6.").arg(info.ip, where);
}

QList<Ipv6LeakGuard::InterfaceInfo> Ipv6GuardSession::takeSnapshot() const
{
    if (m_hooks.snapshot)
        return m_hooks.snapshot();
    return Ipv6LeakGuard::snapshot();
}

void Ipv6GuardSession::setState(State state, bool tunnelHasIpv6, const QStringList& exposed, const QString& error)
{
    m_state = state;
    m_tunnelHasIpv6 = state == State::Active && tunnelHasIpv6;
    m_text = describe(state, tunnelHasIpv6, exposed, error);
    emit stateChanged(state, m_text);
}

void Ipv6GuardSession::finishReady(bool ok, const QString& message)
{
    if (!m_pendingReady)
        return;
    m_pendingReady = false;
    emit ready(ok, message);
}

void Ipv6GuardSession::begin(const QString& rasEntry, const QStringList& allowRemoteIpv6)
{
    m_entry = rasEntry;
    m_allowRemote = allowRemoteIpv6;
    m_pendingReady = true;
    m_monitor->stop();

    // Tiến trình phụ của lần trước vẫn còn (vd nối lại sau khi rớt) - giao việc mới, không hỏi UAC lại.
    if (m_sock && m_verified)
    {
        setState(State::Starting);
        sendGuard();
        m_launchTimeout->start(m_hooks.helperTimeoutMs);
        return;
    }

    // Tính theo tên kết nối: nếu chính kết nối VPN này đang nối sẵn (nối lại) thì card của nó không phải "lộ".
    const Ipv6LeakGuard::Assessment a = Ipv6LeakGuard::assess(takeSnapshot(), rasEntry);
    if (a.exposure == Ipv6LeakGuard::Exposure::NoIpv6)
    {
        setState(State::NotNeeded);
        m_monitor->start(m_hooks.monitorIntervalMs);
        QTimer::singleShot(0, this, [this]() { finishReady(true, QString()); });
        return;
    }

    Logger::instance().info("VPN", QStringLiteral("Có IPv6 ra Internet qua %1 - bật chặn rò rỉ IPv6 trước khi nối VPN")
                                       .arg(a.exposedInterfaces.join(", ")));
    setState(State::Starting);
    QString error;
    if (!launchHelper(&error))
    {
        setState(State::Failed, false, {}, error);
        QTimer::singleShot(0, this, [this, error]() { finishReady(false, QStringLiteral("Không chặn được rò rỉ IPv6: ") + error); });
    }
}

void Ipv6GuardSession::release()
{
    m_monitor->stop();
    m_launchTimeout->stop();
    m_entry.clear();
    m_pendingReady = false; // nơi gọi tự hủy - không phát ready() nữa
    if (m_sock && m_sock->state() == QLocalSocket::ConnectedState)
    {
        m_sock->write(QByteArrayLiteral("{\"t\":\"release\"}\n"));
        m_sock->flush();
        m_sock->waitForBytesWritten(1000);
    }
    dropHelper();
    if (m_state != State::Idle)
        setState(State::Idle);
}

void Ipv6GuardSession::dropHelper()
{
    if (m_sock)
    {
        QLocalSocket* s = m_sock;
        m_sock = nullptr;
        s->disconnect(this);
        s->disconnectFromServer();
        s->deleteLater();
    }
    if (m_server)
    {
        m_server->close();
        m_server->deleteLater();
        m_server = nullptr;
    }
    m_verified = false;
    m_buf.clear();
}

bool Ipv6GuardSession::launchHelper(QString* error)
{
    dropHelper();
    m_server = new QLocalServer(this);
    const QString name = QStringLiteral("OneForAll-ipv6-") + QString::fromLatin1(randomHex(16));
    if (!m_server->listen(name))
    {
        *error = QStringLiteral("không tạo được kênh điều khiển nội bộ (%1).").arg(m_server->errorString());
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, &Ipv6GuardSession::onNewConnection);
    m_token = randomHex(16);
    m_verified = false;

    const QString program =
        m_hooks.helperProgram.isEmpty() ? QCoreApplication::applicationFilePath() : m_hooks.helperProgram;
    const QStringList args{QString::fromLatin1(Ipv6GuardHelper::kSwitch), m_server->fullServerName(),
                           QString::fromLatin1(m_token)};
    if (m_hooks.launchWithoutElevation || WinElevation::isElevated())
    {
        // Ứng dụng đã có quyền Admin (tiến trình con thừa hưởng) hoặc kiểm thử: chạy thẳng, không hỏi UAC.
        if (!QProcess::startDetached(program, args))
        {
            *error = QStringLiteral("không khởi chạy được tiến trình chặn IPv6.");
            return false;
        }
    }
    else
    {
        const std::wstring exe = QDir::toNativeSeparators(program).toStdWString();
        const std::wstring params = (args[0] + ' ' + args[1] + ' ' + args[2]).toStdWString();
        SHELLEXECUTEINFOW info{};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOASYNC;
        info.lpVerb = L"runas"; // thêm bộ lọc Windows Filtering Platform cần quyền Administrator
        info.lpFile = exe.c_str();
        info.lpParameters = params.c_str();
        info.nShow = SW_HIDE;
        if (!ShellExecuteExW(&info))
        {
            const DWORD e = GetLastError();
            *error = e == ERROR_CANCELLED
                         ? QStringLiteral("bạn đã từ chối quyền Administrator (cần để chặn IPv6 ở tầng Windows).")
                         : QStringLiteral("không khởi chạy được tiến trình chặn IPv6 (mã %1).").arg(e);
            return false;
        }
    }
    m_launchTimeout->start(m_hooks.helperTimeoutMs);
    return true;
}

void Ipv6GuardSession::onNewConnection()
{
    while (QLocalSocket* s = m_server ? m_server->nextPendingConnection() : nullptr)
    {
        if (m_sock)
        {
            s->abort(); // chỉ nhận MỘT tiến trình phụ
            s->deleteLater();
            continue;
        }
        m_sock = s;
        m_buf.clear();
        connect(s, &QLocalSocket::readyRead, this, &Ipv6GuardSession::onReadyRead);
        connect(s, &QLocalSocket::disconnected, this, &Ipv6GuardSession::onHelperGone);
        QTimer::singleShot(0, this, &Ipv6GuardSession::onReadyRead); // "hello" có thể đã nằm sẵn trong bộ đệm
    }
    if (m_server)
        m_server->close(); // không nhận thêm kết nối nào
}

void Ipv6GuardSession::sendGuard()
{
    if (!m_sock)
        return;
    QJsonArray allow;
    for (const QString& a : std::as_const(m_allowRemote))
        allow.append(a);
    QJsonArray extra;
    for (const QString& p : std::as_const(m_hooks.extraBlock))
        extra.append(p);
    const QJsonObject o{{"t", "guard"}, {"entry", m_entry}, {"allowRemote", allow}, {"extraBlock", extra}};
    m_sock->write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
    m_sock->flush();
}

void Ipv6GuardSession::onReadyRead()
{
    if (!m_sock)
        return;
    m_buf += m_sock->readAll();
    if (m_buf.size() > 256 * 1024)
    {
        m_buf.clear();
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
            if (t != QLatin1String("hello") || m.value("token").toString().toLatin1() != m_token)
            {
                // Không phải tiến trình phụ của mình - bỏ, chờ hết giờ.
                QLocalSocket* s = m_sock;
                m_sock = nullptr;
                s->disconnect(this);
                s->abort();
                s->deleteLater();
                return;
            }
            m_verified = true;
            if (m_entry.isEmpty())
            {
                // Người dùng đã hủy trong lúc chờ UAC - bảo tiến trình phụ thoát.
                m_sock->write(QByteArrayLiteral("{\"t\":\"release\"}\n"));
                m_sock->flush();
                dropHelper();
                return;
            }
            sendGuard();
            continue;
        }
        if (t != QLatin1String("state") || m.value("released").toBool())
            continue;
        const bool active = m.value("active").toBool();
        const QString error = m.value("error").toString();
        const bool tunnelHasIpv6 = m.value("tunnelHasIpv6").toBool();
        const QStringList exposed = toStringList(m.value("exposed").toArray());
        m_launchTimeout->stop();
        if (active)
        {
            setState(State::Active, tunnelHasIpv6, exposed);
            finishReady(true, QString());
        }
        else
        {
            Logger::instance().warning("VPN", "Không chặn được rò rỉ IPv6: " + error);
            setState(State::Failed, false, {}, error);
            finishReady(false, QStringLiteral("Không chặn được rò rỉ IPv6: ") + error);
        }
    }
}

void Ipv6GuardSession::onHelperGone()
{
    const bool wasActive = m_state == State::Active || m_state == State::Starting;
    dropHelper();
    if (m_entry.isEmpty() || !wasActive)
        return;
    setState(State::Failed, false, {}, QStringLiteral("tiến trình chặn IPv6 đã dừng."));
    finishReady(false, QStringLiteral("Không chặn được rò rỉ IPv6: tiến trình chặn đã dừng."));
}

void Ipv6GuardSession::monitorTick()
{
    if (m_state != State::NotNeeded || m_entry.isEmpty())
        return;
    const Ipv6LeakGuard::Assessment a = Ipv6LeakGuard::assess(takeSnapshot(), m_entry);
    if (a.exposure == Ipv6LeakGuard::Exposure::NoIpv6)
        return;
    // IPv6 vừa xuất hiện giữa phiên (đổi mạng, router vừa cấp IPv6) - chặn ngay.
    m_monitor->stop();
    Logger::instance().info("VPN", QStringLiteral("IPv6 vừa xuất hiện qua %1 giữa phiên VPN - bật chặn rò rỉ IPv6")
                                       .arg(a.exposedInterfaces.join(", ")));
    setState(State::Starting);
    QString error;
    if (!launchHelper(&error))
        setState(State::Failed, false, {}, error);
}
