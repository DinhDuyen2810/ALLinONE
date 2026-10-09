#pragma once

#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;

/// Thông tin IP công khai + quốc gia hiện thấy được - dùng để XÁC NHẬN cho người dùng rằng VPN thật sự
/// đang đổi IP ra nước ngoài (không chỉ tin vào trạng thái "Connected" của Windows, vốn chỉ có nghĩa là
/// đường hầm VPN đã dựng xong, không chắc traffic thật sự đi qua nó).
struct PublicIpInfo
{
    QString ip;
    QString country;
    QString city;
};

/**
 * @brief Tra cứu IP công khai + quốc gia hiện tại qua một dịch vụ "what is my IP" công khai
 * (https://ipwho.is/, không cần khóa API - cùng tinh thần dùng endpoint công khai của Cloudflare mà
 * SpeedTestRunner đã dùng; ipapi.co từng được thử và bị giới hạn 429, xem PublicIpChecker.cpp). Chỉ đọc,
 * không gửi gì ngoài 1 yêu cầu GET.
 */
class PublicIpChecker : public QObject
{
    Q_OBJECT

public:
    explicit PublicIpChecker(QObject* parent = nullptr);

    void check();

    /// CHỈ dùng cho kiểm thử: trỏ sang một máy chủ cục bộ (127.0.0.1) và rút ngắn thời gian chờ.
    void setEndpointForTest(const QUrl& url, int timeoutMs);

signals:
    void result(PublicIpInfo info);
    void errorOccurred(QString message);

private:
    QNetworkAccessManager* m_nam{nullptr};
    /// QPointer: tự về null nếu phản hồi bị hủy ở nơi khác - không bao giờ so/gọi qua một địa chỉ đã giải phóng.
    QPointer<QNetworkReply> m_activeReply;
    QUrl m_url;
    int m_timeoutMs{0};
};
