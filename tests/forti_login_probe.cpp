// CÔNG CỤ KIỂM TRA TAY: dò đăng nhập FortiGate SSL-VPN THẬT để xem máy chủ trả gì (vì sao báo "sai mật khẩu").
// Thử: (1) POST /remote/logincheck thẳng; (2) GET /remote/login trước rồi POST (một số FortiGate cần cookie phiên).
// In NGUYÊN VĂN mã HTTP + body + cookie. Bí mật truyền qua tham số, KHÔNG ghi tệp. Bỏ qua lỗi chứng chỉ (FortiGate tự ký).
//   forti_login_probe <host> <port> <user> <pass> [realm]
#include "tools/vpn/direct/FortinetProtocol.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkCookie>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QTimer>
#include <cstdio>

static QNetworkAccessManager* g_nam = nullptr;
static QString g_base;
static QByteArray g_ua = "Mozilla/5.0 SV1";

static QNetworkRequest req(const QString& path)
{
    QNetworkRequest r(QUrl(g_base + path));
    r.setHeader(QNetworkRequest::UserAgentHeader, g_ua);
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    r.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);
    r.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    r.setTransferTimeout(20000);
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone); // FortiGate hay tự ký - chỉ để CHẨN ĐOÁN
    r.setSslConfiguration(ssl);
    return r;
}

static void dump(const char* label, QNetworkReply* reply)
{
    const int st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    const auto cookies = reply->header(QNetworkRequest::SetCookieHeader).value<QList<QNetworkCookie>>();
    std::printf("\n== %s ==\n  HTTP %d, error=%s\n", label, st, reply->errorString().toUtf8().constData());
    std::printf("  body (%d byte): %s\n", body.size(), QString::fromUtf8(body.left(400)).toUtf8().constData());
    for (const QNetworkCookie& c : cookies)
        std::printf("  Set-Cookie: %s = %s\n", c.name().constData(), c.value().left(16).constData());
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc < 5) { std::printf("Dung: forti_login_probe <host> <port> <user> <pass> [realm]\n"); return 1; }
    const QString host = argv[1], port = argv[2];
    const QString user = QString::fromLocal8Bit(argv[3]), pass = QString::fromLocal8Bit(argv[4]);
    const QString realm = argc >= 6 ? QString::fromLocal8Bit(argv[5]) : QString();
    g_base = "https://" + host + ":" + port;
    g_nam = new QNetworkAccessManager();
    std::printf("== Do dang nhap FortiGate SSL-VPN %s (user %s, realm '%s') ==\n", g_base.toUtf8().constData(),
                user.toUtf8().constData(), realm.toUtf8().constData());

    const QByteArray loginBody = FortinetProtocol::loginBody(user, pass, realm);
    std::printf("  POST body: %s\n", loginBody.constData());

    // Bước 1: POST thang /remote/logincheck
    QNetworkRequest r1 = req("/remote/logincheck");
    r1.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* p1 = g_nam->post(r1, loginBody);
    QObject::connect(p1, &QNetworkReply::finished, [p1, host, port, loginBody]() {
        dump("POST /remote/logincheck (khong GET truoc)", p1);
        p1->deleteLater();
        // Bước 2: GET /remote/login truoc, roi POST voi cookie nhan duoc
        QNetworkReply* g = g_nam->get(req("/remote/login?lang=en"));
        QObject::connect(g, &QNetworkReply::finished, [g, loginBody]() {
            dump("GET /remote/login?lang=en", g);
            const auto cookies = g->header(QNetworkRequest::SetCookieHeader).value<QList<QNetworkCookie>>();
            g->deleteLater();
            QByteArray cookieHdr;
            for (const QNetworkCookie& c : cookies) { if (!cookieHdr.isEmpty()) cookieHdr += "; "; cookieHdr += c.name() + "=" + c.value(); }
            QNetworkRequest r2 = req("/remote/logincheck");
            r2.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
            if (!cookieHdr.isEmpty()) r2.setRawHeader("Cookie", cookieHdr);
            QNetworkReply* p2 = g_nam->post(r2, loginBody);
            QObject::connect(p2, &QNetworkReply::finished, [p2]() {
                dump("POST /remote/logincheck (CO cookie tu GET)", p2);
                p2->deleteLater();
                QTimer::singleShot(100, []() { QCoreApplication::quit(); });
            });
        });
    });

    QTimer::singleShot(40000, []() { std::printf("\n(qua han)\n"); QCoreApplication::quit(); });
    return app.exec();
}
