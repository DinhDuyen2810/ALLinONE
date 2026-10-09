#pragma once

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

/**
 * @brief Đo tốc độ mạng thực tế (ping, jitter, tải xuống, tải lên) qua HTTP(S).
 *
 * Dùng các endpoint đo tốc độ công khai, không cần khóa API của Cloudflare
 * (chính là backend mà https://speed.cloudflare.com dùng):
 *   - GET  https://speed.cloudflare.com/__down?bytes=N  (đo ping bằng N=0, đo tải xuống bằng N lớn)
 *   - POST https://speed.cloudflare.com/__up             (đo tải lên)
 *
 * Chỉ dùng Qt Network (QNetworkAccessManager), không gọi Win32 - độc lập máy nào cũng chạy được
 * miễn có mạng Internet.
 */
class SpeedTestRunner : public QObject
{
    Q_OBJECT

public:
    explicit SpeedTestRunner(QObject* parent = nullptr);
    ~SpeedTestRunner() override;

    void start();
    void stop();
    bool isRunning() const { return m_running; }
    /// Lần đo gần nhất kết thúc do người dùng bấm dừng (stop()) chứ không phải chạy hết - giao diện dùng để
    /// KHÔNG hiện "Hoàn tất" cho một phép đo bị bỏ dở. Có giá trị từ lúc finished() phát tới lần start() sau.
    bool wasStoppedByUser() const { return m_stopRequested; }

    struct PingStats
    {
        double minMs{0};
        double avgMs{0};
        double maxMs{0};
        double jitterMs{0}; // trung bình độ lệch giữa hai mẫu liên tiếp
    };
    /// Thống kê trên các mẫu ping ĐÃ loại mẫu khởi động (xem kPingWarmupCount trong .cpp). Thuần tính toán.
    static PingStats computePingStats(const QList<double>& samplesMs);

signals:
    void phaseChanged(QString phase);
    void pingUpdated(double lastMs, double minMs, double avgMs, double maxMs, double jitterMs);
    void downloadProgress(double currentMbps, qint64 bytesReceived);
    void downloadFinished(double mbps);
    void uploadProgress(double currentMbps, qint64 bytesSent);
    void uploadFinished(double mbps);
    void finished();
    void errorOccurred(QString message);

private:
    void runPingPhase();
    void sendNextPing();
    void runDownloadPhase();
    void runUploadPhase();
    void abortActiveReply();
    void finishAll();

    QNetworkAccessManager* m_nam;
    QNetworkReply* m_activeReply{nullptr};
    QTimer* m_abortTimer{nullptr};
    QTimer* m_firstByteTimer{nullptr}; // hết giờ chờ byte ĐẦU TIÊN của pha tải xuống/tải lên

    bool m_running{false};
    bool m_stopRequested{false};
    bool m_intentionalAbort{false}; // true khi tự abort do hết thời gian tối đa (không phải lỗi)

    // Ping
    int m_pingIndex{0};
    QList<double> m_pingSamplesMs;
    QElapsedTimer m_pingTimer;

    // Download / Upload
    QElapsedTimer m_transferTimer;
    qint64 m_bytesTransferred{0};
    bool m_transferStarted{false};
};
