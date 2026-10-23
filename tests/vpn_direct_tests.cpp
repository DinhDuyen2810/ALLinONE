// Kiểm thử kết nối VPN TRỰC TIẾP (FortiGate SSL-VPN / GlobalProtect) - KHÔNG chạm mạng thật, KHÔNG tạo adapter,
// KHÔNG đổi tuyến: lõi thuần + máy chủ FortiGate/GlobalProtect GIẢ trên 127.0.0.1 (HTTP/TCP trần, chế độ chỉ
// dành cho test của TunnelEngine) + tiến trình phụ thật "--vpn-helper" (không nâng quyền, chỉ thử kênh điều
// khiển). cmake --build build --target vpn_direct_tests && build\vpn_direct_tests.exe

#include "TestDataDir.h"

#include "core/AppPaths.h"
#include "tools/vpn/direct/CredentialStore.h"
#include "tools/vpn/direct/DirectVpnTypes.h"
#include "tools/vpn/direct/FortinetProtocol.h"
#include "tools/vpn/direct/GlobalProtectProtocol.h"
#include "tools/vpn/direct/GpEsp.h"
#include "tools/vpn/direct/PppSession.h"
#include "tools/vpn/direct/TunnelEngine.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QSet>
#include <QTcpServer>
#include <QUdpSocket>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>

#include <cstdio>
#include <functional>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

namespace
{
bool waitUntil(const std::function<bool()>& cond, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < timeoutMs)
    {
        QEventLoop loop;
        QTimer::singleShot(10, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return cond();
}

quint16 be16(const QByteArray& b, int p)
{
    return static_cast<quint16>((static_cast<quint8>(b[p]) << 8) | static_cast<quint8>(b[p + 1]));
}
quint32 be32(const QByteArray& b, int p)
{
    return (static_cast<quint32>(be16(b, p)) << 16) | be16(b, p + 2);
}
QByteArray u32(quint32 v)
{
    QByteArray b;
    b.append(char(v >> 24)).append(char(v >> 16)).append(char(v >> 8)).append(char(v));
    return b;
}
QByteArray opt(quint8 type, const QByteArray& v)
{
    QByteArray o;
    o.append(char(type)).append(char(v.size() + 2)).append(v);
    return o;
}

/// Gói IPv4 giả (đủ để nhận diện, không cần checksum đúng).
QByteArray fakeIpPacket(quint8 marker)
{
    QByteArray p(28, '\0');
    p[0] = 0x45;
    p[3] = 28;
    p[9] = 17;
    p[27] = static_cast<char>(marker);
    return p;
}

// ---- Máy chủ HTTP giả tối giản (nhiều yêu cầu trên một kết nối, có thể chuyển kết nối sang chế độ đường hầm) ----
struct HttpReq
{
    QByteArray method;
    QByteArray path;  // không gồm query
    QByteArray query;
    QHash<QByteArray, QByteArray> headers; // khóa chữ thường
    QByteArray body;
};

class FakeServer : public QObject
{
public:
    std::function<void(QTcpSocket*, const HttpReq&)> onRequest;
    std::function<void(QTcpSocket*, const QByteArray&)> onTunnelData;

    FakeServer()
    {
        server.listen(QHostAddress::LocalHost, 0);
        connect(&server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = server.nextPendingConnection())
                connect(s, &QTcpSocket::readyRead, this, [this, s]() { onRead(s); });
        });
    }
    quint16 port() const { return server.serverPort(); }
    void startTunnel(QTcpSocket* s) { tunnels.insert(s); }

    static void respond(QTcpSocket* s, int status, const QByteArray& body, const QList<QByteArray>& extraHeaders = {})
    {
        QByteArray r = "HTTP/1.1 " + QByteArray::number(status) + (status == 200 ? " OK" : " Error") + "\r\n";
        for (const QByteArray& h : extraHeaders)
            r += h + "\r\n";
        r += "Content-Type: text/plain\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: keep-alive\r\n\r\n";
        s->write(r + body);
    }

private:
    void onRead(QTcpSocket* s)
    {
        QByteArray& buf = bufs[s];
        buf += s->readAll();
        while (!tunnels.contains(s))
        {
            const int end = buf.indexOf("\r\n\r\n");
            if (end < 0)
                return;
            HttpReq req;
            const QList<QByteArray> lines = buf.left(end).split('\n');
            const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
            req.method = first.value(0);
            const QByteArray target = first.value(1);
            const int q = target.indexOf('?');
            req.path = q < 0 ? target : target.left(q);
            req.query = q < 0 ? QByteArray() : target.mid(q + 1);
            for (int i = 1; i < lines.size(); ++i)
            {
                const int c = lines[i].indexOf(':');
                if (c > 0)
                    req.headers.insert(lines[i].left(c).trimmed().toLower(), lines[i].mid(c + 1).trimmed());
            }
            const int len = req.headers.value("content-length").toInt();
            if (buf.size() < end + 4 + len)
                return;
            req.body = buf.mid(end + 4, len);
            buf.remove(0, end + 4 + len);
            onRequest(s, req);
        }
        if (!buf.isEmpty() && onTunnelData)
        {
            const QByteArray data = buf;
            buf.clear();
            onTunnelData(s, data);
        }
    }

    QTcpServer server;
    QHash<QTcpSocket*, QByteArray> bufs;
    QSet<QTcpSocket*> tunnels;
};

/// Phía MÁY CHỦ của PPP FortiGate (theo kịch bản): Ack LCP của client, gửi ConfReq LCP riêng; Nak IPCP địa
/// chỉ 0.0.0.0 bằng địa chỉ cấp; trả DNS nếu client xin; Echo-Reply; thu gói IP client gửi.
struct FakeFortiPpp
{
    QTcpSocket* sock{nullptr};
    FortinetFraming::Deframer deframer;
    bool sentLcpReq{false}, sentIpcpReq{false}, lcpAcked{false}, ipcpAcked{false};
    quint32 assigned{0x0AD486C8}; // 10.212.134.200
    QList<QByteArray> ipFromClient;
    int protRejects{0};

    void send(quint16 proto, const QByteArray& payload)
    {
        sock->write(FortinetFraming::encapsulate(PppSession::buildFrame(proto, payload)));
    }
    void feed(const QByteArray& data)
    {
        deframer.feed(data);
        QByteArray frame;
        QString err;
        while (deframer.next(&frame, &err) == FortinetFraming::Deframer::Result::Frame)
        {
            quint16 proto = 0;
            QByteArray p;
            if (!PppSession::parseFrame(frame, &proto, &p))
                continue;
            if (proto == PppSession::kProtoIp)
            {
                ipFromClient << p;
                continue;
            }
            const quint8 code = static_cast<quint8>(p[0]);
            const quint8 id = static_cast<quint8>(p[1]);
            const QByteArray d = p.mid(4, be16(p, 2) - 4);
            if (proto == PppSession::kProtoLcp)
            {
                if (code == PppSession::ConfReq)
                {
                    send(proto, PppSession::buildControl(PppSession::ConfAck, id, d));
                    if (!sentLcpReq)
                    {
                        sentLcpReq = true;
                        send(proto, PppSession::buildControl(PppSession::ConfReq, 1,
                                                             opt(1, QByteArray::fromHex("054a")) + opt(5, u32(0x11223344))));
                    }
                }
                else if (code == PppSession::ConfAck)
                    lcpAcked = true;
                else if (code == PppSession::EchoReq)
                    send(proto, PppSession::buildControl(PppSession::EchoRep, id, u32(0x11223344)));
                else if (code == PppSession::ProtRej)
                    ++protRejects;
            }
            else if (proto == PppSession::kProtoIpcp)
            {
                if (code == PppSession::ConfReq)
                {
                    // Đọc địa chỉ client xin; 0.0.0.0 -> Nak kèm địa chỉ cấp (+ DNS nếu client xin).
                    quint32 addr = 0;
                    bool wantsDns = false;
                    for (int i = 0; i + 2 <= d.size(); i += static_cast<quint8>(d[i + 1]))
                    {
                        if (static_cast<quint8>(d[i]) == 3)
                            addr = be32(d, i + 2);
                        if (static_cast<quint8>(d[i]) == 129 && be32(d, i + 2) == 0)
                            wantsDns = true;
                        if (static_cast<quint8>(d[i + 1]) < 2)
                            break;
                    }
                    if (addr != assigned || wantsDns)
                    {
                        QByteArray nak = opt(3, u32(assigned));
                        if (wantsDns)
                            nak += opt(129, u32(0x0A000035)); // 10.0.0.53
                        send(proto, PppSession::buildControl(PppSession::ConfNak, id, nak));
                    }
                    else
                        send(proto, PppSession::buildControl(PppSession::ConfAck, id, d));
                    if (!sentIpcpReq)
                    {
                        sentIpcpReq = true;
                        send(proto, PppSession::buildControl(PppSession::ConfReq, 1, opt(3, u32(0x0AD48601))));
                    }
                }
                else if (code == PppSession::ConfAck)
                    ipcpAcked = true;
            }
        }
    }
};
} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    TestDataDir dataDir; // thư mục dữ liệu tạm - không đụng dữ liệu thật của người dùng
    AppPaths::setDataDirOverride(dataDir.path());

    // ================= DirectVpnTypes =================
    {
        quint32 v = 0;
        CHECK(DirectVpn::parseIpv4("10.212.134.200", &v) && v == 0x0AD486C8);
        CHECK(DirectVpn::parseIpv4(" 1.2.3.4 ", &v) && v == 0x01020304);
        CHECK(!DirectVpn::parseIpv4("1.2.3", nullptr));
        CHECK(!DirectVpn::parseIpv4("256.1.1.1", nullptr));
        CHECK(!DirectVpn::parseIpv4("01.2.3.4", nullptr)); // số 0 thừa (bát phân) bị chặn
        CHECK(!DirectVpn::parseIpv4("1.2.3.4x", nullptr));
        CHECK(!DirectVpn::parseIpv4("1.2.3.-4", nullptr));
        CHECK(DirectVpn::ipv4ToString(0xC0A80101) == "192.168.1.1");
        // Dải riêng RFC 1918 (DNS nội bộ cần tuyến qua VPN khi chia tuyến - lỗi thật v1.19.33: 10.45.1.20 đi Wi-Fi).
        CHECK(DirectVpn::isPrivateIpv4(0x0A2D0114));    // 10.45.1.20
        CHECK(DirectVpn::isPrivateIpv4(0xAC1F0001));    // 172.31.0.1
        CHECK(!DirectVpn::isPrivateIpv4(0xAC200001));   // 172.32.0.1 - ngoài 172.16/12
        CHECK(DirectVpn::isPrivateIpv4(0xC0A80001));    // 192.168.0.1
        CHECK(!DirectVpn::isPrivateIpv4(0x08080808));   // 8.8.8.8

        CHECK(DirectVpn::maskToPrefix("255.255.255.0") == 24);
        CHECK(DirectVpn::maskToPrefix("255.255.255.255") == 32);
        CHECK(DirectVpn::maskToPrefix("0.0.0.0") == 0);
        CHECK(DirectVpn::maskToPrefix("255.0.255.0") == -1);

        IpRoute r;
        CHECK(DirectVpn::parseRoute("10.1.2.3/8", &r) && r.network == 0x0A000000 && r.prefix == 8); // chuẩn hóa
        CHECK(DirectVpn::parseRoute("10.0.0.0/255.0.0.0", &r) && r.prefix == 8);
        CHECK(DirectVpn::parseRoute("1.2.3.4", &r) && r.prefix == 32 && r.network == 0x01020304);
        CHECK(DirectVpn::parseRoute("0.0.0.0/0", &r) && r.prefix == 0 && r.network == 0);
        CHECK(!DirectVpn::parseRoute("1.2.3.4/33", nullptr));
        CHECK(!DirectVpn::parseRoute("x/8", nullptr));
        CHECK(DirectVpn::parseRoute("10.11.1.7", "255.255.255.0", &r) && r.network == 0x0A0B0100 && r.prefix == 24);
        CHECK(DirectVpn::routeToString(IpRoute{0x0A0B0100, 24}) == "10.11.1.0/24");

        QString host;
        int port = 0;
        CHECK(DirectVpn::splitHostPort("103.221.228.37:10443", 443, &host, &port) && host == "103.221.228.37" && port == 10443);
        CHECK(DirectVpn::splitHostPort("https://VPN.Cty.vn/global-protect", 443, &host, &port) && host == "vpn.cty.vn" && port == 443);
        CHECK(DirectVpn::splitHostPort("171.226.12.171", 443, &host, &port) && port == 443);
        CHECK(!DirectVpn::splitHostPort("vpn.cty.vn:0", 443, nullptr, nullptr));
        CHECK(!DirectVpn::splitHostPort("vpn.cty.vn:99999", 443, nullptr, nullptr));
        CHECK(!DirectVpn::splitHostPort("a b", 443, nullptr, nullptr));
        CHECK(!DirectVpn::splitHostPort("evil.com\r\nHost: x", 443, nullptr, nullptr)); // chặn chèn header
        CHECK(!DirectVpn::splitHostPort("", 443, nullptr, nullptr));
    }

    // ================= CredentialStore (Windows Credential Manager) - tiền tố RIÊNG cho test, luôn xóa sau =================
    {
        CredentialStore::setTargetPrefixForTest(QStringLiteral("OneForAll-TEST-") + QString::number(QCoreApplication::applicationPid()) + "/");
        const QString server = QStringLiteral("vpn-test.invalid:10443");
        CredentialStore::remove(server);
        CHECK(!CredentialStore::has(server));
        QString err;
        CHECK(CredentialStore::save(server, QStringLiteral("alice"), QString::fromUtf8("m\xe1\xba\xadt kh\xe1\xba\xa9u & ký tự"), &err));
        QString u, p;
        CHECK(CredentialStore::load(server, &u, &p) && u == "alice" && p == QString::fromUtf8("m\xe1\xba\xadt kh\xe1\xba\xa9u & ký tự"));
        CHECK(CredentialStore::has(QStringLiteral("VPN-TEST.invalid:10443"))); // khóa không phân biệt hoa/thường
        CHECK(CredentialStore::save(server, QStringLiteral("alice"), QStringLiteral("moi"), &err)); // ghi đè
        CHECK(CredentialStore::load(server, &u, &p) && p == "moi");
        CHECK(!CredentialStore::save(QString(), QStringLiteral("x"), QStringLiteral("y"), &err));
        CredentialStore::remove(server);
        CHECK(!CredentialStore::has(server) && !CredentialStore::load(server, &u, &p));
        CredentialStore::setTargetPrefixForTest(QStringLiteral("OneForAll/VPN/"));
    }

    // ================= PppSession với máy chủ PPP kịch bản =================
    {
        QList<QByteArray> sent;
        QList<QByteArray> ipIn;
        bool opened = false;
        QString terminated;
        PppSession::Callbacks cb;
        cb.sendFrame = [&](const QByteArray& f) { sent << f; };
        cb.ipReceived = [&](const QByteArray& ip) { ipIn << ip; };
        cb.opened = [&]() { opened = true; };
        cb.terminated = [&](const QString& r) { terminated = r; };
        PppSession ppp(cb);
        ppp.setRequestDns(true);
        qint64 t = 1000;

        auto lastControl = [&](quint16* proto = nullptr) {
            quint16 p = 0;
            QByteArray payload;
            PppSession::parseFrame(sent.last(), &p, &payload);
            if (proto)
                *proto = p;
            return payload;
        };
        auto serverFrame = [&](quint16 proto, quint8 code, quint8 id, const QByteArray& data) {
            ppp.handleFrame(PppSession::buildFrame(proto, PppSession::buildControl(code, id, data)), t);
        };

        ppp.start(t);
        CHECK(sent.size() == 1);
        quint16 proto = 0;
        QByteArray req = lastControl(&proto);
        CHECK(proto == PppSession::kProtoLcp && static_cast<quint8>(req[0]) == PppSession::ConfReq);
        CHECK(req.mid(4, 4) == QByteArray::fromHex("0104054a")); // MRU 1354
        CHECK(static_cast<quint8>(req[8]) == 5 && static_cast<quint8>(req[9]) == 6); // Magic
        CHECK(!sent.first().isEmpty() && static_cast<quint8>(sent.first()[0]) == 0xFF); // FF 03 không nén
        const quint8 lcpId = static_cast<quint8>(req[1]);

        // Máy chủ đòi xác thực PAP (tùy chọn 3) -> ta phải Configure-Reject đúng tùy chọn đó.
        serverFrame(PppSession::kProtoLcp, PppSession::ConfReq, 7,
                    opt(1, QByteArray::fromHex("054a")) + opt(3, QByteArray::fromHex("c023")));
        QByteArray rej = lastControl();
        CHECK(static_cast<quint8>(rej[0]) == PppSession::ConfRej && static_cast<quint8>(rej[1]) == 7);
        CHECK(rej.mid(4) == opt(3, QByteArray::fromHex("c023")));
        // Yêu cầu lại không có xác thực -> Ack.
        serverFrame(PppSession::kProtoLcp, PppSession::ConfReq, 8, opt(1, QByteArray::fromHex("0578")) + opt(5, u32(0xCAFEBABE)));
        QByteArray ack = lastControl();
        CHECK(static_cast<quint8>(ack[0]) == PppSession::ConfAck && static_cast<quint8>(ack[1]) == 8);
        CHECK(!opened);

        // Retransmit: chưa được Ack sau 3 giây -> gửi lại ConfReq.
        const int before = sent.size();
        t += 3100;
        ppp.tick(t);
        CHECK(sent.size() == before + 1);
        QByteArray req2 = lastControl(&proto);
        CHECK(proto == PppSession::kProtoLcp && static_cast<quint8>(req2[0]) == PppSession::ConfReq);
        // Ack cho yêu cầu CŨ bị bỏ qua (id lệch), Ack đúng id mới -> LCP mở, gửi IPCP ConfReq.
        serverFrame(PppSession::kProtoLcp, PppSession::ConfAck, lcpId, req.mid(4));
        CHECK(lastControl(&proto) == req2.left(req2.size()) && proto == PppSession::kProtoLcp);
        serverFrame(PppSession::kProtoLcp, PppSession::ConfAck, static_cast<quint8>(req2[1]), req2.mid(4));
        QByteArray ipcpReq = lastControl(&proto);
        CHECK(proto == PppSession::kProtoIpcp && static_cast<quint8>(ipcpReq[0]) == PppSession::ConfReq);
        CHECK(ipcpReq.mid(4, 6) == opt(3, u32(0)));          // xin địa chỉ 0.0.0.0 = để máy chủ cấp
        CHECK(ipcpReq.contains(opt(129, u32(0))));           // xin DNS
        CHECK(ppp.peerMru() == 0x578);

        // Máy chủ gửi IPCP ConfReq (địa chỉ đầu bên kia) -> Ack; Nak địa chỉ + DNS -> ta gửi lại với giá trị đó.
        serverFrame(PppSession::kProtoIpcp, PppSession::ConfReq, 3, opt(3, u32(0x0AD48601)));
        CHECK(static_cast<quint8>(lastControl()[0]) == PppSession::ConfAck);
        serverFrame(PppSession::kProtoIpcp, PppSession::ConfNak, static_cast<quint8>(ipcpReq[1]),
                    opt(3, u32(0x0AD486C8)) + opt(129, u32(0x0A000035)) + opt(131, u32(0x0A000036)));
        QByteArray ipcpReq2 = lastControl(&proto);
        CHECK(proto == PppSession::kProtoIpcp && ipcpReq2.contains(opt(3, u32(0x0AD486C8))));
        CHECK(ipcpReq2.contains(opt(129, u32(0x0A000035))) && ipcpReq2.contains(opt(131, u32(0x0A000036))));
        CHECK(!opened);
        serverFrame(PppSession::kProtoIpcp, PppSession::ConfAck, static_cast<quint8>(ipcpReq2[1]), ipcpReq2.mid(4));
        CHECK(opened && ppp.isOpened());
        CHECK(ppp.localIpv4() == 0x0AD486C8);
        CHECK(ppp.dnsServers() == (QList<quint32>{0x0A000035, 0x0A000036}));

        // Gói IP hai chiều (cả khung đầy đủ lẫn khung nén PFC/ACFC).
        const QByteArray ip = fakeIpPacket(0x42);
        ppp.handleFrame(PppSession::buildFrame(PppSession::kProtoIp, ip), t);
        ppp.handleFrame(QByteArray(1, 0x21) + ip, t); // không FF 03, giao thức 1 byte
        CHECK(ipIn.size() == 2 && ipIn[0] == ip && ipIn[1] == ip);
        CHECK(ppp.sendIp(ip));
        CHECK(sent.last() == PppSession::buildFrame(PppSession::kProtoIp, ip));

        // Echo-Request -> Echo-Reply kèm magic của ta.
        serverFrame(PppSession::kProtoLcp, PppSession::EchoReq, 44, u32(0xCAFEBABE));
        QByteArray echo = lastControl();
        CHECK(static_cast<quint8>(echo[0]) == PppSession::EchoRep && static_cast<quint8>(echo[1]) == 44 && echo.size() == 8);

        // Giao thức không hỗ trợ (IPv6CP) -> LCP Protocol-Reject nêu đúng giao thức.
        ppp.handleFrame(PppSession::buildFrame(0x8057, PppSession::buildControl(1, 1, opt(1, QByteArray(8, '\1')))), t);
        QByteArray prej = lastControl(&proto);
        CHECK(proto == PppSession::kProtoLcp && static_cast<quint8>(prej[0]) == PppSession::ProtRej && be16(prej, 4) == 0x8057);

        // Im lặng lâu -> Echo-Request; quá 4 lần không trả lời -> kết thúc với lý do mất kết nối.
        for (int i = 0; i < 6 && terminated.isEmpty(); ++i)
        {
            t += 16000;
            ppp.tick(t);
        }
        CHECK(!terminated.isEmpty() && ppp.isDead());
        std::printf("PPP mat ket noi -> '%s'\n", qPrintable(terminated));
    }
    {
        // Máy chủ Terminate-Request -> Terminate-Ack + terminated(); close() chủ động -> TermReq rồi kết thúc khi Ack.
        QList<QByteArray> sent;
        QString term;
        PppSession::Callbacks cb;
        cb.sendFrame = [&](const QByteArray& f) { sent << f; };
        cb.terminated = [&](const QString& r) { term = r; };
        PppSession ppp(cb);
        ppp.start(0);
        ppp.handleFrame(PppSession::buildFrame(PppSession::kProtoLcp, PppSession::buildControl(PppSession::TermReq, 9, "idle timeout")), 10);
        quint16 proto = 0;
        QByteArray p;
        PppSession::parseFrame(sent.last(), &proto, &p);
        CHECK(static_cast<quint8>(p[0]) == PppSession::TermAck && static_cast<quint8>(p[1]) == 9);
        CHECK(term.contains("idle timeout"));

        QString term2;
        PppSession::Callbacks cb2;
        QList<QByteArray> sent2;
        cb2.sendFrame = [&](const QByteArray& f) { sent2 << f; };
        cb2.terminated = [&](const QString& r) { term2 = r; };
        PppSession ppp2(cb2);
        ppp2.start(0);
        ppp2.close("bye", 5);
        PppSession::parseFrame(sent2.last(), &proto, &p);
        CHECK(static_cast<quint8>(p[0]) == PppSession::TermReq);
        ppp2.handleFrame(PppSession::buildFrame(PppSession::kProtoLcp, PppSession::buildControl(PppSession::TermAck, static_cast<quint8>(p[1]), {})), 6);
        CHECK(term2 == "bye");

        // Không ai trả lời LCP -> hết lượt gửi lại -> kết thúc (không treo mãi).
        QString term3;
        PppSession::Callbacks cb3;
        cb3.sendFrame = [](const QByteArray&) {};
        cb3.terminated = [&](const QString& r) { term3 = r; };
        PppSession ppp3(cb3);
        ppp3.start(0);
        for (qint64 t = 0; t < 60000 && term3.isEmpty(); t += 1000)
            ppp3.tick(t);
        CHECK(!term3.isEmpty());

        // Khung hỏng/rỗng không làm sập.
        CHECK(!PppSession::parseFrame(QByteArray(), nullptr, nullptr));
        CHECK(!PppSession::parseFrame(QByteArray::fromHex("ff03"), nullptr, nullptr));
        ppp3.handleFrame(QByteArray::fromHex("ff03c021"), 1);
        ppp3.handleFrame(QByteArray::fromHex("ff03c02101"), 1);
    }

    // ================= Khung Fortinet =================
    {
        const QByteArray frame = PppSession::buildFrame(PppSession::kProtoIp, fakeIpPacket(1));
        const QByteArray enc = FortinetFraming::encapsulate(frame);
        CHECK(be16(enc, 0) == frame.size() + 6 && be16(enc, 2) == 0x5050 && be16(enc, 4) == frame.size());

        FortinetFraming::Deframer d;
        QByteArray out;
        QString err;
        const QByteArray two = enc + FortinetFraming::encapsulate(PppSession::buildFrame(PppSession::kProtoIp, fakeIpPacket(2)));
        int got = 0;
        for (char c : two) // từng byte một
        {
            d.feed(QByteArray(1, c));
            while (d.next(&out, &err) == FortinetFraming::Deframer::Result::Frame)
                ++got;
        }
        CHECK(got == 2 && out.endsWith(char(2)));

        FortinetFraming::Deframer http;
        http.feed("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n");
        CHECK(http.next(&out, &err) == FortinetFraming::Deframer::Result::Error && err.contains("403"));
        FortinetFraming::Deframer bad;
        bad.feed(QByteArray::fromHex("000a12340004ff03c021"));
        CHECK(bad.next(&out, &err) == FortinetFraming::Deframer::Result::Error);
    }

    // ================= Giao thức Fortinet (thuần) =================
    {
        const QByteArray body = FortinetProtocol::loginBody("alice", "p@ss&w=rd", "");
        CHECK(body == "username=alice&credential=p%40ss%26w%3Drd&realm=&ajax=1&just_logged_in=1");

        using Kind = FortinetProtocol::LoginOutcome::Kind;
        CHECK(FortinetProtocol::interpretLogin(200, "ret=1,redir=/remote/index", true).kind == Kind::Success);
        CHECK(FortinetProtocol::interpretLogin(405, "", false).kind == Kind::BadCredentials);
        const auto tok = FortinetProtocol::interpretLogin(
            200, "ret=2,reqid=777,polid=1-1-1,grp=VPN Users,portal=full,peer=alice,magic=1-abcdef,tokeninfo=,chal_msg=Nhap ma OTP", false);
        CHECK(tok.kind == Kind::NeedToken && !tok.ftmPush);
        CHECK(tok.tokenFields == "reqid=777&polid=1-1-1&grp=VPN%20Users&portal=full&peer=alice&magic=1-abcdef");
        CHECK(tok.message == "Nhap ma OTP");
        const auto push = FortinetProtocol::interpretLogin(200, "ret=2,reqid=1,magic=1-x,tokeninfo=ftm_push,chal_msg=x", false);
        CHECK(push.kind == Kind::NeedToken && push.ftmPush);
        CHECK(FortinetProtocol::interpretLogin(200, "ret=0,redir=/remote/login?err=sslvpn_login_permission_denied", false).kind ==
              Kind::BadCredentials);
        CHECK(FortinetProtocol::interpretLogin(401, "<html>", false).kind == Kind::Failed);
        CHECK(FortinetProtocol::interpretLogin(0, "", false).kind == Kind::Failed);

        CHECK(FortinetProtocol::tokenBody("alice", "123456", "", tok.tokenFields, false) ==
              "username=alice&code=123456&realm=&reqid=777&polid=1-1-1&grp=VPN%20Users&portal=full&peer=alice&magic=1-abcdef");
        // FortiToken push: để trống mã -> bỏ "magic", thêm ftmpush=1.
        const QByteArray pushBody = FortinetProtocol::tokenBody("alice", "", "", "reqid=1&magic=1-x", true);
        CHECK(!pushBody.contains("magic") && pushBody.endsWith("&ftmpush=1"));

        // Mẫu XML cấu hình (theo tài liệu OpenConnect fortinet.c).
        const QByteArray xml = R"(<?xml version="1.0" encoding="utf-8"?>
<sslvpn-tunnel ver="2" dtls="1" patch="1">
  <dtls-config heartbeat-interval="10" heartbeat-fail-count="10" heartbeat-idle-timeout="10" client-hello-timeout="10"/>
  <tunnel-method value="ppp"/>
  <fos platform="FG100E" major="5" minor="06" patch="6" build="1630" branch="1630"/>
  <auth-ses check-src-ip='1' tun-connect-without-reauth='1' tun-user-ses-timeout='240' />
  <ipv4>
    <dns ip="1.1.1.1"/>
    <dns ip="8.8.8.8" domain="foo.com"/>
    <assigned-addr ipv4="172.16.1.1"/>
    <split-tunnel-info>
      <addr ip="10.11.10.10" mask="255.255.255.255"/>
      <addr ip="10.11.1.0" mask="255.255.255.0"/>
    </split-tunnel-info>
    <split-tunnel-info negate="1">
      <addr ip="1.2.3.4" mask="255.255.255.255"/>
    </split-tunnel-info>
  </ipv4>
  <idle-timeout val="3600"/>
</sslvpn-tunnel>)";
        TunnelConfig cfg;
        QString err;
        CHECK(FortinetProtocol::parseConfigXml(xml, &cfg, &err));
        CHECK(cfg.ipv4 == "172.16.1.1");
        CHECK(cfg.dns == (QStringList{"1.1.1.1", "8.8.8.8"}) && cfg.dnsSuffix == QStringList{"foo.com"});
        CHECK(cfg.includeRoutes.size() == 2 && cfg.includeRoutes[0] == (IpRoute{0x0A0B0A0A, 32}) &&
              cfg.includeRoutes[1] == (IpRoute{0x0A0B0100, 24}));
        CHECK(cfg.excludeRoutes.size() == 1 && cfg.excludeRoutes[0] == (IpRoute{0x01020304, 32}));
        CHECK(!cfg.fullTunnel());
        // Không có split-tunnel -> toàn bộ lưu lượng; giá trị rác bị bỏ qua.
        TunnelConfig full;
        CHECK(FortinetProtocol::parseConfigXml(
            "<sslvpn-tunnel><ipv4><assigned-addr ipv4='10.0.0.9'/><dns ip='bad'/><dns ip='9.9.9.9'/></ipv4></sslvpn-tunnel>", &full, &err));
        CHECK(full.fullTunnel() && full.dns == QStringList{"9.9.9.9"});
        CHECK(!FortinetProtocol::parseConfigXml("<html>login</html>", &full, &err) && !err.isEmpty());
        CHECK(!FortinetProtocol::parseConfigXml("", &full, &err));

        const QByteArray treq = FortinetProtocol::tunnelRequest("1.2.3.4:10443", "abc");
        CHECK(treq.startsWith("GET /remote/sslvpn-tunnel HTTP/1.1\r\n") && treq.contains("Cookie: SVPNCOOKIE=abc\r\n") &&
              treq.endsWith("\r\n\r\n"));
    }

    // ================= Giao thức GlobalProtect (thuần) =================
    {
        // Phản hồi prelogin THẬT của portal 171.226.12.171 (lấy trên máy người dùng, không gửi thông tin đăng nhập).
        const QByteArray realPrelogin = R"(<?xml version="1.0" encoding="UTF-8" ?>
<prelogin-response>
<status>Success</status>
<ccusername></ccusername>
<autosubmit>false</autosubmit>
<msg></msg>
<newmsg></newmsg>
<authentication-message>Enter login credentials</authentication-message>
<username-label>Username</username-label>
<password-label>Password</password-label>
<panos-version>1</panos-version>
<saml-default-browser>yes</saml-default-browser>

<server-ip>171.226.12.171</server-ip><auth-api>no</auth-api><default-browser>no</default-browser><region>VN</region>
</prelogin-response>)";
        GlobalProtectProtocol::Prelogin pre;
        QString err;
        CHECK(GlobalProtectProtocol::parsePrelogin(realPrelogin, &pre, &err));
        CHECK(!pre.saml && pre.userLabel == "Username" && pre.message == "Enter login credentials");
        CHECK(GlobalProtectProtocol::parsePrelogin(
                  "<prelogin-response><status>Success</status><saml-auth-method>REDIRECT</saml-auth-method>"
                  "<saml-request>aHR0cA==</saml-request></prelogin-response>", &pre, &err) && pre.saml);
        CHECK(!GlobalProtectProtocol::parsePrelogin("<prelogin-response><status>Error</status><msg>Valid client certificate is required</msg></prelogin-response>",
                                                    &pre, &err) && err.contains("certificate"));
        CHECK(!GlobalProtectProtocol::parsePrelogin("<html/>", &pre, &err));

        GlobalProtectProtocol::LoginParams lp;
        lp.server = "171.226.12.171";
        lp.computer = "PC-01";
        lp.username = "alice";
        lp.password = "p&ss";
        const QByteArray lb = GlobalProtectProtocol::loginBody(lp);
        CHECK(lb.startsWith("jnlpReady=jnlpReady&ok=Login&direct=yes&clientVer=4100&prot=https:"));
        CHECK(lb.contains("&user=alice") && lb.contains("&passwd=p%26ss") && lb.contains("&server=171.226.12.171") &&
              !lb.contains("portal-userauthcookie"));
        lp.portalUserAuthCookie = "cookie/+=";
        lp.inputStr = "abc123";
        const QByteArray lb2 = GlobalProtectProtocol::loginBody(lp);
        CHECK(lb2.contains("&portal-userauthcookie=cookie%2F%2B%3D") && lb2.contains("&inputStr=abc123"));

        const QByteArray portalXml = R"(<?xml version="1.0" encoding="UTF-8" ?>
<policy>
  <portal-name>GP-Portal</portal-name>
  <portal-config-version>4100</portal-config-version>
  <version>6.3.3-915</version>
  <gateways>
    <cutoff-time>5</cutoff-time>
    <external>
      <list>
        <entry name="gw1.cty.vn:4443"><description>HN</description></entry>
        <entry name="171.226.12.172"><description>HCM</description></entry>
      </list>
    </external>
  </gateways>
  <portal-userauthcookie>XyZ123==</portal-userauthcookie>
  <portal-prelogonuserauthcookie>empty</portal-prelogonuserauthcookie>
</policy>)";
        GlobalProtectProtocol::PortalConfig pc;
        CHECK(GlobalProtectProtocol::parsePortalConfig(portalXml, &pc, &err));
        CHECK(pc.gateways == (QStringList{"gw1.cty.vn:4443", "171.226.12.172"})); // <gateways> không bị "nuốt"
        CHECK(pc.userAuthCookie == "XyZ123==" && pc.prelogonCookie.isEmpty() && pc.version == "6.3.3-915");
        CHECK(!GlobalProtectProtocol::parsePortalConfig("<policy><version>1</version></policy>", &pc, &err)); // không gateway
        CHECK(!GlobalProtectProtocol::parsePortalConfig("garbage", &pc, &err));

        const QByteArray jnlp = R"(<?xml version="1.0" encoding="utf-8"?>
<jnlp><application-desc>
<argument>(null)</argument><argument>a1b2c3d4e5f6</argument><argument>persist</argument><argument>GP-Portal</argument>
<argument>alice</argument><argument>LDAP-auth</argument><argument>vsys1</argument><argument>%28empty_domain%29</argument>
<argument>(null)</argument><argument></argument><argument></argument><argument></argument>
<argument>tunnel</argument><argument>-1</argument><argument>4100</argument><argument>10.200.0.9</argument>
</application-desc></jnlp>)";
        GlobalProtectProtocol::GatewayAuth ga;
        CHECK(GlobalProtectProtocol::parseGatewayLogin(jnlp, &ga, &err));
        CHECK(ga.authcookie == "a1b2c3d4e5f6" && ga.portal == "GP-Portal" && ga.user == "alice");
        CHECK(ga.domain == "(empty_domain)" && ga.preferredIp == "10.200.0.9");
        ga.computer = "PC-01";
        CHECK(ga.cookieQuery() ==
              "authcookie=a1b2c3d4e5f6&portal=GP-Portal&user=alice&domain=%28empty_domain%29&preferred-ip=10.200.0.9&computer=PC-01");
        CHECK(!GlobalProtectProtocol::parseGatewayLogin("<jnlp><application-desc><argument>x</argument></application-desc></jnlp>", &ga, &err));
        QByteArray notTunnel = jnlp;
        notTunnel.replace("<argument>tunnel</argument>", "<argument>ipsec-only</argument>");
        CHECK(!GlobalProtectProtocol::parseGatewayLogin(notTunnel, &ga, &err));

        using RK = GlobalProtectProtocol::ServerReply::Kind;
        const auto ch = GlobalProtectProtocol::classify(
            "var respStatus = \"Challenge\";\nvar respMsg = \"Enter \\\"OTP\\\" code\";\nthisForm.inputStr.value = \"5f3a9\";\n");
        CHECK(ch.kind == RK::Challenge && ch.message == "Enter \"OTP\" code" && ch.inputStr == "5f3a9");
        const auto jsErr = GlobalProtectProtocol::classify("<html><script>var respStatus = \"Error\"; var respMsg = \"Authentication failed\";</script></html>");
        CHECK(jsErr.kind == RK::Error && jsErr.message == "Authentication failed");
        const auto xmlErr = GlobalProtectProtocol::classify("<response status=\"error\"><error>Invalid username or password</error></response>");
        CHECK(xmlErr.kind == RK::Error && xmlErr.message == "Invalid username or password");
        CHECK(GlobalProtectProtocol::classify(portalXml).kind == RK::Xml);
        CHECK(GlobalProtectProtocol::classify("errors getting SSL/VPN config").kind == RK::Error);

        const QByteArray gwConfig = R"(<?xml version="1.0" encoding="UTF-8" ?>
<response status="success">
  <need-tunnel>yes</need-tunnel>
  <ssl-tunnel-url>/ssl-tunnel-connect.sslvpn</ssl-tunnel-url>
  <portal>GP-Portal</portal>
  <user>alice</user>
  <ip-address>10.200.0.9</ip-address>
  <netmask>255.255.255.255</netmask>
  <mtu>0</mtu>
  <dns><member>10.0.0.10</member><member>10.0.0.11</member></dns>
  <dns-suffix><member>cty.local</member></dns-suffix>
  <access-routes><member>0.0.0.0/0</member></access-routes>
  <ipsec><udp-port>4501</udp-port><enc-algo>aes-128-cbc</enc-algo></ipsec>
</response>)";
        TunnelConfig gc;
        QString path;
        CHECK(GlobalProtectProtocol::parseGatewayConfig(gwConfig, &gc, &path, &err));
        CHECK(gc.ipv4 == "10.200.0.9" && gc.prefixLength == 32 && gc.fullTunnel());
        CHECK(gc.dns == (QStringList{"10.0.0.10", "10.0.0.11"}) && gc.dnsSuffix == QStringList{"cty.local"});
        CHECK(path == "/ssl-tunnel-connect.sslvpn");
        QByteArray split = gwConfig;
        split.replace("<member>0.0.0.0/0</member>", "<member>10.0.0.0/8</member><member>172.16.5.0/24</member>");
        CHECK(GlobalProtectProtocol::parseGatewayConfig(split, &gc, &path, &err) && gc.includeRoutes.size() == 2 && !gc.fullTunnel());
        CHECK(!GlobalProtectProtocol::parseGatewayConfig("<response status=\"success\"></response>", &gc, &path, &err)); // thiếu IP

        CHECK(GlobalProtectProtocol::tunnelRequest("/ssl-tunnel-connect.sslvpn", ga) ==
              "GET /ssl-tunnel-connect.sslvpn?authcookie=a1b2c3d4e5f6&user=alice HTTP/1.1\r\n\r\n");

        // HIP (Host Information Profile): md5 ổn định (không phụ thuộc authcookie/preferred-ip); check/report.
        const QString md5 = GlobalProtectProtocol::hipMd5(ga);
        CHECK(md5.size() == 32);
        GlobalProtectProtocol::GatewayAuth ga2 = ga;
        ga2.authcookie = "KHAC";
        ga2.preferredIp = "10.9.9.9";
        CHECK(GlobalProtectProtocol::hipMd5(ga2) == md5); // đổi authcookie/preferred-ip -> md5 giữ nguyên
        ga2.user = "bob";
        CHECK(GlobalProtectProtocol::hipMd5(ga2) != md5); // đổi user -> md5 đổi
        const QByteArray hcb = GlobalProtectProtocol::hipCheckBody(ga, "10.200.0.9", md5);
        CHECK(hcb.startsWith("client-role=global-protect-full&authcookie=a1b2c3d4e5f6") &&
              hcb.contains("&client-ip=10.200.0.9") && hcb.contains("&md5=" + md5.toLatin1()));
        CHECK(GlobalProtectProtocol::hipReportNeeded("<response><hip-report-needed>yes</hip-report-needed></response>"));
        CHECK(!GlobalProtectProtocol::hipReportNeeded("<response><hip-report-needed>no</hip-report-needed></response>"));
        CHECK(!GlobalProtectProtocol::hipReportNeeded("<response></response>")); // không rõ -> không nộp
        const QString hipXml = GlobalProtectProtocol::buildHipReportXml(ga, "10.200.0.9", md5, "6.3.3-915");
        CHECK(hipXml.contains("<md5-sum>" + md5 + "</md5-sum>") && hipXml.contains("<user-name>alice</user-name>") &&
              hipXml.contains("<ip-address>10.200.0.9</ip-address>") && hipXml.contains("<host-name>PC-01</host-name>"));
        CHECK(hipXml.startsWith("<hip-report") && hipXml.trimmed().endsWith("</hip-report>"));
        const QByteArray hrb = GlobalProtectProtocol::hipReportBody(ga, "10.200.0.9", hipXml.toUtf8());
        CHECK(hrb.contains("client-role=global-protect-full&") && hrb.contains("&report=%3Chip-report"));

        // ESP config: khối <ipsec> + <gw-address> (khóa/SPI gateway cấp sẵn).
        const QByteArray espXml = R"(<response status="success">
<ip-address>10.0.20.9</ip-address><netmask>255.255.255.255</netmask>
<access-routes><member>10.0.0.0/8</member></access-routes>
<gw-address>10.0.20.1</gw-address>
<ipsec>
  <udp-port>4501</udp-port><enc-algo>aes-128-cbc</enc-algo><hmac-algo>sha1</hmac-algo>
  <c2s-spi>0x11223344</c2s-spi><s2c-spi>0xAABBCCDD</s2c-spi>
  <ekey-c2s><bits>128</bits><val>000102030405060708090a0b0c0d0e0f</val></ekey-c2s>
  <ekey-s2c><bits>128</bits><val>101112131415161718191a1b1c1d1e1f</val></ekey-s2c>
  <akey-c2s><bits>160</bits><val>202122232425262728292a2b2c2d2e2f30313233</val></akey-c2s>
  <akey-s2c><bits>160</bits><val>404142434445464748494a4b4c4d4e4f50515253</val></akey-s2c>
  <ipsec-mode>esp-tunnel</ipsec-mode>
</ipsec></response>)";
        GlobalProtectProtocol::EspConfig esp;
        CHECK(GlobalProtectProtocol::parseEspConfig(espXml, &esp));
        CHECK(esp.present && esp.udpPort == 4501 && esp.encAlgo == "aes-128-cbc" && esp.hmacAlgo == "sha1");
        CHECK(esp.spiOut == 0x11223344u && esp.spiIn == 0xAABBCCDDu && esp.magicV4 == 0x0A001401u);
        CHECK(esp.encKeyOut.size() == 16 && esp.encKeyIn.size() == 16 && esp.hmacKeyOut.size() == 20 && esp.hmacKeyIn.size() == 20);
        CHECK(esp.encKeyOut == QByteArray::fromHex("000102030405060708090a0b0c0d0e0f"));
        // Không có ESP / thiếu khóa -> false.
        GlobalProtectProtocol::EspConfig e2;
        CHECK(!GlobalProtectProtocol::parseEspConfig("<response><ip-address>10.0.0.9</ip-address></response>", &e2));
        CHECK(!GlobalProtectProtocol::parseEspConfig("<response><ipsec><udp-port>4501</udp-port></ipsec></response>", &e2));

        // Gói dò ESP (ICMP echo + payload magic): dựng và nhận diện phản hồi.
        const QByteArray probe = GpEspChannel::buildProbePacket(0x0A001409 /*10.0.20.9*/, esp.magicV4, 7);
        CHECK((static_cast<quint8>(probe[0]) >> 4) == 4 && static_cast<quint8>(probe[9]) == 1); // IPv4/ICMP
        CHECK(static_cast<quint8>(probe[20]) == 8); // ICMP echo request
        CHECK(probe.contains(QByteArray("monitor", 7)));
        // Phản hồi = cùng gói nhưng đảo src<->magic, ICMP type 0.
        QByteArray reply = probe;
        reply[12] = char((esp.magicV4 >> 24) & 0xFF); reply[13] = char((esp.magicV4 >> 16) & 0xFF);
        reply[14] = char((esp.magicV4 >> 8) & 0xFF);  reply[15] = char(esp.magicV4 & 0xFF);
        reply[20] = char(0); // echo reply
        CHECK(GpEspChannel::isProbeReply(reply, esp.magicV4));
        CHECK(!GpEspChannel::isProbeReply(probe, esp.magicV4)); // echo REQUEST không phải reply
        CHECK(!GpEspChannel::isProbeReply(reply, 0x01020304));  // sai địa chỉ magic

        // ESP crypto (BCrypt) round-trip: đặt khóa/SPI in==out để một đối tượng seal rồi tự open được.
        for (const char* enc : {"aes-128-cbc", "aes-256-cbc"})
            for (const char* mac : {"sha1", "sha256", "md5"})
            {
                GlobalProtectProtocol::EspConfig c;
                c.present = true;
                c.encAlgo = enc;
                c.hmacAlgo = mac;
                c.spiOut = c.spiIn = 0x12345678;
                const int klen = QByteArray(enc).contains("256") ? 32 : 16;
                c.encKeyOut = c.encKeyIn = QByteArray(klen, '\x5a');
                c.hmacKeyOut = c.hmacKeyIn = QByteArray(20, '\x3c');
                c.magicV4 = 1;
                GpEspCrypto crypto;
                QString cerr;
                CHECK(crypto.init(c, &cerr));
                for (int plen : {1, 15, 16, 17, 100, 1400})
                {
                    QByteArray payload(plen, Qt::Uninitialized);
                    for (int i = 0; i < plen; ++i)
                        payload[i] = char(i & 0xFF);
                    const QByteArray sealed = crypto.seal(payload, 4);
                    CHECK(!sealed.isEmpty());
                    QByteArray back;
                    quint8 nh = 0;
                    CHECK(crypto.open(sealed, &back, &nh) && back == payload && nh == 4);
                    // Sửa 1 byte ciphertext -> HMAC phải phát hiện (từ chối).
                    QByteArray tampered = sealed;
                    tampered[tampered.size() - 1] = tampered[tampered.size() - 1] ^ 0x01;
                    CHECK(!crypto.open(tampered, &back, &nh));
                    QByteArray midTamper = sealed;
                    midTamper[30] = midTamper[30] ^ 0x01;
                    CHECK(!crypto.open(midTamper, &back, &nh));
                }
                // SPI khác -> open từ chối.
                GlobalProtectProtocol::EspConfig c2 = c;
                c2.spiIn = 0x99999999;
                GpEspCrypto other;
                CHECK(other.init(c2, &cerr));
                QByteArray x;
                quint8 nh2 = 0;
                CHECK(!other.open(crypto.seal(QByteArray(20, 'z')), &x, &nh2)); // SPI không khớp
            }
        std::printf("ESP crypto round-trip (AES-128/256, SHA1/256/MD5) OK\n");

        // Khung gói GlobalProtect.
        const QByteArray ip = fakeIpPacket(9);
        const QByteArray enc = GpFraming::encapsulateIpv4(ip);
        CHECK(enc.left(16) == QByteArray::fromHex("1a2b3c4d0800001c0100000000000000") && enc.mid(16) == ip);
        CHECK(GpFraming::keepalive() == QByteArray::fromHex("1a2b3c4d000000000000000000000000"));
        GpFraming::Deframer d;
        QByteArray out;
        const QByteArray v6 = QByteArray::fromHex("1a2b3c4d86dd000201000000000000006000");
        const QByteArray stream = enc + GpFraming::keepalive() + v6 + enc;
        QList<GpFraming::Deframer::Result> results;
        for (char c : stream)
        {
            d.feed(QByteArray(1, c));
            for (;;)
            {
                const auto r = d.next(&out, &err);
                if (r == GpFraming::Deframer::Result::NeedMore)
                    break;
                results << r;
            }
        }
        using GR = GpFraming::Deframer::Result;
        CHECK(results == (QList<GR>{GR::Packet, GR::Keepalive, GR::Skip, GR::Packet}) && out == ip);
        GpFraming::Deframer bad;
        bad.feed("HTTP/1.1 502 Bad Gateway\r\n\r\n");
        CHECK(bad.next(&out, &err) == GR::Error && err.contains("502"));
    }

    // ================= TunnelEngine đầu-cuối: FortiGate GIẢ (có OTP) =================
    {
        FakeServer srv;
        FakeFortiPpp ppp;
        int loginCalls = 0;
        bool configCookieOk = false;
        srv.onRequest = [&](QTcpSocket* s, const HttpReq& r) {
            if (r.method == "POST" && r.path == "/remote/logincheck")
            {
                ++loginCalls;
                const QUrlQuery q(QString::fromUtf8(r.body));
                if (q.hasQueryItem("credential"))
                {
                    if (q.queryItemValue("username") == "alice" && QUrl::fromPercentEncoding(q.queryItemValue("credential").toUtf8()) == "s&cret")
                        FakeServer::respond(s, 200, "ret=2,reqid=777,polid=1-1-1,grp=VPN,portal=full,peer=alice,magic=1-abc,tokeninfo=,chal_msg=Nhap ma OTP");
                    else
                        FakeServer::respond(s, 405, "");
                    return;
                }
                if (q.queryItemValue("code") == "123456" && q.queryItemValue("reqid") == "777" && q.queryItemValue("magic") == "1-abc")
                    FakeServer::respond(s, 200, "ret=1,redir=/remote/index", {"Set-Cookie: SVPNCOOKIE=SESS42; path=/; secure; httponly"});
                else
                    FakeServer::respond(s, 405, "");
                return;
            }
            if (r.method == "GET" && r.path == "/remote/fortisslvpn_xml")
            {
                configCookieOk = r.headers.value("cookie") == "SVPNCOOKIE=SESS42";
                FakeServer::respond(s, configCookieOk ? 200 : 403,
                                    "<?xml version=\"1.0\"?><sslvpn-tunnel ver=\"2\"><ipv4><assigned-addr ipv4=\"10.212.134.200\"/>"
                                    "<split-tunnel-info><addr ip=\"10.10.0.0\" mask=\"255.255.0.0\"/></split-tunnel-info></ipv4></sslvpn-tunnel>");
                return;
            }
            if (r.method == "GET" && r.path == "/remote/sslvpn-tunnel" && r.headers.value("cookie") == "SVPNCOOKIE=SESS42")
            {
                ppp.sock = s;
                srv.startTunnel(s); // từ đây là khung PPP, không trả HTTP (đúng hành vi FortiGate)
                return;
            }
            FakeServer::respond(s, 404, "");
        };
        srv.onTunnelData = [&](QTcpSocket*, const QByteArray& d) { ppp.feed(d); };

        TunnelEngine engine;
        QString otpPrompt, summary, endMsg;
        bool ended = false, endedConnected = false;
        QList<QByteArray> toAdapter;
        QObject::connect(&engine, &TunnelEngine::otpRequired, [&](const QString& p, bool) { otpPrompt = p; engine.provideOtp("123456"); });
        QObject::connect(&engine, &TunnelEngine::connected, [&](const QString& s) { summary = s; });
        QObject::connect(&engine, &TunnelEngine::packetToAdapterForTest, [&](const QByteArray& ip) { toAdapter << ip; });
        QObject::connect(&engine, &TunnelEngine::finished, [&](bool up, const QString& m) { ended = true; endedConnected = up; endMsg = m; });

        TunnelEngine::Job job;
        job.protocol = DirectProtocol::FortinetSsl;
        job.server = "127.0.0.1:" + QString::number(srv.port());
        job.username = "alice";
        job.password = "s&cret";
        job.plainForTest = true;
        job.noAdapterForTest = true;
        engine.start(job);
        CHECK(waitUntil([&]() { return engine.isUp() || ended; }, 15000));
        CHECK(engine.isUp() && otpPrompt == "Nhap ma OTP" && loginCalls == 2 && configCookieOk);
        CHECK(ppp.lcpAcked && ppp.ipcpAcked);
        CHECK(summary.contains("10.212.134.200") && summary.contains("1 mạng nội bộ"));
        std::printf("Forti gia -> connected: %s\n", qPrintable(summary));

        // Gói từ máy chủ -> adapter; gói từ adapter -> máy chủ (qua PPP + khung Fortinet).
        ppp.send(PppSession::kProtoIp, fakeIpPacket(0x77));
        CHECK(waitUntil([&]() { return !toAdapter.isEmpty(); }, 5000) && toAdapter.first() == fakeIpPacket(0x77));
        engine.injectFromAdapterForTest(fakeIpPacket(0x88));
        CHECK(waitUntil([&]() { return !ppp.ipFromClient.isEmpty(); }, 5000) && ppp.ipFromClient.first() == fakeIpPacket(0x88));

        engine.stop("Người dùng ngắt");
        CHECK(waitUntil([&]() { return ended; }, 8000) && endedConnected && endMsg == QString::fromUtf8("Người dùng ngắt"));
    }

    // ================= TunnelEngine: sai mật khẩu FortiGate (HTTP 405) =================
    {
        FakeServer srv;
        srv.onRequest = [&](QTcpSocket* s, const HttpReq&) { FakeServer::respond(s, 405, ""); };
        TunnelEngine engine;
        bool ended = false, up = true;
        QString msg;
        QObject::connect(&engine, &TunnelEngine::finished, [&](bool u, const QString& m) { ended = true; up = u; msg = m; });
        TunnelEngine::Job job;
        job.protocol = DirectProtocol::FortinetSsl;
        job.server = "127.0.0.1:" + QString::number(srv.port());
        job.username = "alice";
        job.password = "sai";
        job.plainForTest = job.noAdapterForTest = true;
        engine.start(job);
        CHECK(waitUntil([&]() { return ended; }, 10000) && !up && msg.contains(QString::fromUtf8("Sai tên đăng nhập")));
    }

    // ================= TunnelEngine đầu-cuối: GlobalProtect GIẢ (portal -> OTP -> gateway -> đường hầm) =================
    {
        FakeServer srv;
        const QString self = "127.0.0.1:" + QString::number(srv.port());
        QTcpSocket* tunnelSock = nullptr;
        GpFraming::Deframer fromClient;
        QList<QByteArray> clientPackets;
        int keepalives = 0;
        bool gatewayGotPortalCookie = false;
        bool hipChecked = false, hipReported = false;
        srv.onRequest = [&](QTcpSocket* s, const HttpReq& r) {
            const QUrlQuery q(QString::fromUtf8(r.body));
            if (r.path == "/ssl-vpn/hipreportcheck.esp")
            {
                hipChecked = q.queryItemValue("md5").size() == 32 && q.queryItemValue("client-ip") == "10.200.0.9";
                FakeServer::respond(s, 200, "<response><hip-report-needed>yes</hip-report-needed></response>");
                return;
            }
            if (r.path == "/ssl-vpn/hipreport.esp")
            {
                hipReported = r.body.contains("report=%3Chip-report") && q.queryItemValue("authcookie") == "AUTH77";
                FakeServer::respond(s, 200, "<response status=\"success\"></response>");
                return;
            }
            if (r.path == "/global-protect/prelogin.esp")
                FakeServer::respond(s, 200, "<prelogin-response><status>Success</status><username-label>Username</username-label></prelogin-response>");
            else if (r.path == "/global-protect/getconfig.esp")
            {
                if (q.queryItemValue("inputStr") == "chal-1" && q.queryItemValue("passwd") == "654321")
                    FakeServer::respond(s, 200, ("<policy><version>6.3.3-915</version><gateways><external><list><entry name=\"" + self +
                                                 "\"><description>GW</description></entry></list></external></gateways>"
                                                 "<portal-userauthcookie>PUAC</portal-userauthcookie></policy>")
                                                    .toUtf8());
                else if (q.queryItemValue("user") == "bob" && q.queryItemValue("passwd") == "pw")
                    FakeServer::respond(s, 200, "var respStatus = \"Challenge\";\nvar respMsg = \"Enter OTP\";\nthisForm.inputStr.value = \"chal-1\";\n");
                else
                    FakeServer::respond(s, 200, "<response status=\"error\"><error>Invalid username or password</error></response>");
            }
            else if (r.path == "/ssl-vpn/login.esp")
            {
                gatewayGotPortalCookie = q.queryItemValue("portal-userauthcookie") == "PUAC";
                FakeServer::respond(s, 200, "<jnlp><application-desc><argument/><argument>AUTH77</argument><argument/><argument>GP</argument>"
                                            "<argument>bob</argument><argument/><argument>vsys1</argument><argument>corp</argument>"
                                            "<argument/><argument/><argument/><argument/><argument>tunnel</argument></application-desc></jnlp>");
            }
            else if (r.path == "/ssl-vpn/getconfig.esp")
                FakeServer::respond(s, q.queryItemValue("authcookie") == "AUTH77" ? 200 : 403,
                                    "<response><ip-address>10.200.0.9</ip-address><netmask>255.255.255.255</netmask>"
                                    "<dns><member>10.0.0.10</member></dns><access-routes><member>0.0.0.0/0</member></access-routes></response>");
            else if (r.method == "GET" && r.path == "/ssl-tunnel-connect.sslvpn" && r.query == "authcookie=AUTH77&user=bob")
            {
                tunnelSock = s;
                srv.startTunnel(s);
                s->write("START_TUNNEL");
            }
            else
                FakeServer::respond(s, 404, "");
        };
        srv.onTunnelData = [&](QTcpSocket*, const QByteArray& d) {
            fromClient.feed(d);
            QByteArray ip;
            QString e;
            for (;;)
            {
                const auto res = fromClient.next(&ip, &e);
                if (res == GpFraming::Deframer::Result::NeedMore || res == GpFraming::Deframer::Result::Error)
                    break;
                if (res == GpFraming::Deframer::Result::Packet)
                    clientPackets << ip;
                else if (res == GpFraming::Deframer::Result::Keepalive)
                    ++keepalives;
            }
        };

        TunnelEngine engine;
        QString otpPrompt, summary;
        bool ended = false;
        QList<QByteArray> toAdapter;
        QObject::connect(&engine, &TunnelEngine::otpRequired, [&](const QString& p, bool) { otpPrompt = p; engine.provideOtp("654321"); });
        QObject::connect(&engine, &TunnelEngine::connected, [&](const QString& s) { summary = s; });
        QObject::connect(&engine, &TunnelEngine::packetToAdapterForTest, [&](const QByteArray& ip) { toAdapter << ip; });
        QObject::connect(&engine, &TunnelEngine::finished, [&](bool, const QString& m) { ended = true; std::printf("GP ket thuc: %s\n", qPrintable(m)); });
        TunnelEngine::Job job;
        job.protocol = DirectProtocol::GlobalProtect;
        job.server = self;
        job.username = "bob";
        job.password = "pw";
        job.plainForTest = job.noAdapterForTest = true;
        engine.start(job);
        CHECK(waitUntil([&]() { return engine.isUp() || ended; }, 15000));
        CHECK(engine.isUp() && otpPrompt == "Enter OTP" && gatewayGotPortalCookie);
        CHECK(summary.contains("10.200.0.9") && summary.contains(QString::fromUtf8("toàn bộ lưu lượng")));
        // HIP: gateway yêu cầu -> phải kiểm VÀ nộp báo cáo TRƯỚC khi đường hầm lên (nếu không gateway bỏ gói -> rớt).
        CHECK(hipChecked && hipReported);
        std::printf("GP gia -> connected: %s (HIP checked=%d reported=%d)\n", qPrintable(summary), hipChecked, hipReported);
        tunnelSock->write(GpFraming::encapsulateIpv4(fakeIpPacket(0x31)));
        CHECK(waitUntil([&]() { return !toAdapter.isEmpty(); }, 5000) && toAdapter.first() == fakeIpPacket(0x31));
        engine.injectFromAdapterForTest(fakeIpPacket(0x32));
        CHECK(waitUntil([&]() { return !clientPackets.isEmpty(); }, 5000) && clientPackets.first() == fakeIpPacket(0x32));
        // Keepalive mỗi 10 giây (như client chính thức).
        CHECK(waitUntil([&]() { return keepalives > 0; }, 13000));
        engine.stop("stop");
        CHECK(waitUntil([&]() { return ended; }, 8000));
    }

    // ================= TunnelEngine đầu-cuối: GlobalProtect qua ESP (gateway GIẢ phản hồi ESP/UDP thật) =======
    {
        // Khóa/SPI cố định; gateway giả dùng bộ ĐẢO (c2s<->s2c) để giải mã gói client và seal gói trả về.
        const QByteArray ekC2S = QByteArray::fromHex("000102030405060708090a0b0c0d0e0f");
        const QByteArray ekS2C = QByteArray::fromHex("101112131415161718191a1b1c1d1e1f");
        const QByteArray akC2S = QByteArray::fromHex("202122232425262728292a2b2c2d2e2f30313233");
        const QByteArray akS2C = QByteArray::fromHex("404142434445464748494a4b4c4d4e4f50515253");

        QUdpSocket espGw; // gateway ESP giả
        CHECK(espGw.bind(QHostAddress(QHostAddress::LocalHost), 0));
        const quint16 espPort = espGw.localPort();

        GlobalProtectProtocol::EspConfig gwCfg; // crypto phía gateway = đảo chiều
        gwCfg.present = true; gwCfg.encAlgo = "aes-128-cbc"; gwCfg.hmacAlgo = "sha1";
        gwCfg.spiOut = 0xAABBCCDD; gwCfg.spiIn = 0x11223344; // out=s2c, in=c2s
        gwCfg.encKeyOut = ekS2C; gwCfg.encKeyIn = ekC2S; gwCfg.hmacKeyOut = akS2C; gwCfg.hmacKeyIn = akC2S;
        gwCfg.magicV4 = 1;
        auto gwCrypto = std::make_shared<GpEspCrypto>();
        QString gwErr;
        CHECK(gwCrypto->init(gwCfg, &gwErr));

        QList<QByteArray> gwDataFromClient;
        int gwProbes = 0;
        QObject::connect(&espGw, &QUdpSocket::readyRead, [&]() {
            while (espGw.hasPendingDatagrams())
            {
                QByteArray dg(espGw.pendingDatagramSize(), Qt::Uninitialized);
                QHostAddress from; quint16 fromPort = 0;
                espGw.readDatagram(dg.data(), dg.size(), &from, &fromPort);
                QByteArray inner; quint8 nh = 0;
                if (!gwCrypto->open(dg, &inner, &nh))
                    continue;
                if (GpEspChannel::isProbeReply(inner, 0x0A001401) || (inner.size() > 20 && static_cast<quint8>(inner[20]) == 8))
                {
                    // Gói dò (echo request) -> trả echo REPLY: đảo src<->dst, ICMP type 0.
                    ++gwProbes;
                    QByteArray rep = inner;
                    for (int i = 0; i < 4; ++i) std::swap(rep[12 + i], rep[16 + i]);
                    rep[20] = char(0);
                    espGw.writeDatagram(gwCrypto->seal(rep, 4), from, fromPort);
                }
                else if ((static_cast<quint8>(inner[0]) >> 4) == 4)
                {
                    gwDataFromClient << inner;
                    espGw.writeDatagram(gwCrypto->seal(inner, 4), from, fromPort); // dội lại để client nhận
                }
            }
        });

        FakeServer srv;
        const QString self = "127.0.0.1:" + QString::number(srv.port());
        const QString espBlock = QStringLiteral(
            "<gw-address>10.0.20.1</gw-address><ipsec><udp-port>%1</udp-port><enc-algo>aes-128-cbc</enc-algo>"
            "<hmac-algo>sha1</hmac-algo><c2s-spi>0x11223344</c2s-spi><s2c-spi>0xAABBCCDD</s2c-spi>"
            "<ekey-c2s><bits>128</bits><val>000102030405060708090a0b0c0d0e0f</val></ekey-c2s>"
            "<ekey-s2c><bits>128</bits><val>101112131415161718191a1b1c1d1e1f</val></ekey-s2c>"
            "<akey-c2s><bits>160</bits><val>202122232425262728292a2b2c2d2e2f30313233</val></akey-c2s>"
            "<akey-s2c><bits>160</bits><val>404142434445464748494a4b4c4d4e4f50515253</val></akey-s2c>"
            "<ipsec-mode>esp-tunnel</ipsec-mode></ipsec>").arg(espPort);
        srv.onRequest = [&](QTcpSocket* s, const HttpReq& r) {
            if (r.path == "/global-protect/prelogin.esp")
                FakeServer::respond(s, 200, "<prelogin-response><status>Success</status></prelogin-response>");
            else if (r.path == "/global-protect/getconfig.esp")
                FakeServer::respond(s, 200, ("<policy><version>6.3</version><gateways><external><list><entry name=\"" + self +
                                             "\"><description>GW</description></entry></list></external></gateways></policy>").toUtf8());
            else if (r.path == "/ssl-vpn/login.esp")
                FakeServer::respond(s, 200, "<jnlp><application-desc><argument/><argument>AUTH9</argument><argument/><argument>GP</argument>"
                                            "<argument>bob</argument><argument/><argument/><argument/><argument/><argument/><argument/>"
                                            "<argument/><argument>tunnel</argument></application-desc></jnlp>");
            else if (r.path == "/ssl-vpn/hipreportcheck.esp")
                FakeServer::respond(s, 200, "<response><hip-report-needed>no</hip-report-needed></response>");
            else if (r.path == "/ssl-vpn/getconfig.esp")
                FakeServer::respond(s, 200, ("<response><ip-address>10.0.20.9</ip-address><netmask>255.255.255.255</netmask>"
                                             "<access-routes><member>10.0.0.0/8</member></access-routes>" + espBlock + "</response>").toUtf8());
            else
                FakeServer::respond(s, 404, "");
        };

        TunnelEngine engine;
        QString summary;
        bool ended = false;
        QList<QByteArray> toAdapter;
        QObject::connect(&engine, &TunnelEngine::connected, [&](const QString& s) { summary = s; });
        QObject::connect(&engine, &TunnelEngine::packetToAdapterForTest, [&](const QByteArray& ip) { toAdapter << ip; });
        QObject::connect(&engine, &TunnelEngine::finished, [&](bool, const QString& m) { ended = true; std::printf("GP-ESP ket thuc: %s\n", qPrintable(m)); });
        TunnelEngine::Job job;
        job.protocol = DirectProtocol::GlobalProtect;
        job.server = self;
        job.username = "bob";
        job.password = "pw";
        job.plainForTest = job.noAdapterForTest = true; // HTTP trần + không adapter; ESP vẫn chạy thật trên UDP loopback
        engine.start(job);
        CHECK(waitUntil([&]() { return engine.isUp() || ended; }, 15000));
        CHECK(engine.isUp() && gwProbes > 0);
        CHECK(summary.contains("ESP") && summary.contains("10.0.20.9"));
        std::printf("GP-ESP -> connected: %s (gw nhan %d goi do)\n", qPrintable(summary), gwProbes);
        // Dữ liệu client -> gateway (qua ESP) -> dội lại -> adapter.
        engine.injectFromAdapterForTest(fakeIpPacket(0x5a));
        CHECK(waitUntil([&]() { return !gwDataFromClient.isEmpty(); }, 5000) && gwDataFromClient.first() == fakeIpPacket(0x5a));
        CHECK(waitUntil([&]() { return !toAdapter.isEmpty(); }, 5000) && toAdapter.first() == fakeIpPacket(0x5a));
        engine.stop("stop");
        CHECK(waitUntil([&]() { return ended; }, 8000));
    }

    // ================= TunnelEngine: GlobalProtect sai mật khẩu =================
    {
        FakeServer srv;
        srv.onRequest = [&](QTcpSocket* s, const HttpReq& r) {
            if (r.path == "/global-protect/prelogin.esp")
                FakeServer::respond(s, 200, "<prelogin-response><status>Success</status></prelogin-response>");
            else
                FakeServer::respond(s, 200, "<response status=\"error\"><error>Invalid username or password</error></response>");
        };
        TunnelEngine engine;
        bool ended = false;
        QString msg;
        QObject::connect(&engine, &TunnelEngine::finished, [&](bool, const QString& m) { ended = true; msg = m; });
        TunnelEngine::Job job;
        job.protocol = DirectProtocol::GlobalProtect;
        job.server = "127.0.0.1:" + QString::number(srv.port());
        job.username = "bob";
        job.password = "x";
        job.plainForTest = job.noAdapterForTest = true;
        engine.start(job);
        CHECK(waitUntil([&]() { return ended; }, 10000) && msg.contains(QString::fromUtf8("Sai tên đăng nhập")));
    }

    // ================= Tiến trình phụ THẬT (OneForAll.exe --vpn-helper): kênh điều khiển + tự thoát =================
    {
        const QString exe = QCoreApplication::applicationDirPath() + "/OneForAll.exe";
        if (!QFileInfo::exists(exe))
            std::printf("BO QUA test tien trinh phu: chua co %s\n", qPrintable(exe));
        else
        {
            QLocalServer server;
            const QString name = "OneForAll-vpn-test-" + QString::number(QCoreApplication::applicationPid());
            QLocalServer::removeServer(name);
            CHECK(server.listen(name));
            QProcess helper;
            helper.start(exe, {"--vpn-helper", server.fullServerName(), "tok123"});
            CHECK(helper.waitForStarted(10000));
            CHECK(waitUntil([&]() { return server.hasPendingConnections(); }, 15000));
            QLocalSocket* s = server.nextPendingConnection();
            QByteArray buf;
            QList<QJsonObject> msgs;
            if (s)
            {
                auto readAll = [&]() {
                    buf += s->readAll();
                    int nl;
                    while ((nl = buf.indexOf('\n')) >= 0)
                    {
                        msgs << QJsonDocument::fromJson(buf.left(nl)).object();
                        buf.remove(0, nl + 1);
                    }
                };
                QObject::connect(s, &QLocalSocket::readyRead, readAll);
                readAll(); // "hello" có thể đã tới TRƯỚC khi gắn slot (readyRead đã phát) - đọc phần sẵn có
                CHECK(waitUntil([&]() { return !msgs.isEmpty(); }, 10000));
                CHECK(!msgs.isEmpty() && msgs.first().value("t").toString() == "hello" && msgs.first().value("token").toString() == "tok123");
                // Việc tới cổng đóng (TLS thật) -> tiến trình phụ phải báo "end" với lỗi, không treo, không tạo adapter.
                s->write(QJsonDocument(QJsonObject{{"t", "job"}, {"protocol", 1}, {"server", "127.0.0.1:1"}, {"user", "a"}, {"pass", "b"}})
                             .toJson(QJsonDocument::Compact) + '\n');
                auto hasEnd = [&]() {
                    for (const QJsonObject& m : msgs)
                        if (m.value("t").toString() == "end")
                            return true;
                    return false;
                };
                CHECK(waitUntil(hasEnd, 20000));
                for (const QJsonObject& m : msgs)
                    if (m.value("t").toString() == "end")
                    {
                        CHECK(!m.value("connected").toBool() && !m.value("message").toString().isEmpty());
                        std::printf("Helper end -> %s\n", qPrintable(m.value("message").toString()));
                    }
                // Đóng kênh điều khiển (như ứng dụng chính thoát) -> tiến trình phụ phải tự thoát.
                s->abort();
            }
            CHECK(helper.waitForFinished(15000));
            if (helper.state() != QProcess::NotRunning)
                helper.kill();
        }
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: chua kiem chung voi FortiGate/GlobalProtect THAT (can tai khoan cua nguoi dung) va chua tao\n");
    std::printf("adapter wintun that (can quyen Administrator) - xem PROJECT_OVERVIEW.md muc 4zq.\n");
    return g_fail == 0 ? 0 : 1;
}
