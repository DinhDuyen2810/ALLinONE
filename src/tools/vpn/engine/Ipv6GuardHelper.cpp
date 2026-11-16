#include "Ipv6GuardHelper.h"

#include "Ipv6LeakGuard.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>

#include <windows.h>
#include <ras.h>
#include <raserror.h>

#include <cstring>
#include <vector>

namespace Ipv6GuardHelper
{
namespace internal
{
bool shouldKeepGuarding(bool released, bool appConnected, qint64 msTunnelAbsent)
{
    if (released)
        return false;
    if (appConnected)
        return true; // ứng dụng còn: nó tự báo release khi VPN ngắt/nối thất bại
    return msTunnelAbsent < kGoneGraceMs;
}
} // namespace internal

namespace
{
bool processIsElevated()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return false;
    TOKEN_ELEVATION elev{};
    DWORD size = 0;
    const bool ok = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size) && elev.TokenIsElevated;
    CloseHandle(token);
    return ok;
}

/// Kết nối VPN của Windows tên 'entry' có đang tồn tại (đang quay số hoặc đã nối) không. Lỗi RAS -> trả về
/// 'onError' để nơi gọi tự chọn hướng an toàn.
bool rasEntryPresent(const QString& entry, bool onError)
{
    std::vector<RASCONNW> connections(8);
    DWORD bytes = static_cast<DWORD>(connections.size() * sizeof(RASCONNW));
    DWORD count = 0;
    std::memset(connections.data(), 0, bytes);
    connections[0].dwSize = sizeof(RASCONNW);
    DWORD result = RasEnumConnectionsW(connections.data(), &bytes, &count);
    if (result == ERROR_BUFFER_TOO_SMALL)
    {
        connections.resize(bytes / sizeof(RASCONNW) + 1);
        bytes = static_cast<DWORD>(connections.size() * sizeof(RASCONNW));
        std::memset(connections.data(), 0, bytes);
        connections[0].dwSize = sizeof(RASCONNW);
        result = RasEnumConnectionsW(connections.data(), &bytes, &count);
    }
    if (result != ERROR_SUCCESS)
        return onError;
    for (DWORD i = 0; i < count && i < connections.size(); ++i)
        if (QString::fromWCharArray(connections[i].szEntryName).compare(entry, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

QJsonArray toJson(const QStringList& list)
{
    QJsonArray a;
    for (const QString& s : list)
        a.append(s);
    return a;
}
} // namespace

int run(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 4)
        return 2;
    const QString pipeName = QString::fromLocal8Bit(argv[2]);
    const QByteArray token = QByteArray(argv[3]);

    QLocalSocket sock;
    sock.connectToServer(pipeName);
    if (!sock.waitForConnected(15000))
        return 3;

    bool appConnected = true;
    bool released = false;
    bool configured = false;
    QString entry;
    Ipv6LeakGuard::WfpBlock::Config baseConfig; // allowRemote + extraBlockedForTest; LUID đường hầm dò mỗi giây
    Ipv6LeakGuard::WfpBlock block;
    QElapsedTimer tunnelAbsent; // chạy khi KHÔNG thấy kết nối VPN; invalid = đang thấy
    QString lastError;

    auto send = [&sock](const QJsonObject& o) {
        if (sock.state() != QLocalSocket::ConnectedState)
            return;
        sock.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
        sock.flush();
    };
    send({{"t", "hello"}, {"token", QString::fromLatin1(token)}, {"elevated", processIsElevated()}});

    // Áp bộ lọc theo trạng thái card mạng HIỆN TẠI; báo lại cho ứng dụng khi có thay đổi (hoặc force).
    auto refresh = [&](bool force) {
        if (!configured || released)
            return;
        QString snapError;
        const auto interfaces = Ipv6LeakGuard::snapshot(&snapError);
        const Ipv6LeakGuard::Assessment a = Ipv6LeakGuard::assess(interfaces, entry);
        Ipv6LeakGuard::WfpBlock::Config cfg = baseConfig;
        cfg.allowInterfaceLuid = a.tunnelLuid;
        const bool changed = !block.isActive() || !(cfg == block.config());
        if (!changed && !force)
            return;
        QString error;
        const bool ok = block.apply(cfg, &error);
        if (!ok)
            lastError = error;
        send({{"t", "state"},
              {"active", block.isActive()},
              {"error", ok ? QString() : error},
              {"filters", block.filterCount()},
              {"tunnelUp", a.tunnelLuid != 0},
              {"tunnelHasIpv6", a.tunnelHasIpv6},
              {"exposed", toJson(a.exposedInterfaces)}});
        if (!ok && !block.isActive())
        {
            // Không chặn được ngay từ đầu (thường là thiếu quyền Admin) - không còn gì để giữ, thoát.
            QTimer::singleShot(200, &app, &QCoreApplication::quit);
        }
    };

    auto finishIfDone = [&]() {
        // Có mặt = RAS còn liệt kê kết nối (kể cả đang quay số) HOẶC card đường hầm cùng tên còn hoạt động. Lỗi RAS
        // tính là "vắng" và chỉ dựa vào card - không để tiến trình treo mãi khi RAS hỏng.
        const bool tunnelPresent =
            !entry.isEmpty() && (rasEntryPresent(entry, /*onError*/ false) ||
                                 Ipv6LeakGuard::assess(Ipv6LeakGuard::snapshot(), entry).tunnelLuid != 0);
        if (tunnelPresent)
            tunnelAbsent.invalidate();
        else if (!tunnelAbsent.isValid())
            tunnelAbsent.start();
        const qint64 absentMs = tunnelAbsent.isValid() ? tunnelAbsent.elapsed() : 0;
        if (!internal::shouldKeepGuarding(released, appConnected, absentMs))
        {
            block.close();
            app.quit();
        }
    };

    QTimer tick;
    tick.setInterval(1000);
    QObject::connect(&tick, &QTimer::timeout, &app, [&]() {
        refresh(false);
        finishIfDone();
    });
    tick.start();

    QByteArray buffer;
    QObject::connect(&sock, &QLocalSocket::readyRead, &app, [&]() {
        buffer += sock.readAll();
        if (buffer.size() > 256 * 1024)
        {
            released = true;
            block.close();
            app.quit();
            return;
        }
        int nl;
        while ((nl = buffer.indexOf('\n')) >= 0)
        {
            const QJsonObject m = QJsonDocument::fromJson(buffer.left(nl)).object();
            buffer.remove(0, nl + 1);
            const QString t = m.value("t").toString();
            if (t == QLatin1String("guard"))
            {
                entry = m.value("entry").toString();
                baseConfig = Ipv6LeakGuard::WfpBlock::Config{};
                for (const QJsonValue& v : m.value("allowRemote").toArray())
                {
                    Ipv6LeakGuard::Addr16 a;
                    if (Ipv6LeakGuard::parseAddress(v.toString(), &a))
                        baseConfig.allowRemote << a;
                }
                for (const QJsonValue& v : m.value("extraBlock").toArray())
                {
                    Ipv6LeakGuard::Prefix p;
                    if (Ipv6LeakGuard::parsePrefix(v.toString(), &p))
                        baseConfig.extraBlockedForTest << p;
                }
                configured = true;
                refresh(true);
            }
            else if (t == QLatin1String("release"))
            {
                released = true;
                block.close();
                send({{"t", "state"}, {"active", false}, {"released", true}, {"filters", 0}});
                sock.flush();
                QTimer::singleShot(100, &app, &QCoreApplication::quit);
            }
        }
    });
    QObject::connect(&sock, &QLocalSocket::disconnected, &app, [&]() {
        appConnected = false;
        if (!configured)
            app.quit(); // ứng dụng đi trước khi kịp giao việc - không có gì để giữ
        else
            finishIfDone();
    });

    const int rc = app.exec();
    block.close();
    Q_UNUSED(lastError);
    return rc;
}

} // namespace Ipv6GuardHelper
