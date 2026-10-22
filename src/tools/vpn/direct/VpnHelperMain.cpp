#include "VpnHelperMain.h"

#include "TunnelEngine.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>

#include <windows.h>

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
} // namespace

int runVpnHelper(int argc, char* argv[])
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

    auto send = [&sock](const QJsonObject& o) {
        if (sock.state() != QLocalSocket::ConnectedState)
            return;
        sock.write(QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n');
        sock.flush();
    };
    send({{"t", "hello"}, {"token", QString::fromLatin1(token)}, {"elevated", processIsElevated()}});

    TunnelEngine engine;
    bool quitting = false;
    QObject::connect(&engine, &TunnelEngine::status, &app, [&](const QString& s) { send({{"t", "status"}, {"text", s}}); });
    QObject::connect(&engine, &TunnelEngine::logMessage, &app, [&](const QString& s) { send({{"t", "log"}, {"text", s}}); });
    QObject::connect(&engine, &TunnelEngine::otpRequired, &app,
                     [&](const QString& prompt, bool push) { send({{"t", "otp"}, {"prompt", prompt}, {"push", push}}); });
    QObject::connect(&engine, &TunnelEngine::certificateUntrusted, &app,
                     [&](const QString& hp, const QString& sha, const QString& subject, const QString& issuer) {
                         send({{"t", "cert"}, {"host", hp}, {"sha256", sha}, {"subject", subject}, {"issuer", issuer}});
                     });
    QObject::connect(&engine, &TunnelEngine::connected, &app, [&](const QString& summary) { send({{"t", "up"}, {"summary", summary}}); });
    QObject::connect(&engine, &TunnelEngine::finished, &app, [&](bool wasUp, const QString& message) {
        send({{"t", "end"}, {"connected", wasUp}, {"message", message}});
        if (quitting)
            app.quit();
    });

    auto shutdown = [&](const QString& reason) {
        if (quitting)
            return;
        quitting = true;
        if (engine.isRunning())
        {
            engine.stop(reason);
            QTimer::singleShot(10000, &app, &QCoreApplication::quit); // lưới an toàn nếu đăng xuất treo
        }
        else
            app.quit();
    };

    QByteArray buffer;
    QObject::connect(&sock, &QLocalSocket::readyRead, &app, [&]() {
        buffer += sock.readAll();
        if (buffer.size() > 1024 * 1024)
        {
            shutdown(QStringLiteral("Dữ liệu điều khiển bất thường."));
            return;
        }
        int nl;
        while ((nl = buffer.indexOf('\n')) >= 0)
        {
            const QJsonObject m = QJsonDocument::fromJson(buffer.left(nl)).object();
            buffer.remove(0, nl + 1);
            const QString t = m.value("t").toString();
            if (t == QLatin1String("job"))
            {
                TunnelEngine::Job job;
                job.protocol = static_cast<DirectProtocol>(m.value("protocol").toInt());
                job.server = m.value("server").toString();
                job.username = m.value("user").toString();
                job.password = m.value("pass").toString();
                job.realm = m.value("realm").toString();
                const QJsonObject pins = m.value("pins").toObject();
                for (auto it = pins.begin(); it != pins.end(); ++it)
                    job.pins.insert(it.key(), it.value().toString());
                engine.start(job);
            }
            else if (t == QLatin1String("otp"))
                engine.provideOtp(m.value("code").toString());
            else if (t == QLatin1String("stop"))
                engine.stop(QStringLiteral("Đã ngắt kết nối."));
            else if (t == QLatin1String("quit"))
                shutdown(QStringLiteral("Đã ngắt kết nối."));
        }
    });
    QObject::connect(&sock, &QLocalSocket::disconnected, &app,
                     [&]() { shutdown(QStringLiteral("Ứng dụng đã đóng - ngắt VPN.")); });

    return app.exec();
}
