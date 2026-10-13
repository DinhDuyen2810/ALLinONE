#pragma once

// Máy chủ HTTP tối thiểu trên 127.0.0.1 dùng chung cho các test Downloader (lõi + giao diện): không gói tin nào ra khỏi máy.

#include <QByteArray>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include <functional>
#include <memory>

/// Máy chủ HTTP/1.1 tối thiểu trên 127.0.0.1 (cổng ngẫu nhiên): mỗi kết nối đọc MỘT yêu cầu, gọi
/// `handler(phần đầu yêu cầu)` lấy nguyên văn các byte trả lời, ghi ra rồi đóng kết nối. Trả lời khai
/// Content-Length lớn hơn số byte thật sự gửi = giả lập "đứt kết nối giữa chừng".
struct MiniHttpServer
{
    QTcpServer server;
    QList<QByteArray> requests; ///< Phần đầu (request line + header) của từng yêu cầu đã nhận, theo thứ tự
    std::function<QByteArray(const QByteArray& head)> handler;

    MiniHttpServer()
    {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this]() {
            while (QTcpSocket* sock = server.nextPendingConnection())
            {
                auto buffer = std::make_shared<QByteArray>();
                QObject::connect(sock, &QTcpSocket::readyRead, sock, [this, sock, buffer]() {
                    buffer->append(sock->readAll());
                    const int end = buffer->indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    const QByteArray head = buffer->left(end);
                    buffer->clear();
                    requests << head;
                    sock->write(handler(head));
                    sock->disconnectFromHost(); // ghi hết phần đã xếp hàng rồi mới đóng
                });
                QObject::connect(sock, &QTcpSocket::disconnected, sock, &QObject::deleteLater);
            }
        });
        server.listen(QHostAddress::LocalHost, 0);
    }

    QString url(const QString& path) const { return QString("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path); }

    /// Giá trị một header trong phần đầu yêu cầu (không phân biệt hoa thường), rỗng nếu không có.
    static QByteArray header(const QByteArray& head, const QByteArray& name)
    {
        for (const QByteArray& line : head.split('\n'))
        {
            const int colon = line.indexOf(':');
            if (colon > 0 && line.left(colon).trimmed().toLower() == name.toLower())
                return line.mid(colon + 1).trimmed();
        }
        return {};
    }
    static QByteArray path(const QByteArray& head) { return head.split(' ').value(1); }

    static QByteArray response(const QByteArray& statusLine, const QList<QByteArray>& headers, const QByteArray& body,
                               qint64 declaredLength = -1)
    {
        QByteArray out = "HTTP/1.1 " + statusLine + "\r\nConnection: close\r\n";
        for (const QByteArray& h : headers)
            out += h + "\r\n";
        out += "Content-Length: " + QByteArray::number(declaredLength >= 0 ? declaredLength : body.size()) + "\r\n\r\n";
        return out + body;
    }
};
