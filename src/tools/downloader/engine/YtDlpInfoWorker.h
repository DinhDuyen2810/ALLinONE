#pragma once

#include "../model/VideoInfo.h"

#include <QString>
#include <QThread>

/// Lấy thông tin video (`yt-dlp --dump-json --simulate`) trên QThread riêng - đây là lệnh gọi MẠNG THẬT
/// (hỏi máy chủ YouTube/Facebook/... về metadata), có thể mất vài giây, không được chặn luồng giao diện.
class YtDlpInfoWorker : public QThread
{
    Q_OBJECT

public:
    explicit YtDlpInfoWorker(QObject* parent = nullptr);

    void setUrl(const QString& url) { m_url = url; }

signals:
    /// Đổi tên khỏi `finished` - trùng tín hiệu có sẵn của QThread (cùng bài học đã áp dụng cho
    /// CleanupExecutor/VpnConnector/DefenderScanWorker).
    void infoFetched(bool ok, VideoInfo info, QString error);

protected:
    void run() override;

private:
    QString m_url;
};

namespace YtDlpInfoWorkerInternal
{
/// Phân tích JSON thật của yt-dlp (một dòng `--dump-json`) - thuần, test được bằng dữ liệu mẫu.
VideoInfo parseInfoJson(const QByteArray& json, QString* error);
} // namespace YtDlpInfoWorkerInternal
