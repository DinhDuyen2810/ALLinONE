#include "PppSession.h"

#include <QRandomGenerator>

namespace
{
constexpr qint64 kRetransmitMs = 3000;  // gửi lại Configure-Request nếu chưa được trả lời
constexpr int kMaxRetries = 10;         // ~30 giây thương lượng tối đa mỗi tầng
constexpr qint64 kEchoIdleMs = 15000;   // im lặng 15 giây thì gửi LCP Echo-Request để thăm dò
constexpr int kMaxUnansweredEchoes = 4; // 4 lần (~60 giây) không trả lời -> coi như máy chủ mất
constexpr qint64 kTermTimeoutMs = 3000;

// Tùy chọn LCP (RFC 1661 §6) / IPCP (RFC 1332, RFC 1877).
constexpr quint8 kLcpMru = 1, kLcpAsyncMap = 2, kLcpMagic = 5, kLcpPfc = 7, kLcpAcfc = 8;
constexpr quint8 kIpcpAddr = 3, kIpcpDns1 = 129, kIpcpDns2 = 131;

void appendBe16(QByteArray& b, quint16 v)
{
    b.append(static_cast<char>(v >> 8));
    b.append(static_cast<char>(v & 0xFF));
}
void appendBe32(QByteArray& b, quint32 v)
{
    appendBe16(b, static_cast<quint16>(v >> 16));
    appendBe16(b, static_cast<quint16>(v & 0xFFFF));
}
quint16 be16(const QByteArray& b, int pos)
{
    return static_cast<quint16>((static_cast<quint8>(b[pos]) << 8) | static_cast<quint8>(b[pos + 1]));
}
quint32 be32(const QByteArray& b, int pos)
{
    return (static_cast<quint32>(be16(b, pos)) << 16) | be16(b, pos + 2);
}
QByteArray option(quint8 type, const QByteArray& value)
{
    QByteArray o;
    o.append(static_cast<char>(type));
    o.append(static_cast<char>(value.size() + 2));
    o.append(value);
    return o;
}
QByteArray u32(quint32 v)
{
    QByteArray b;
    appendBe32(b, v);
    return b;
}

/// Duyệt các tùy chọn TLV; false nếu dữ liệu hỏng (độ dài sai).
template <typename Fn> bool forEachOption(const QByteArray& data, Fn fn)
{
    int i = 0;
    while (i + 2 <= data.size())
    {
        const quint8 type = static_cast<quint8>(data[i]);
        const int len = static_cast<quint8>(data[i + 1]);
        if (len < 2 || i + len > data.size())
            return false;
        fn(type, data.mid(i + 2, len - 2), data.mid(i, len));
        i += len;
    }
    return i == data.size();
}

QString ipStr(quint32 a)
{
    return QStringLiteral("%1.%2.%3.%4").arg((a >> 24) & 0xFF).arg((a >> 16) & 0xFF).arg((a >> 8) & 0xFF).arg(a & 0xFF);
}
} // namespace

PppSession::PppSession(Callbacks cb)
    : m_cb(std::move(cb))
{
    do
        m_magic = QRandomGenerator::system()->generate();
    while (m_magic == 0);
}

QList<quint32> PppSession::dnsServers() const
{
    QList<quint32> out;
    for (quint32 d : m_dns)
        if (d != 0)
            out << d;
    return out;
}

QByteArray PppSession::buildFrame(quint16 proto, const QByteArray& payload)
{
    QByteArray f;
    f.reserve(payload.size() + 4);
    f.append(static_cast<char>(0xFF));
    f.append(static_cast<char>(0x03));
    appendBe16(f, proto);
    f.append(payload);
    return f;
}

bool PppSession::parseFrame(const QByteArray& frame, quint16* proto, QByteArray* payload)
{
    int pos = 0;
    if (frame.size() >= 2 && static_cast<quint8>(frame[0]) == 0xFF && static_cast<quint8>(frame[1]) == 0x03)
        pos = 2;
    if (pos >= frame.size())
        return false;
    quint16 p = 0;
    // RFC 1661 §6.5: trường giao thức nén còn 1 byte khi byte đầu LẺ.
    if (static_cast<quint8>(frame[pos]) & 1)
    {
        p = static_cast<quint8>(frame[pos]);
        pos += 1;
    }
    else
    {
        if (pos + 2 > frame.size())
            return false;
        p = be16(frame, pos);
        pos += 2;
    }
    if (proto)
        *proto = p;
    if (payload)
        *payload = frame.mid(pos);
    return true;
}

QByteArray PppSession::buildControl(quint8 code, quint8 id, const QByteArray& data)
{
    QByteArray p;
    p.append(static_cast<char>(code));
    p.append(static_cast<char>(id));
    appendBe16(p, static_cast<quint16>(data.size() + 4));
    p.append(data);
    return p;
}

void PppSession::log(const QString& m) const
{
    if (m_cb.log)
        m_cb.log(m);
}

void PppSession::sendControl(quint16 proto, quint8 code, quint8 id, const QByteArray& data)
{
    if (m_cb.sendFrame)
        m_cb.sendFrame(buildFrame(proto, buildControl(code, id, data)));
}

void PppSession::start(qint64 nowMs)
{
    m_state = State::Establish;
    m_lastRx = nowMs;
    sendLcpConfReq(nowMs);
}

void PppSession::sendLcpConfReq(qint64 now)
{
    QByteArray opts;
    if (m_sendMru)
    {
        QByteArray v;
        appendBe16(v, static_cast<quint16>(m_mru));
        opts += option(kLcpMru, v);
    }
    if (m_sendMagic)
        opts += option(kLcpMagic, u32(m_magic));
    m_lcp.id = static_cast<quint8>(m_lcp.id + 1);
    m_lcp.reqSent = true;
    m_lcp.lastReq = now;
    sendControl(kProtoLcp, ConfReq, m_lcp.id, opts);
}

void PppSession::sendIpcpConfReq(qint64 now)
{
    QByteArray opts = option(kIpcpAddr, u32(m_ip));
    if (m_reqDns[0])
        opts += option(kIpcpDns1, u32(m_dns[0]));
    if (m_reqDns[1])
        opts += option(kIpcpDns2, u32(m_dns[1]));
    m_ipcp.id = static_cast<quint8>(m_ipcp.id + 1);
    m_ipcp.reqSent = true;
    m_ipcp.lastReq = now;
    sendControl(kProtoIpcp, ConfReq, m_ipcp.id, opts);
}

void PppSession::handleFrame(const QByteArray& frame, qint64 nowMs)
{
    if (m_state == State::Dead || m_state == State::Initial)
        return;
    quint16 proto = 0;
    QByteArray payload;
    if (!parseFrame(frame, &proto, &payload))
    {
        log(QStringLiteral("PPP: bỏ qua khung hỏng (%1 byte)").arg(frame.size()));
        return;
    }
    m_lastRx = nowMs;
    m_unansweredEchoes = 0;

    if (proto == kProtoIp)
    {
        if (m_state == State::Opened && m_cb.ipReceived)
            m_cb.ipReceived(payload);
        return;
    }
    if (proto == kProtoIp6)
        return; // không cấu hình IPv6 - bỏ gói

    if (proto == kProtoLcp || proto == kProtoIpcp)
    {
        if (payload.size() < 4)
            return;
        const quint8 code = static_cast<quint8>(payload[0]);
        const quint8 id = static_cast<quint8>(payload[1]);
        int len = be16(payload, 2);
        if (len < 4 || len > payload.size())
            return;
        const QByteArray data = payload.mid(4, len - 4); // bỏ đệm thừa sau độ dài khai báo
        if (proto == kProtoLcp)
            handleLcp(code, id, data, nowMs);
        else
            handleIpcp(code, id, data, nowMs);
        advance(nowMs);
        return;
    }

    // Giao thức khác (IPv6CP, CCP nén dữ liệu...) chúng ta không hỗ trợ: Protocol-Reject (RFC 1661 §5.7)
    // để máy chủ thôi thương lượng thay vì gửi lại mãi.
    if (m_lcp.ackSent && m_lcp.ackRcvd)
    {
        QByteArray rej;
        appendBe16(rej, proto);
        rej.append(payload.left(qMax(0, (m_peerMru > 0 ? m_peerMru : 1500) - 6)));
        sendControl(kProtoLcp, ProtRej, ++m_echoId, rej);
        log(QStringLiteral("PPP: từ chối giao thức 0x%1").arg(proto, 4, 16, QChar('0')));
    }
}

void PppSession::handleLcp(quint8 code, quint8 id, const QByteArray& data, qint64 now)
{
    switch (code)
    {
        case ConfReq:
        {
            QByteArray rej;
            int peerMru = 0;
            bool ok = forEachOption(data, [&](quint8 type, const QByteArray& v, const QByteArray& whole) {
                switch (type)
                {
                    case kLcpMru:
                        if (v.size() == 2)
                            peerMru = be16(v, 0);
                        else
                            rej += whole;
                        break;
                    case kLcpMagic:
                        if (v.size() != 4)
                            rej += whole;
                        break;
                    case kLcpAsyncMap: // vô nghĩa trên TLS nhưng vô hại - chấp nhận
                    case kLcpPfc:      // máy chủ được phép gửi khung nén; ta đọc được cả hai dạng
                    case kLcpAcfc:
                        break;
                    default: // xác thực PPP (PAP/CHAP), callback... - FortiGate không dùng; từ chối
                        rej += whole;
                        break;
                }
            });
            if (!ok)
            {
                log(QStringLiteral("PPP: LCP Configure-Request hỏng - bỏ qua"));
                return;
            }
            if (!rej.isEmpty())
            {
                sendControl(kProtoLcp, ConfRej, id, rej);
                return;
            }
            if (peerMru > 0)
                m_peerMru = peerMru;
            sendControl(kProtoLcp, ConfAck, id, data);
            m_lcp.ackSent = true;
            return;
        }
        case ConfAck:
            if (id == m_lcp.id)
                m_lcp.ackRcvd = true;
            return;
        case ConfNak:
        case ConfRej:
        {
            if (id != m_lcp.id)
                return; // trả lời cho yêu cầu cũ
            forEachOption(data, [&](quint8 type, const QByteArray& v, const QByteArray&) {
                if (type == kLcpMru)
                {
                    if (code == ConfNak && v.size() == 2 && be16(v, 0) >= 576)
                        m_mru = be16(v, 0);
                    else
                        m_sendMru = false;
                }
                else if (type == kLcpMagic)
                {
                    if (code == ConfNak) // trùng magic (vòng lặp) -> chọn số mới
                        m_magic = QRandomGenerator::system()->generate() | 1u;
                    else
                        m_sendMagic = false;
                }
            });
            if (++m_lcp.retries > kMaxRetries)
            {
                die(QStringLiteral("Máy chủ không chấp nhận tham số kết nối PPP (LCP)."));
                return;
            }
            sendLcpConfReq(now);
            return;
        }
        case TermReq:
        {
            sendControl(kProtoLcp, TermAck, id, QByteArray());
            const QString reason = data.isEmpty() ? QString() : QString::fromUtf8(data).trimmed();
            die(reason.isEmpty() ? QStringLiteral("Máy chủ đã ngắt phiên VPN.")
                                 : QStringLiteral("Máy chủ đã ngắt phiên VPN: %1").arg(reason));
            return;
        }
        case TermAck:
            if (m_state == State::Terminating)
                die(m_termReason);
            return;
        case EchoReq:
            if (m_lcp.ackSent && m_lcp.ackRcvd)
                sendControl(kProtoLcp, EchoRep, id, u32(m_magic) + data.mid(4));
            return;
        case EchoRep:
        case DiscardReq:
            return;
        case ProtRej:
            if (data.size() >= 2 && be16(data, 0) == kProtoIpcp)
                die(QStringLiteral("Máy chủ từ chối cấp địa chỉ IPv4 (IPCP)."));
            return;
        case CodeRej:
            return;
        default:
        {
            // Mã lạ: Code-Reject kèm nguyên gói (RFC 1661 §5.6).
            sendControl(kProtoLcp, CodeRej, ++m_echoId, buildControl(code, id, data));
            return;
        }
    }
}

void PppSession::handleIpcp(quint8 code, quint8 id, const QByteArray& data, qint64 now)
{
    // IPCP chỉ có nghĩa sau khi LCP đã mở (RFC 1661 §3.6) - gói tới sớm thì bỏ, máy chủ sẽ gửi lại.
    if (!(m_lcp.ackSent && m_lcp.ackRcvd))
        return;
    switch (code)
    {
        case ConfReq:
        {
            QByteArray rej;
            bool ok = forEachOption(data, [&](quint8 type, const QByteArray& v, const QByteArray& whole) {
                if (type == kIpcpAddr && v.size() == 4)
                    return; // địa chỉ đầu bên kia - chấp nhận
                rej += whole; // nén IP (VJ), địa chỉ cũ dạng cặp... - không hỗ trợ
            });
            if (!ok)
                return;
            if (!rej.isEmpty())
            {
                sendControl(kProtoIpcp, ConfRej, id, rej);
                return;
            }
            sendControl(kProtoIpcp, ConfAck, id, data);
            m_ipcp.ackSent = true;
            return;
        }
        case ConfAck:
            if (id == m_ipcp.id)
                m_ipcp.ackRcvd = true;
            return;
        case ConfNak:
        case ConfRej:
        {
            if (id != m_ipcp.id)
                return;
            bool fatal = false;
            forEachOption(data, [&](quint8 type, const QByteArray& v, const QByteArray&) {
                if (type == kIpcpAddr)
                {
                    if (code == ConfNak && v.size() == 4 && be32(v, 0) != 0)
                        m_ip = be32(v, 0); // máy chủ chỉ định địa chỉ cho ta
                    else
                        fatal = true;      // không có địa chỉ thì không có đường hầm
                }
                else if (type == kIpcpDns1 || type == kIpcpDns2)
                {
                    const int idx = type == kIpcpDns1 ? 0 : 1;
                    if (code == ConfNak && v.size() == 4 && be32(v, 0) != 0)
                        m_dns[idx] = be32(v, 0);
                    else
                        m_reqDns[idx] = false; // máy chủ không cấp DNS qua IPCP - thôi xin
                }
            });
            if (fatal)
            {
                die(QStringLiteral("Máy chủ từ chối cấp địa chỉ IPv4 cho phiên VPN."));
                return;
            }
            if (++m_ipcp.retries > kMaxRetries)
            {
                die(QStringLiteral("Không thương lượng được địa chỉ IP với máy chủ (IPCP)."));
                return;
            }
            sendIpcpConfReq(now);
            return;
        }
        case TermReq:
            sendControl(kProtoIpcp, TermAck, id, QByteArray());
            die(QStringLiteral("Máy chủ đã đóng tầng IP của phiên VPN."));
            return;
        default:
            return;
    }
}

void PppSession::advance(qint64 now)
{
    if (m_state == State::Establish && m_lcp.ackSent && m_lcp.ackRcvd)
    {
        m_state = State::Network;
        log(QStringLiteral("PPP: LCP đã mở (MRU máy chủ %1) - thương lượng IPCP").arg(m_peerMru));
        sendIpcpConfReq(now);
    }
    if (m_state == State::Network && m_ipcp.ackSent && m_ipcp.ackRcvd)
    {
        m_state = State::Opened;
        m_lastEchoSent = now;
        log(QStringLiteral("PPP: IPCP đã mở - địa chỉ %1").arg(ipStr(m_ip)));
        if (m_cb.opened)
            m_cb.opened();
    }
}

void PppSession::tick(qint64 now)
{
    switch (m_state)
    {
        case State::Establish:
            if (m_lcp.reqSent && !m_lcp.ackRcvd && now - m_lcp.lastReq >= kRetransmitMs)
            {
                if (++m_lcp.retries > kMaxRetries)
                    die(QStringLiteral("Máy chủ không phản hồi thương lượng PPP (LCP)."));
                else
                    sendLcpConfReq(now);
            }
            break;
        case State::Network:
            if (m_ipcp.reqSent && !m_ipcp.ackRcvd && now - m_ipcp.lastReq >= kRetransmitMs)
            {
                if (++m_ipcp.retries > kMaxRetries)
                    die(QStringLiteral("Máy chủ không cấp địa chỉ IP (IPCP hết thời gian chờ)."));
                else
                    sendIpcpConfReq(now);
            }
            break;
        case State::Opened:
            // Thăm dò khi im lặng: máy chủ chết/đường truyền đứt mà TCP chưa báo thì tự phát hiện.
            if (now - m_lastRx >= kEchoIdleMs && now - m_lastEchoSent >= kEchoIdleMs)
            {
                if (m_unansweredEchoes >= kMaxUnansweredEchoes)
                {
                    die(QStringLiteral("Máy chủ VPN không còn phản hồi (mất kết nối)."));
                    return;
                }
                ++m_unansweredEchoes;
                m_lastEchoSent = now;
                sendControl(kProtoLcp, EchoReq, ++m_echoId, u32(m_magic));
            }
            break;
        case State::Terminating:
            if (now - m_termSentAt >= kTermTimeoutMs)
                die(m_termReason);
            break;
        default:
            break;
    }
}

bool PppSession::sendIp(const QByteArray& ipPacket)
{
    if (m_state != State::Opened || !m_cb.sendFrame)
        return false;
    m_cb.sendFrame(buildFrame(kProtoIp, ipPacket));
    return true;
}

void PppSession::close(const QString& reason, qint64 nowMs)
{
    if (m_state == State::Dead || m_state == State::Terminating)
        return;
    if (m_state == State::Initial)
    {
        die(reason);
        return;
    }
    m_state = State::Terminating;
    m_termReason = reason;
    m_termSentAt = nowMs;
    sendControl(kProtoLcp, TermReq, ++m_termId, reason.toUtf8().left(64));
}

void PppSession::die(const QString& reason)
{
    if (m_state == State::Dead)
        return;
    m_state = State::Dead;
    log(QStringLiteral("PPP: kết thúc - %1").arg(reason));
    if (m_cb.terminated)
        m_cb.terminated(reason);
}

// ---------------------------------------------------------------------------------------------------------

namespace FortinetFraming
{
QByteArray encapsulate(const QByteArray& pppFrame)
{
    QByteArray out;
    out.reserve(pppFrame.size() + 6);
    appendBe16(out, static_cast<quint16>(pppFrame.size() + 6));
    out.append(static_cast<char>(0x50));
    out.append(static_cast<char>(0x50));
    appendBe16(out, static_cast<quint16>(pppFrame.size()));
    out.append(pppFrame);
    return out;
}

Deframer::Result Deframer::next(QByteArray* frame, QString* error)
{
    if (m_buf.size() < 6)
    {
        // Máy chủ từ chối yêu cầu đường hầm bằng phản hồi HTTP thay vì khung PPP.
        if (m_buf.startsWith("HTTP/") && m_buf.contains('\n'))
        {
            if (error)
                *error = QString::fromLatin1(m_buf.left(m_buf.indexOf('\n'))).trimmed();
            return Result::Error;
        }
        return Result::NeedMore;
    }
    if (m_buf.startsWith("HTTP/"))
    {
        const int nl = m_buf.indexOf('\n');
        if (nl < 0)
            return Result::NeedMore;
        if (error)
            *error = QString::fromLatin1(m_buf.left(nl)).trimmed();
        return Result::Error;
    }
    const quint16 total = be16(m_buf, 0);
    const quint16 magic = be16(m_buf, 2);
    const quint16 len = be16(m_buf, 4);
    if (magic != 0x5050 || total != len + 6 || len == 0)
    {
        if (error)
            *error = QStringLiteral("Khung đường hầm Fortinet hỏng (đầu khung %1 %2 %3).")
                         .arg(total)
                         .arg(magic, 4, 16, QChar('0'))
                         .arg(len);
        return Result::Error;
    }
    if (m_buf.size() < total)
        return Result::NeedMore;
    if (frame)
        *frame = m_buf.mid(6, len);
    m_buf.remove(0, total);
    return Result::Frame;
}
} // namespace FortinetFraming
