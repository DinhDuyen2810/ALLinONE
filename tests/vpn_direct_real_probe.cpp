// KIỂM TRA TAY (không phải test tự động - gọi máy chủ VPN THẬT, KHÔNG gửi thông tin đăng nhập): với mỗi hồ sơ
// FortiGate SSL-VPN / GlobalProtect quét được trên máy, dùng ĐÚNG ngăn TLS của ứng dụng (QSslSocket/QNAM) để:
//   - bắt tay TLS, in vân tay SHA-256 chứng chỉ (cùng cách TunnelEngine ghim) và việc Windows có tin cậy không;
//   - gọi endpoint KHÔNG cần đăng nhập: FortiGate GET /remote/login, GlobalProtect POST prelogin.esp (+ phân tích
//     bằng GlobalProtectProtocol::parsePrelogin như lúc kết nối thật).
// cmake --build build --target vpn_direct_real_probe && build\vpn_direct_real_probe.exe

#include "tools/vpn/direct/DirectVpnTypes.h"
#include "tools/vpn/direct/GlobalProtectProtocol.h"
#include "tools/vpn/engine/ThirdPartyVpnScanner.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSslSocket>
#include <QTimer>

#include <cstdio>

namespace
{
void out(const QString& s)
{
    std::printf("%s\n", s.toUtf8().constData());
    std::fflush(stdout);
}

QByteArray request(QNetworkAccessManager& nam, const QByteArray& method, const QUrl& url, const QByteArray& body, int* status)
{
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(15000);
    req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/x-www-form-urlencoded"));
    QNetworkReply* r = method == "POST" ? nam.post(req, body) : nam.get(req);
    QObject::connect(r, &QNetworkReply::sslErrors, r, [r](const QList<QSslError>&) { r->ignoreSslErrors(); }); // chỉ thăm dò
    QEventLoop loop;
    QObject::connect(r, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    *status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray data = r->readAll();
    if (*status == 0)
        out("    loi: " + r->errorString());
    r->deleteLater();
    return data;
}
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    out("TLS backend: " + QSslSocket::activeBackend());
    QNetworkAccessManager nam;
    int checked = 0, ok = 0;
    for (const ThirdPartyVpnProfile& p : ThirdPartyVpnScanner::scan())
    {
        if (!p.canConnectDirectly())
            continue;
        QString host;
        int port = 443;
        if (!DirectVpn::splitHostPort(p.server, 443, &host, &port))
            continue;
        ++checked;
        out(QString("\n[%1] %2 -> %3:%4 (%5)").arg(p.clientName, p.name, host).arg(port).arg(DirectVpn::protocolName(p.direct)));

        QSslSocket s;
        QList<QSslError> errs;
        QObject::connect(&s, &QSslSocket::sslErrors, &s, [&](const QList<QSslError>& e) { errs = e; s.ignoreSslErrors(); });
        s.connectToHostEncrypted(host, static_cast<quint16>(port));
        const bool enc = s.waitForEncrypted(15000);
        out(QString("    TLS: %1, giao thuc %2").arg(enc ? "OK" : "LOI " + s.errorString()).arg(int(s.sessionProtocol())));
        if (enc)
        {
            const QSslCertificate c = s.peerCertificate();
            out("    chu the: " + c.subjectDisplayName() + " | cap boi: " + c.issuerDisplayName());
            out("    SHA-256: " + QString::fromLatin1(c.digest(QCryptographicHash::Sha256).toHex()));
            out(errs.isEmpty() ? QStringLiteral("    Windows TIN CAY chung chi (khong can ghim)")
                               : QStringLiteral("    chung chi CHUA duoc tin cay (%1 loi) -> app se hoi nguoi dung + ghim van tay").arg(errs.size()));
        }
        s.abort();

        const QString base = QString("https://%1:%2").arg(host).arg(port);
        int status = 0;
        if (p.direct == DirectProtocol::FortinetSsl)
        {
            const QByteArray body = request(nam, "GET", QUrl(base + "/remote/login?lang=en"), {}, &status);
            const bool isForti = body.contains("logincheck") || body.contains("/remote/") || body.contains("sslvpn") ||
                                 body.contains("fortinet") || body.contains("FortiGate");
            out(QString("    GET /remote/login -> HTTP %1, %2 byte, dau hieu FortiGate SSL-VPN: %3").arg(status).arg(body.size()).arg(isForti ? "CO" : "khong"));
            if (status == 200 && isForti)
                ++ok;
        }
        else
        {
            const QByteArray body = request(nam, "POST", QUrl(base + "/global-protect/prelogin.esp?tmp=tmp&clientVer=4100&clientos=Windows"),
                                            "cas-support=yes", &status);
            GlobalProtectProtocol::Prelogin pre;
            QString err;
            const bool parsed = GlobalProtectProtocol::parsePrelogin(body, &pre, &err);
            out(QString("    prelogin -> HTTP %1, phan tich: %2, SAML: %3, loi nhac: \"%4\"")
                    .arg(status).arg(parsed ? "OK" : "LOI " + err).arg(pre.saml ? "co (chua ho tro)" : "khong").arg(pre.message));
            if (parsed && !pre.saml)
                ++ok;
        }
    }
    out(QString("\nKet qua: %1/%2 may chu san sang cho ket noi truc tiep (chua dang nhap).").arg(ok).arg(checked));
    return ok == checked && checked > 0 ? 0 : 1;
}
