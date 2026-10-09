#include "PublicIpChecker.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

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
    , m_url(kIpInfoUrl)
    , m_timeoutMs(kTimeoutMs)
{
}

void PublicIpChecker::setEndpointForTest(const QUrl& url, int timeoutMs)
{
    m_url = url;
    m_timeoutMs = timeoutMs;
}

void PublicIpChecker::check()
{
    if (m_activeReply)
        return; // đã có yêu cầu đang chạy

    QNetworkRequest req(m_url);
    req.setHeader(QNetworkRequest::UserAgentHeader, "OneForAll-VPN-Check/1.0");
    QNetworkReply* thisReply = m_nam->get(req);
    m_activeReply = thisReply;

    // Bộ hẹn giờ là CON của chính phản hồi này và chỉ nối tới nó: phản hồi xong (deleteLater bên dưới) thì bộ
    // hẹn giờ bị hủy theo. Trước đây dùng QTimer::singleShot(8 giây) giữ con trỏ thô rồi so
    // "m_activeReply == thisReply" - phản hồi cũ xong sớm và bị hủy, lần check() kế tiếp có thể được cấp phát
    // ĐÚNG địa chỉ đó, và bộ hẹn giờ của lần trước hủy nhầm phản hồi MỚI (vừa gửi chưa tới 8 giây).
    auto* timeout = new QTimer(thisReply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, thisReply, &QNetworkReply::abort);
    timeout->start(m_timeoutMs);

    connect(thisReply, &QNetworkReply::finished, this, [this, thisReply]() {
        thisReply->deleteLater();
        if (m_activeReply != thisReply)
            return; // reply cũ đã bị thay bởi một yêu cầu mới hơn - bỏ qua
        m_activeReply = nullptr;

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
