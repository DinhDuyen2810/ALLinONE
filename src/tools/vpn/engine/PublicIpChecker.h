#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

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

signals:
    void result(PublicIpInfo info);
    void errorOccurred(QString message);

private:
    QNetworkAccessManager* m_nam{nullptr};
    QNetworkReply* m_activeReply{nullptr};
};
