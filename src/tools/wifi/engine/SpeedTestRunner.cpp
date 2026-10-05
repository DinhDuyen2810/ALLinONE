#include "SpeedTestRunner.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <cmath>

namespace
{
// Endpoint đo tốc độ công khai, không cần khóa API - cùng backend mà https://speed.cloudflare.com dùng.
const QUrl kPingUrl("https://speed.cloudflare.com/__down?bytes=0");
const QUrl kDownloadUrl("https://speed.cloudflare.com/__down?bytes=60000000"); // 60MB, bị cắt sớm theo thời gian
const QUrl kUploadUrl("https://speed.cloudflare.com/__up");

constexpr int kPingSampleCount = 8;
constexpr int kPingTimeoutMs = 4000;
constexpr int kTransferMaxDurationMs = 9000; // tính từ byte đầu tiên, không tính thời gian bắt tay TCP/TLS
constexpr qint64 kUploadBytes = 24 * 1000 * 1000; // 24MB

double mbps(qint64 bytes, qint64 elapsedMs)
{
    if (elapsedMs <= 0)
        return 0.0;
    return (static_cast<double>(bytes) * 8.0) / 1e6 / (static_cast<double>(elapsedMs) / 1000.0);
}
} // namespace

SpeedTestRunner::SpeedTestRunner(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    m_abortTimer = new QTimer(this);
    m_abortTimer->setSingleShot(true);
    connect(m_abortTimer, &QTimer::timeout, this, [this] {
        m_intentionalAbort = true;
        abortActiveReply();
    });
}

SpeedTestRunner::~SpeedTestRunner()
{
    abortActiveReply();
}

void SpeedTestRunner::abortActiveReply()
{
    if (m_activeReply)
        m_activeReply->abort();
}

void SpeedTestRunner::start()
{
    if (m_running)
        return;

    m_running = true;
    m_stopRequested = false;
    m_pingIndex = 0;
    m_pingSamplesMs.clear();
    runPingPhase();
}

void SpeedTestRunner::stop()
{
    if (!m_running)
        return;
    m_stopRequested = true;
    m_intentionalAbort = true;
    abortActiveReply();
}

void SpeedTestRunner::finishAll()
{
    m_running = false;
    m_abortTimer->stop();
    emit finished();
}

// ------------------------------------------------------------------ Ping

void SpeedTestRunner::runPingPhase()
{
    emit phaseChanged("Đang đo ping...");
    sendNextPing();
}

void SpeedTestRunner::sendNextPing()
{
    if (m_stopRequested)
    {
        finishAll();
        return;
    }

    QNetworkRequest req(kPingUrl);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setRawHeader("Cache-Control", "no-cache");

    m_pingTimer.start();
    m_activeReply = m_nam->get(req);

    // So khớp đúng con trỏ reply (không chỉ kiểm tra m_activeReply != nullptr): nếu ping này đã
    // hoàn tất trước 4s và ping kế tiếp đã bắt đầu, m_activeReply lúc timeout bắn ra sẽ trỏ tới
    // reply MỚI - không được abort nhầm reply đó.
    QNetworkReply* thisReply = m_activeReply;
    QTimer::singleShot(kPingTimeoutMs, thisReply, [this, thisReply] {
        if (m_activeReply == thisReply)
        {
            m_intentionalAbort = false; // timeout thật sự, không phải hủy có chủ đích
            thisReply->abort();
        }
    });

    connect(m_activeReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = m_activeReply;
        m_activeReply = nullptr;
        const double elapsedMs = static_cast<double>(m_pingTimer.elapsed());
        const bool stoppedByUser = m_stopRequested;
        const bool ok = (reply->error() == QNetworkReply::NoError) || stoppedByUser;
        reply->deleteLater();

        if (!ok)
        {
            emit errorOccurred("Không kết nối được tới máy chủ đo tốc độ. Kiểm tra kết nối Internet rồi thử lại.");
            finishAll();
            return;
        }
        if (stoppedByUser)
        {
            finishAll();
            return;
        }

        m_pingSamplesMs.push_back(elapsedMs);

        double minMs = m_pingSamplesMs.first(), maxMs = m_pingSamplesMs.first(), sum = 0;
        for (double v : m_pingSamplesMs)
        {
            minMs = std::min(minMs, v);
            maxMs = std::max(maxMs, v);
            sum += v;
        }
        const double avgMs = sum / m_pingSamplesMs.size();

        double jitterSum = 0;
        for (int i = 1; i < m_pingSamplesMs.size(); ++i)
            jitterSum += std::fabs(m_pingSamplesMs[i] - m_pingSamplesMs[i - 1]);
        const double jitterMs = m_pingSamplesMs.size() > 1 ? jitterSum / (m_pingSamplesMs.size() - 1) : 0.0;

        emit pingUpdated(elapsedMs, minMs, avgMs, maxMs, jitterMs);

        ++m_pingIndex;
        if (m_stopRequested)
        {
            finishAll();
        }
        else if (m_pingIndex < kPingSampleCount)
        {
            sendNextPing();
        }
        else
        {
            runDownloadPhase();
        }
    });
}

// ------------------------------------------------------------------ Download

void SpeedTestRunner::runDownloadPhase()
{
    emit phaseChanged("Đang đo tốc độ tải xuống...");
    m_transferStarted = false;
    m_bytesTransferred = 0;

    QNetworkRequest req(kDownloadUrl);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    m_activeReply = m_nam->get(req);

    connect(m_activeReply, &QNetworkReply::readyRead, this, [this] {
        if (!m_activeReply)
            return;
        const QByteArray chunk = m_activeReply->readAll();
        m_bytesTransferred += chunk.size();

        if (!m_transferStarted)
        {
            m_transferStarted = true;
            m_transferTimer.start();
            m_abortTimer->start(kTransferMaxDurationMs);
        }
        else if (m_transferTimer.elapsed() > 50)
        {
            emit downloadProgress(mbps(m_bytesTransferred, m_transferTimer.elapsed()), m_bytesTransferred);
        }
    });

    connect(m_activeReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = m_activeReply;
        m_activeReply = nullptr;
        m_abortTimer->stop();

        const bool wasIntentionalAbort = m_intentionalAbort;
        m_intentionalAbort = false;
        const bool ok = (reply->error() == QNetworkReply::NoError) || wasIntentionalAbort;
        reply->deleteLater();

        if (!ok)
        {
            emit errorOccurred("Lỗi khi đo tốc độ tải xuống. Kiểm tra kết nối Internet rồi thử lại.");
            finishAll();
            return;
        }

        const double finalMbps = m_transferStarted ? mbps(m_bytesTransferred, m_transferTimer.elapsed()) : 0.0;
        emit downloadFinished(finalMbps);

        if (m_stopRequested)
            finishAll();
        else
            runUploadPhase();
    });
}

// ------------------------------------------------------------------ Upload

void SpeedTestRunner::runUploadPhase()
{
    emit phaseChanged("Đang đo tốc độ tải lên...");
    m_transferStarted = false;
    m_bytesTransferred = 0;

    QNetworkRequest req(kUploadUrl);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");

    const QByteArray payload(kUploadBytes, 'Q'); // nội dung không quan trọng, chỉ cần đúng dung lượng
    m_activeReply = m_nam->post(req, payload);

    connect(m_activeReply, &QNetworkReply::uploadProgress, this, [this](qint64 bytesSent, qint64) {
        if (bytesSent <= 0)
            return;
        m_bytesTransferred = bytesSent;
        if (!m_transferStarted)
        {
            m_transferStarted = true;
            m_transferTimer.start();
            m_abortTimer->start(kTransferMaxDurationMs);
        }
        else if (m_transferTimer.elapsed() > 50)
        {
            emit uploadProgress(mbps(m_bytesTransferred, m_transferTimer.elapsed()), m_bytesTransferred);
        }
    });

    connect(m_activeReply, &QNetworkReply::finished, this, [this] {
        QNetworkReply* reply = m_activeReply;
        m_activeReply = nullptr;
        m_abortTimer->stop();

        const bool wasIntentionalAbort = m_intentionalAbort;
        m_intentionalAbort = false;
        const bool ok = (reply->error() == QNetworkReply::NoError) || wasIntentionalAbort;
        reply->deleteLater();

        if (!ok)
        {
            emit errorOccurred("Lỗi khi đo tốc độ tải lên. Kiểm tra kết nối Internet rồi thử lại.");
            finishAll();
            return;
        }

        const double finalMbps = m_transferStarted ? mbps(m_bytesTransferred, m_transferTimer.elapsed()) : 0.0;
        emit uploadFinished(finalMbps);
        finishAll();
    });
}
