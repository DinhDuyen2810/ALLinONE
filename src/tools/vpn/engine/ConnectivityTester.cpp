#include "ConnectivityTester.h"

#include <QTcpSocket>
#include <QTimer>

ConnectivityTester::ConnectivityTester(QObject* parent)
    : QObject(parent)
{
    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    connect(m_timeout, &QTimer::timeout, this, [this]() {
        if (m_done)
            return;
        const int ms = static_cast<int>(m_clock.elapsed());
        cleanup();
        emit finished(false, ms, QStringLiteral("Hết thời gian chờ - máy chủ không phản hồi (có thể đã chết, "
                                                 "chặn cổng này, hoặc chặn mọi kết nối tự động)."));
    });
}

ConnectivityTester::~ConnectivityTester()
{
    cleanup();
}

bool ConnectivityTester::isRunning() const
{
    return !m_done;
}

void ConnectivityTester::cleanup()
{
    m_done = true;
    if (m_timeout)
        m_timeout->stop();
    if (m_socket)
    {
        m_socket->disconnect(this);
        m_socket->abort();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

void ConnectivityTester::cancel()
{
    if (!m_done)
        cleanup();
}

void ConnectivityTester::test(const QString& host, quint16 port, int timeoutMs)
{
    cleanup();
    m_done = false;

    auto* sock = new QTcpSocket(this);
    m_socket = sock;

    connect(sock, &QTcpSocket::connected, this, [this]() {
        if (m_done)
            return;
        const int ms = static_cast<int>(m_clock.elapsed());
        cleanup();
        emit finished(true, ms, QStringLiteral("Kết nối TCP thành công."));
    });
    connect(sock, &QTcpSocket::errorOccurred, this, [this, sock](QAbstractSocket::SocketError) {
        if (m_done)
            return;
        const int ms = static_cast<int>(m_clock.elapsed());
        const QString reason = sock->errorString();
        cleanup();
        emit finished(false, ms, QStringLiteral("Không kết nối được: ") + reason);
    });

    m_clock.start();
    m_timeout->start(timeoutMs);
    sock->connectToHost(host, port);
}
