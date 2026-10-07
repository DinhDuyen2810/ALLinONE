#include "PublicIpChecker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace
{
// ipwho.is: HTTPS, không cần khóa API, giới hạn miễn phí rộng rãi. Đã THỬ ipapi.co trước - bị chặn
// 429 (Too Many Requests) khi kiểm tra tay nhiều lần liên tiếp trên cùng mạng; ipwho.is không gặp vấn
// đề này khi kiểm tra lại cùng lúc trên cùng máy/mạng.
const QUrl kIpInfoUrl("https://ipwho.is/");
constexpr int kTimeoutMs = 8000;
} // namespace

PublicIpChecker::PublicIpChecker(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

void PublicIpChecker::check()
{
    if (m_activeReply)
        return; // đã có yêu cầu đang chạy

    QNetworkRequest req(kIpInfoUrl);
    req.setHeader(QNetworkRequest::UserAgentHeader, "OneForAll-VPN-Check/1.0");
    m_activeReply = m_nam->get(req);
    QNetworkReply* thisReply = m_activeReply;

    QTimer::singleShot(kTimeoutMs, this, [this, thisReply]() {
        if (m_activeReply == thisReply)
            m_activeReply->abort();
    });

    connect(m_activeReply, &QNetworkReply::finished, this, [this, thisReply]() {
        if (m_activeReply != thisReply)
            return; // reply cũ đã bị thay bởi một yêu cầu mới hơn - bỏ qua
        m_activeReply = nullptr;
        thisReply->deleteLater();

        if (thisReply->error() != QNetworkReply::NoError)
        {
            emit errorOccurred("Không tra cứu được IP công khai: " + thisReply->errorString());
            return;
        }

        const QJsonDocument doc = QJsonDocument::fromJson(thisReply->readAll());
        if (!doc.isObject())
        {
            emit errorOccurred("Phản hồi không hợp lệ từ dịch vụ tra cứu IP.");
            return;
        }

        const QJsonObject obj = doc.object();
        if (!obj.value("success").toBool(true))
        {
            emit errorOccurred("Dịch vụ tra cứu IP báo lỗi: " + obj.value("message").toString("không rõ nguyên nhân"));
            return;
        }

        PublicIpInfo info;
        info.ip = obj.value("ip").toString();
        info.country = obj.value("country").toString();
        info.city = obj.value("city").toString();
        emit result(info);
    });
}
