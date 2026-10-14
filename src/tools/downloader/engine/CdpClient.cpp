#include "CdpClient.h"

#include <QHostAddress>
#include <QJsonDocument>
#include <QRandomGenerator>

namespace
{
constexpr int kSliceMs = 200;                          // lát chờ giữa hai lần kiểm tra cờ hủy
constexpr quint64 kMaxMessageBytes = 512ULL * 1024 * 1024; // chặn độ dài khung bất thường (tránh cấp phát khổng lồ)
} // namespace

CdpClient::CdpClient(const std::atomic_bool* cancel)
    : m_cancel(cancel)
{
}

CdpClient::Status CdpClient::fail(Status status, const QString& text)
{
    m_error = text;
    return status;
}

void CdpClient::close()
{
    if (m_sock.state() != QAbstractSocket::UnconnectedState)
    {
        m_sock.abort();
    }
    m_buf.clear();
}

// Đợi thêm dữ liệu từ socket (đã chuyển hết vào m_buf). Chia lát 200 ms để thấy cờ hủy.
CdpClient::Status CdpClient::fill(const QDeadlineTimer& deadline)
{
    for (;;)
    {
        if (canceled())
            return fail(Status::Canceled, "Đã hủy.");
        if (m_sock.bytesAvailable() > 0)
        {
            m_buf += m_sock.readAll();
            return Status::Ok;
        }
        if (m_sock.state() != QAbstractSocket::ConnectedState)
            return fail(Status::Disconnected, "Trình duyệt đã đóng kết nối.");
        if (deadline.hasExpired())
            return fail(Status::TimedOut, "Hết thời gian chờ phản hồi từ trình duyệt.");
        const qint64 remaining = deadline.remainingTime();
        const int slice = remaining < 0 ? kSliceMs : int(qMin<qint64>(kSliceMs, remaining));
        if (m_sock.waitForReadyRead(qMax(1, slice)))
        {
            m_buf += m_sock.readAll();
            return Status::Ok;
        }
    }
}

CdpClient::Status CdpClient::take(qsizetype n, const QDeadlineTimer& deadline, QByteArray* out)
{
    while (m_buf.size() < n)
    {
        const Status st = fill(deadline);
        if (st != Status::Ok)
            return st;
    }
    *out = m_buf.left(n);
    m_buf.remove(0, n);
    return Status::Ok;
}

CdpClient::Status CdpClient::httpGetLocal(quint16 port, const QByteArray& path, int timeoutMs, QByteArray* body)
{
    const QDeadlineTimer deadline(timeoutMs);
    close();
    m_sock.connectToHost(QHostAddress(QHostAddress::LocalHost), port);
    while (m_sock.state() == QAbstractSocket::ConnectingState || m_sock.state() == QAbstractSocket::HostLookupState)
    {
        if (canceled())
            return fail(Status::Canceled, "Đã hủy.");
        if (deadline.hasExpired())
            return fail(Status::TimedOut, "Không nối được cổng DevTools của trình duyệt.");
        m_sock.waitForConnected(kSliceMs);
    }
    if (m_sock.state() != QAbstractSocket::ConnectedState)
        return fail(Status::Disconnected, "Không nối được cổng DevTools của trình duyệt.");

    // HTTP/1.1 bắt buộc: máy chủ DevTools của Edge/Chrome đóng kết nối ngay (không trả gì) khi nhận yêu cầu HTTP/1.0.
    m_sock.write("GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) + "\r\nConnection: close\r\n\r\n");
    m_sock.waitForBytesWritten(5000);

    // Đọc phần đầu, rồi đọc tiếp tới khi đủ Content-Length (hoặc tới khi server đóng nếu không khai độ dài).
    Status st = Status::Ok;
    while (!m_buf.contains("\r\n\r\n"))
    {
        st = fill(deadline);
        if (st != Status::Ok)
        {
            close();
            return st;
        }
    }
    const int headEnd = m_buf.indexOf("\r\n\r\n");
    const QByteArray head = m_buf.left(headEnd);
    if (!head.startsWith("HTTP/1.") || !head.mid(8, 5).contains("200"))
    {
        close();
        return fail(Status::Failed, "DevTools trả lỗi: " + QString::fromLatin1(head.left(40)));
    }
    qint64 length = -1;
    for (const QByteArray& line : head.split('\n'))
        if (line.trimmed().toLower().startsWith("content-length:"))
            length = line.mid(line.indexOf(':') + 1).trimmed().toLongLong();
    const qsizetype bodyStart = headEnd + 4;
    while (length < 0 ? true : (m_buf.size() - bodyStart < length))
    {
        st = fill(deadline);
        if (st == Status::Disconnected && length < 0)
            break; // không khai độ dài: server đóng kết nối = hết thân
        if (st != Status::Ok)
        {
            close();
            return st;
        }
    }
    *body = m_buf.mid(bodyStart, length < 0 ? -1 : qsizetype(length));
    close();
    return Status::Ok;
}

QByteArray CdpClient::encodeClientFrame(quint8 opcode, const QByteArray& payload, const char mask[4])
{
    QByteArray f;
    f.append(char(0x80 | (opcode & 0x0F))); // FIN + opcode
    const quint64 len = quint64(payload.size());
    if (len < 126)
        f.append(char(0x80 | len)); // bit 0x80 = có che
    else if (len < 65536)
    {
        f.append(char(0x80 | 126));
        f.append(char(len >> 8));
        f.append(char(len & 0xFF));
    }
    else
    {
        f.append(char(0x80 | 127));
        for (int i = 7; i >= 0; --i)
            f.append(char((len >> (8 * i)) & 0xFF));
    }
    f.append(mask, 4);
    for (qsizetype i = 0; i < payload.size(); ++i)
        f.append(char(payload[i] ^ mask[i & 3]));
    return f;
}

CdpClient::Status CdpClient::sendFrame(quint8 opcode, const QByteArray& payload)
{
    char mask[4];
    for (char& m : mask)
        m = char(QRandomGenerator::global()->bounded(256));
    m_sock.write(encodeClientFrame(opcode, payload, mask));
    while (m_sock.bytesToWrite() > 0)
    {
        if (canceled())
            return fail(Status::Canceled, "Đã hủy.");
        if (m_sock.state() != QAbstractSocket::ConnectedState)
            return fail(Status::Disconnected, "Trình duyệt đã đóng kết nối.");
        m_sock.waitForBytesWritten(kSliceMs);
    }
    return Status::Ok;
}

CdpClient::Status CdpClient::connectTo(quint16 port, const QString& wsPath, int timeoutMs)
{
    const QDeadlineTimer deadline(timeoutMs);
    close();
    m_sock.connectToHost(QHostAddress(QHostAddress::LocalHost), port);
    while (m_sock.state() == QAbstractSocket::ConnectingState || m_sock.state() == QAbstractSocket::HostLookupState)
    {
        if (canceled())
            return fail(Status::Canceled, "Đã hủy.");
        if (deadline.hasExpired())
            return fail(Status::TimedOut, "Không nối được WebSocket của trình duyệt.");
        m_sock.waitForConnected(kSliceMs);
    }
    if (m_sock.state() != QAbstractSocket::ConnectedState)
        return fail(Status::Disconnected, "Không nối được WebSocket của trình duyệt.");

    QByteArray rnd(16, 0);
    for (char& c : rnd)
        c = char(QRandomGenerator::global()->bounded(256));
    m_sock.write("GET " + wsPath.toUtf8() + " HTTP/1.1\r\nHost: 127.0.0.1:" + QByteArray::number(port) +
                 "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + rnd.toBase64() +
                 "\r\nSec-WebSocket-Version: 13\r\n\r\n");
    m_sock.waitForBytesWritten(5000);

    while (!m_buf.contains("\r\n\r\n"))
    {
        const Status st = fill(deadline);
        if (st != Status::Ok)
            return st;
    }
    const int end = m_buf.indexOf("\r\n\r\n") + 4;
    const QByteArray head = m_buf.left(end);
    m_buf.remove(0, end); // phần dư (nếu có) đã là khung WebSocket đầu tiên - giữ lại
    if (!head.startsWith("HTTP/1.1 101"))
        return fail(Status::Failed, "DevTools từ chối kết nối WebSocket: " + QString::fromLatin1(head.left(60)));
    return Status::Ok;
}

// Một message văn bản hoàn chỉnh (ghép các mảnh; tự trả lời ping; khung đóng -> Disconnected).
CdpClient::Status CdpClient::readMessage(const QDeadlineTimer& deadline, QByteArray* message)
{
    message->clear();
    for (;;)
    {
        QByteArray h;
        Status st = take(2, deadline, &h);
        if (st != Status::Ok)
            return st;
        const bool fin = (h[0] & 0x80) != 0;
        const int opcode = h[0] & 0x0F;
        quint64 len = quint8(h[1]) & 0x7F;
        if (len == 126)
        {
            QByteArray e;
            if ((st = take(2, deadline, &e)) != Status::Ok)
                return st;
            len = (quint64(quint8(e[0])) << 8) | quint8(e[1]);
        }
        else if (len == 127)
        {
            QByteArray e;
            if ((st = take(8, deadline, &e)) != Status::Ok)
                return st;
            len = 0;
            for (int i = 0; i < 8; ++i)
                len = (len << 8) | quint8(e[i]);
        }
        if (len > kMaxMessageBytes)
            return fail(Status::Failed, "Trình duyệt gửi khung dữ liệu quá lớn.");
        QByteArray data;
        if ((st = take(qsizetype(len), deadline, &data)) != Status::Ok) // khung từ server KHÔNG che
            return st;
        if (opcode == 8)
            return fail(Status::Disconnected, "Trình duyệt đã đóng kết nối.");
        if (opcode == 9) // ping -> pong
        {
            if ((st = sendFrame(0x0A, data)) != Status::Ok)
                return st;
            continue;
        }
        if (opcode == 0x0A)
            continue;
        *message += data;
        if (fin)
            return Status::Ok;
    }
}

CdpClient::Status CdpClient::call(const QString& method, const QJsonObject& params, int timeoutMs, QJsonObject* result)
{
    const QDeadlineTimer deadline(timeoutMs);
    const int id = ++m_nextId;
    Status st = sendFrame(0x01, QJsonDocument(QJsonObject{{"id", id}, {"method", method}, {"params", params}})
                                    .toJson(QJsonDocument::Compact));
    if (st != Status::Ok)
        return st;
    for (;;)
    {
        QByteArray msg;
        st = readMessage(deadline, &msg);
        if (st != Status::Ok)
            return st;
        const QJsonObject o = QJsonDocument::fromJson(msg).object();
        if (o.value("id").toInt(-1) != id)
            continue; // sự kiện hoặc phản hồi cũ - bỏ qua
        if (o.contains("error"))
            return fail(Status::Failed, o.value("error").toObject().value("message").toString("Lệnh bị từ chối."));
        if (result)
            *result = o.value("result").toObject();
        return Status::Ok;
    }
}

CdpClient::Status CdpClient::evaluate(const QString& expression, int timeoutMs, QJsonValue* value)
{
    QJsonObject r;
    const Status st = call("Runtime.evaluate",
                           {{"expression", expression}, {"awaitPromise", true}, {"returnByValue", true}}, timeoutMs, &r);
    if (st != Status::Ok)
        return st;
    if (r.contains("exceptionDetails"))
    {
        const QJsonObject d = r.value("exceptionDetails").toObject();
        return fail(Status::Failed, d.value("exception").toObject().value("description").toString(d.value("text").toString()));
    }
    if (value)
        *value = r.value("result").toObject().value("value");
    return Status::Ok;
}
