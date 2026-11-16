#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

/// Kết quả kiểm tra IPv6 công khai: Internet đang thấy máy này bằng IPv6 nào (nếu có).
struct PublicIpv6Info
{
    bool reachable{false}; ///< ra được Internet bằng IPv6 (false = không có IPv6 / đã bị chặn - không lộ)
    QString ip;            ///< IPv6 mà Internet thấy (chỉ khi reachable)
    QString country;       ///< có thể rỗng nếu tra vị trí thất bại
    QString city;
    QString error;         ///< lỗi cuối cùng khi !reachable (để chẩn đoán)
};

/**
 * @brief Hỏi một dịch vụ "IP của tôi" CHỈ CÓ IPv6 (tên miền chỉ có bản ghi AAAA - api6.ipify.org, dự phòng
 * ipv6.icanhazip.com; đã kiểm bằng DNS thật) nên yêu cầu BẮT BUỘC đi bằng IPv6: trả lời được = máy đang lộ ra
 * Internet bằng IPv6 đó; không tới được = không có IPv6 ra Internet (hoặc đã bị chặn) - đúng thứ người dùng cần
 * biết sau khi "đổi IP". Có IPv6 thì tra tiếp quốc gia của chính địa chỉ đó qua ipwho.is/<ip>.
 *
 * Chỉ gửi yêu cầu GET tiêu chuẩn, không kèm thông tin định danh (giống PublicIpChecker).
 */
class PublicIpv6Checker : public QObject
{
    Q_OBJECT

public:
    explicit PublicIpv6Checker(QObject* parent = nullptr);

    void check();
    bool isRunning() const { return m_activeReply != nullptr; }

    /// CHỈ kiểm thử: các điểm "IP của tôi" (thử lần lượt) + gốc URL tra vị trí (nối thêm địa chỉ IP) + thời gian chờ.
    void setEndpointsForTest(const QList<QUrl>& echoUrls, const QUrl& geoBase, int timeoutMs);

    /// THUẦN: rút địa chỉ từ phản hồi ({"ip":"..."} hoặc văn bản trơn); rỗng nếu không phải IPv6 hợp lệ.
    static QString parseEchoBody(const QByteArray& body);

signals:
    void result(PublicIpv6Info info);

private:
    void startEcho(int index);
    void startGeo(const QString& ip);
    QNetworkReply* get(const QUrl& url);

    QNetworkAccessManager* m_nam{nullptr};
    QPointer<QNetworkReply> m_activeReply;
    QList<QUrl> m_echoUrls;
    QUrl m_geoBase;
    int m_timeoutMs{8000};
    QString m_lastError;
};
