#include "PublicIpv6Checker.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace
{
// Hai tên miền này CHỈ có bản ghi AAAA (đã kiểm bằng Resolve-DnsName: không có bản ghi A) - yêu cầu tới chúng chỉ
// đi được bằng IPv6. ipwho.is (hai ngăn xếp) chỉ dùng để tra vị trí của địa chỉ đã biết.
const QList<QUrl> kEchoUrls{QUrl("https://api6.ipify.org/?format=json"), QUrl("https://ipv6.icanhazip.com/")};
const QUrl kGeoBase("https://ipwho.is/");
constexpr int kTimeoutMs = 8000;
} // namespace

PublicIpv6Checker::PublicIpv6Checker(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_echoUrls(kEchoUrls)
    , m_geoBase(kGeoBase)
    , m_timeoutMs(kTimeoutMs)
{
}

void PublicIpv6Checker::setEndpointsForTest(const QList<QUrl>& echoUrls, const QUrl& geoBase, int timeoutMs)
{
    m_echoUrls = echoUrls;
    m_geoBase = geoBase;
    m_timeoutMs = timeoutMs;
}

QString PublicIpv6Checker::parseEchoBody(const QByteArray& body)
{
    QString text = QString::fromUtf8(body).trimmed();
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    if (doc.isObject())
        text = doc.object().value("ip").toString().trimmed();
    QHostAddress a;
    if (text.isEmpty() || text.size() > 64 || !a.setAddress(text) || a.protocol() != QAbstractSocket::IPv6Protocol)
        return QString();
    return a.toString();
}

QNetworkReply* PublicIpv6Checker::get(const QUrl& url)
{
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "OneForAll-VPN-Check/1.0");
    QNetworkReply* reply = m_nam->get(req);
    m_activeReply = reply;
    auto* timeout = new QTimer(reply); // con của chính phản hồi - xem PublicIpChecker.cpp
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout->start(m_timeoutMs);
    return reply;
}

void PublicIpv6Checker::check()
{
    if (m_activeReply)
        return;
    // Ngay sau khi bật/tắt VPN hay chặn IPv6, kết nối HTTPS giữ sẵn có thể còn gắn với đường cũ - luôn mở mới.
    m_nam->clearConnectionCache();
    m_lastError.clear();
    startEcho(0);
}

void PublicIpv6Checker::startEcho(int index)
{
    if (index >= m_echoUrls.size())
    {
        PublicIpv6Info info;
        info.error = m_lastError;
        emit result(info);
        return;
    }
    QNetworkReply* reply = get(m_echoUrls[index]);
    connect(reply, &QNetworkReply::finished, this, [this, reply, index]() {
        reply->deleteLater();
        if (m_activeReply != reply)
            return;
        m_activeReply = nullptr;
        QString ip;
        if (reply->error() == QNetworkReply::NoError)
            ip = parseEchoBody(reply->readAll());
        else
            m_lastError = reply->errorString();
        if (ip.isEmpty())
        {
            if (m_lastError.isEmpty())
                m_lastError = QStringLiteral("phản hồi không phải địa chỉ IPv6");
            startEcho(index + 1);
            return;
        }
        startGeo(ip);
    });
}

void PublicIpv6Checker::startGeo(const QString& ip)
{
    QUrl url = m_geoBase;
    url.setPath(url.path() + ip);
    QNetworkReply* reply = get(url);
    connect(reply, &QNetworkReply::finished, this, [this, reply, ip]() {
        reply->deleteLater();
        if (m_activeReply != reply)
            return;
        m_activeReply = nullptr;
        PublicIpv6Info info;
        info.reachable = true;
        info.ip = ip;
        if (reply->error() == QNetworkReply::NoError)
        {
            const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
            if (o.value("success").toBool(true))
            {
                info.country = o.value("country").toString();
                info.city = o.value("city").toString();
            }
        }
        emit result(info);
    });
}
