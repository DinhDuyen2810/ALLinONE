#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

#include <functional>

/// Phiên PPP (RFC 1661 LCP + RFC 1332 IPCP + RFC 1877 DNS) chạy trên một kênh tin cậy (TLS) - dùng cho
/// đường hầm FortiGate SSL-VPN (FortiGate chạy pppd phía máy chủ, mỗi khung PPP được bọc thêm 6 byte, xem
/// FortinetFraming). THUẦN: không tự đọc/ghi mạng, không đồng hồ riêng - nơi dùng đưa khung nhận được vào
/// handleFrame(), gọi tick() định kỳ, và nhận khung cần gửi qua Callbacks::sendFrame. Nhờ vậy toàn bộ
/// thương lượng test được bằng "máy chủ giả" trong bộ nhớ.
///
/// Khung PPP ở đây luôn có dạng đầy đủ: FF 03 <giao thức 2 byte> <dữ liệu> (không HDLC, không FCS - TLS
/// đã bảo đảm toàn vẹn). Theo hành vi đã biết của FortiGate (mã OpenConnect ghi nhận), máy chủ từ chối nén
/// trường địa chỉ/giao thức nên ta không xin; nếu máy chủ vẫn gửi khung nén thì vẫn đọc được.
class PppSession
{
public:
    static constexpr quint16 kProtoIp = 0x0021;
    static constexpr quint16 kProtoIp6 = 0x0057;
    static constexpr quint16 kProtoLcp = 0xC021;
    static constexpr quint16 kProtoIpcp = 0x8021;

    struct Callbacks
    {
        std::function<void(const QByteArray& pppFrame)> sendFrame; ///< khung PPP đầy đủ cần gửi đi
        std::function<void(const QByteArray& ipPacket)> ipReceived; ///< gói IPv4 nhận từ đường hầm
        std::function<void()> opened;                                ///< LCP + IPCP đều mở - đường hầm dùng được
        std::function<void(const QString& reason)> terminated;       ///< phiên kết thúc (máy chủ ngắt / hết giờ / lỗi)
        std::function<void(const QString& message)> log;            ///< nhật ký gỡ lỗi (không chứa bí mật)
    };

    explicit PppSession(Callbacks cb);

    /// MRU ta đề nghị (mặc định 1354 = 1500 trừ chi phí TLS + bọc Fortinet + IPv4/TCP dự phòng).
    void setMru(int mru) { m_mru = mru; }
    /// IPv4 xin cấp (thứ tự byte máy) - 0 = để máy chủ chỉ định qua Configure-Nak (RFC 1332).
    void setRequestedIpv4(quint32 addr) { m_ip = addr; }
    /// Có xin DNS qua IPCP không (khi XML cấu hình chưa cho DNS).
    void setRequestDns(bool on) { m_reqDns[0] = m_reqDns[1] = on; }

    void start(qint64 nowMs);
    void handleFrame(const QByteArray& frame, qint64 nowMs);
    void tick(qint64 nowMs);
    /// Gửi một gói IPv4 qua đường hầm; false nếu phiên chưa mở.
    bool sendIp(const QByteArray& ipPacket);
    /// Ngắt chủ động (LCP Terminate-Request) - terminated() sẽ được gọi khi máy chủ xác nhận hoặc hết giờ.
    void close(const QString& reason, qint64 nowMs);

    bool isOpened() const { return m_state == State::Opened; }
    bool isDead() const { return m_state == State::Dead; }
    quint32 localIpv4() const { return m_ip; }
    QList<quint32> dnsServers() const;
    int peerMru() const { return m_peerMru; }

    // ---- Tiện ích thuần ----
    static QByteArray buildFrame(quint16 proto, const QByteArray& payload);
    /// Bóc khung: chấp nhận cả dạng có/không FF 03 và trường giao thức 1 byte (PFC). false nếu hỏng.
    static bool parseFrame(const QByteArray& frame, quint16* proto, QByteArray* payload);
    /// Gói điều khiển LCP/IPCP: code, id, độ dài 2 byte (gồm 4 byte đầu), dữ liệu.
    static QByteArray buildControl(quint8 code, quint8 id, const QByteArray& data);

    // Mã gói điều khiển (RFC 1661 §5).
    enum Code : quint8
    {
        ConfReq = 1, ConfAck = 2, ConfNak = 3, ConfRej = 4, TermReq = 5, TermAck = 6,
        CodeRej = 7, ProtRej = 8, EchoReq = 9, EchoRep = 10, DiscardReq = 11
    };

private:
    enum class State { Initial, Establish, Network, Opened, Terminating, Dead };
    struct Ncp
    {
        bool reqSent{false};
        bool ackRcvd{false};
        bool ackSent{false};
        quint8 id{0};
        qint64 lastReq{0};
        int retries{0};
    };

    void sendControl(quint16 proto, quint8 code, quint8 id, const QByteArray& data);
    void sendLcpConfReq(qint64 now);
    void sendIpcpConfReq(qint64 now);
    void handleLcp(quint8 code, quint8 id, const QByteArray& data, qint64 now);
    void handleIpcp(quint8 code, quint8 id, const QByteArray& data, qint64 now);
    void advance(qint64 now);
    void die(const QString& reason);
    void log(const QString& m) const;

    Callbacks m_cb;
    State m_state{State::Initial};
    Ncp m_lcp;
    Ncp m_ipcp;

    int m_mru{1354};
    bool m_sendMru{true};
    bool m_sendMagic{true};
    quint32 m_magic{0};
    int m_peerMru{0};

    quint32 m_ip{0};
    bool m_reqDns[2]{true, true};
    quint32 m_dns[2]{0, 0};

    qint64 m_lastRx{0};
    qint64 m_lastEchoSent{0};
    int m_unansweredEchoes{0};
    quint8 m_echoId{0};
    quint8 m_termId{0};
    qint64 m_termSentAt{0};
    QString m_termReason;
};

/// Khung bọc của đường hầm FortiGate SSL-VPN: mỗi khung PPP đi kèm 6 byte đầu
/// [tổng độ dài = n+6 (2 byte BE)] [0x50 0x50] [n (2 byte BE)] rồi n byte khung PPP.
namespace FortinetFraming
{
QByteArray encapsulate(const QByteArray& pppFrame);

/// Tách luồng byte nhận được thành từng khung PPP (dữ liệu có thể tới rời rạc/dính nhau).
class Deframer
{
public:
    enum class Result { NeedMore, Frame, Error };
    void feed(const QByteArray& data) { m_buf.append(data); }
    /// Frame: *frame nhận một khung PPP. Error: *error mô tả - khi máy chủ từ chối yêu cầu đường hầm, nó
    /// trả về một phản hồi HTTP ("HTTP/1.1 403 ...") thay vì khung PPP; báo đúng dòng trạng thái đó.
    Result next(QByteArray* frame, QString* error);

private:
    QByteArray m_buf;
};
} // namespace FortinetFraming
