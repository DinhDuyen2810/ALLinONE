#include "SpeedTestRunner.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace
{
// Endpoint đo tốc độ công khai, không cần khóa API - cùng backend mà https://speed.cloudflare.com dùng.
const QUrl kPingUrl("https://speed.cloudflare.com/__down?bytes=0");
const QUrl kDownloadUrl("https://speed.cloudflare.com/__down?bytes=60000000"); // 60MB, bị cắt sớm theo thời gian
const QUrl kUploadUrl("https://speed.cloudflare.com/__up");

constexpr int kPingSampleCount = 8;
// Yêu cầu ĐẦU TIÊN phải phân giải DNS + bắt tay TCP + TLS trước khi gửi được gì, nên chậm hơn hẳn các yêu
// cầu sau (dùng lại kết nối). Tính nó vào trung bình/jitter làm cả hai con số phồng lên rõ rệt (mẫu đầu
// vài trăm ms, các mẫu sau vài chục ms). Gửi thêm 1 yêu cầu "khởi động" và KHÔNG ghi nhận nó.
constexpr int kPingWarmupCount = 1;
constexpr int kPingTimeoutMs = 4000;
constexpr int kTransferMaxDurationMs = 9000; // tính từ byte đầu tiên, không tính thời gian bắt tay TCP/TLS
// Thời gian tối đa chờ byte đầu tiên của pha tải xuống/tải lên. m_abortTimer chỉ chạy TỪ byte đầu tiên, nên
// thiếu mốc này thì một máy chủ nhận kết nối rồi im lặng (hoặc mạng rớt giữa lúc bắt tay) làm phép đo treo
// vô hạn ở "Đang đo tốc độ..." - QNetworkAccessManager mặc định không tự hết giờ.
constexpr int kFirstByteTimeoutMs = 10000;
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

    m_firstByteTimer = new QTimer(this);
    m_firstByteTimer->setSingleShot(true);
    m_firstByteTimer->setInterval(kFirstByteTimeoutMs);
    connect(m_firstByteTimer, &QTimer::timeout, this, [this] {
        if (!m_activeReply || m_transferStarted)
            return;
        m_intentionalAbort = false; // hết giờ thật sự = lỗi, không phải dừng có chủ đích
        abortActiveReply();
    });
}

SpeedTestRunner::~SpeedTestRunner()
{
    // abort() phát finished() đồng bộ; các lambda nối với reply sẽ phát tiếp tín hiệu của runner tới giao
    // diện đang bị hủy. Ngắt kết nối tín hiệu trước, chỉ còn việc hủy yêu cầu mạng.
    if (m_activeReply)
    {
        m_activeReply->disconnect(this);
        m_activeReply->abort();
    }
}

SpeedTestRunner::PingStats SpeedTestRunner::computePingStats(const QList<double>& samplesMs)
{
    PingStats stats;
    if (samplesMs.isEmpty())
        return stats;

    stats.minMs = samplesMs.first();
    stats.maxMs = samplesMs.first();
    double sum = 0;
    for (double v : samplesMs)
    {
        stats.minMs = std::min(stats.minMs, v);
        stats.maxMs = std::max(stats.maxMs, v);
        sum += v;
    }
    stats.avgMs = sum / samplesMs.size();

    double jitterSum = 0;
    for (int i = 1; i < samplesMs.size(); ++i)
        jitterSum += std::fabs(samplesMs[i] - samplesMs[i - 1]);
    stats.jitterMs = samplesMs.size() > 1 ? jitterSum / (samplesMs.size() - 1) : 0.0;
    return stats;
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
    // Phải đặt lại: stop() trong pha ping để cờ này = true mà không nơi nào trả về false, nên ở lần đo kế
    // tiếp một LỖI MẠNG THẬT trong pha tải xuống bị coi là "tự dừng có chủ đích" và hiện ra như một kết
    // quả đo (thường là 0) thay vì báo lỗi.
    m_intentionalAbort = false;
    m_transferStarted = false;
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
    m_firstByteTimer->stop();
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

        // Mẫu khởi động (gồm cả DNS/TCP/TLS) không được ghi nhận - xem kPingWarmupCount.
        if (m_pingIndex >= kPingWarmupCount)
        {
            m_pingSamplesMs.push_back(elapsedMs);
            const PingStats stats = computePingStats(m_pingSamplesMs);
            emit pingUpdated(elapsedMs, stats.minMs, stats.avgMs, stats.maxMs, stats.jitterMs);
        }

        ++m_pingIndex;
        if (m_stopRequested)
        {
            finishAll();
        }
        else if (m_pingIndex < kPingWarmupCount + kPingSampleCount)
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
    m_firstByteTimer->start();

    connect(m_activeReply, &QNetworkReply::readyRead, this, [this] {
        if (!m_activeReply)
            return;
        const QByteArray chunk = m_activeReply->readAll();
        m_bytesTransferred += chunk.size();

        if (!m_transferStarted)
        {
            m_transferStarted = true;
            m_firstByteTimer->stop();
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
        m_firstByteTimer->stop();

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
    m_firstByteTimer->start();

    connect(m_activeReply, &QNetworkReply::uploadProgress, this, [this](qint64 bytesSent, qint64) {
        if (bytesSent <= 0)
            return;
        m_bytesTransferred = bytesSent;
        if (!m_transferStarted)
        {
            m_transferStarted = true;
            m_firstByteTimer->stop();
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
        m_firstByteTimer->stop();

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
