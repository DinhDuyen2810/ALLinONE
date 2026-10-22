#include "VpnGateClient.h"

#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

namespace
{
// API công khai của VPN Gate dành cho phần mềm khách bên thứ ba - trả danh sách máy chủ dạng CSV.
// HTTPS (không để HTTP trần: danh sách máy chủ là thứ sẽ trở thành đích kết nối VPN, không được để bị
// sửa trên đường truyền).
const QUrl kVpnGateApiUrl("https://www.vpngate.net/api/iphone/");
constexpr int kDefaultTimeoutMs = 40000; // danh sách ~1-2 MB, máy chủ ở Nhật có thể chậm với mạng VN
} // namespace

VpnGateClient::VpnGateClient(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
    , m_url(kVpnGateApiUrl)
    , m_timeoutMs(kDefaultTimeoutMs)
{
}

void VpnGateClient::setEndpointForTest(const QUrl& url, int timeoutMs)
{
    m_url = url;
    m_timeoutMs = timeoutMs;
}

void VpnGateClient::cancel()
{
    if (m_activeReply)
    {
        m_activeReply->disconnect(this);
        m_activeReply->abort();
        m_activeReply->deleteLater();
        m_activeReply = nullptr;
    }
}

void VpnGateClient::fetch()
{
    cancel();

    QNetworkRequest req(m_url);
    // Chỉ chấp nhận chuyển hướng trong cùng miền VPN Gate (không theo chuyển hướng sang máy chủ lạ - đây
    // là dữ liệu sẽ trở thành đích kết nối VPN).
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, "OneForAll-VPN/1.0");

    QNetworkReply* reply = m_nam->get(req);
    m_activeReply = reply;

    // Hết thời gian chờ: hủy yêu cầu (finished sẽ phát với lỗi OperationCanceledError).
    auto* timeout = new QTimer(reply);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, reply, [reply]() {
        if (reply->isRunning())
            reply->abort();
    });
    timeout->start(m_timeoutMs);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (m_activeReply != reply)
            return; // đã bị cancel()/fetch() mới thay thế - bỏ qua kết quả cũ
        m_activeReply = nullptr;

        if (reply->error() != QNetworkReply::NoError)
        {
            emit finished({}, "Không tải được danh sách máy chủ VPN Gate: " + reply->errorString());
            return;
        }
        const QByteArray body = reply->readAll();
        const QList<VpnGateServer> servers = VpnGateClientInternal::parseCsv(body);
        if (servers.isEmpty())
        {
            emit finished({}, "Danh sách máy chủ VPN Gate tải về trống hoặc không đọc được. Thử lại sau giây lát.");
            return;
        }
        emit finished(servers, QString());
    });
}

namespace VpnGateClientInternal
{

bool isValidIpv4(const QString& address)
{
    QHostAddress addr;
    if (!addr.setAddress(address.trimmed()))
        return false;
    return addr.protocol() == QAbstractSocket::IPv4Protocol;
}

QList<VpnGateServer> parseCsv(const QByteArray& csv)
{
    QList<VpnGateServer> servers;
    const QString text = QString::fromUtf8(csv);
    // Tách dòng theo cả \r\n lẫn \n.
    const QStringList lines = text.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));

    for (const QString& rawLine : lines)
    {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;
        // "*vpn_servers" (mở đầu) và "*" (kết thúc) và dòng tiêu đề cột "#HostName,..." - bỏ qua.
        if (line.startsWith(QChar('*')) || line.startsWith(QChar('#')))
            continue;

        // Chỉ cần 7 trường đầu (HostName,IP,Score,Ping,Speed,CountryLong,CountryShort). Các trường sau
        // (Message/Operator do người chia sẻ tự đặt, có thể chứa dấu phẩy) KHÔNG dùng tới nên không ảnh
        // hưởng. Giới hạn số lần tách để một Message dài nhiều dấu phẩy không tạo danh sách khổng lồ.
        const QStringList f = line.split(QChar(','));
        if (f.size() < 7)
            continue;

        VpnGateServer s;
        s.hostName = f[0].trimmed();
        s.ip = f[1].trimmed();
        s.countryLong = f[5].trimmed();
        s.countryShort = f[6].trimmed().toUpper();

        // Lọc dòng hỏng/lệch cột: IP phải là IPv4 thật (sẽ trở thành đích kết nối VPN), mã quốc gia phải
        // đúng 2 chữ cái (nếu tên quốc gia hiếm hoi chứa dấu phẩy thì các cột sau bị lệch, mã quốc gia khi
        // đó không còn là 2 chữ cái - bỏ qua cho an toàn).
        if (!isValidIpv4(s.ip))
            continue;
        if (s.countryShort.size() != 2 || !s.countryShort[0].isLetter() || !s.countryShort[1].isLetter())
            continue;
        if (s.countryLong.isEmpty())
            s.countryLong = s.countryShort;

        s.score = qMax<qint64>(0, f[2].trimmed().toLongLong());
        s.ping = qMax(0, f[3].trimmed().toInt());
        s.speedBps = qMax<qint64>(0, f[4].trimmed().toLongLong());
        s.sessions = f.size() > 7 ? qMax(0, f[7].trimmed().toInt()) : 0; // NumVpnSessions (chỉ đọc nếu có)

        // Cấu hình OpenVPN (.ovpn) Base64 là trường CUỐI CÙNG. Các trường Operator/Message ở giữa có thể
        // chứa dấu phẩy (làm lệch chỉ số), nhưng Base64 không có dấu phẩy và không có trường nào sau nó -
        // nên trường cuối LUÔN là cấu hình. Đủ 15 trường mới có cấu hình.
        if (f.size() >= 15)
        {
            const QByteArray ovpn = QByteArray::fromBase64(f.last().trimmed().toUtf8());
            if (ovpn.contains("remote ") && ovpn.size() < 200000)
                s.ovpnConfig = QString::fromUtf8(ovpn);
        }

        servers.push_back(s);
    }
    return servers;
}

} // namespace VpnGateClientInternal
