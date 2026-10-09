#pragma once

#include "../model/VideoInfo.h"

#include <QString>
#include <QStringList>
#include <QThread>
#include <atomic>

/// Lấy thông tin video (`yt-dlp --dump-json --simulate`) trên QThread riêng - đây là lệnh gọi MẠNG THẬT
/// (hỏi máy chủ YouTube/Facebook/... về metadata), có thể mất vài giây, không được chặn luồng giao diện.
///
/// HỦY ĐƯỢC giữa chừng qua requestCancel(): run() kiểm tra cờ mỗi 200ms, tự kết thúc yt-dlp.exe (kèm mọi
/// tiến trình con của nó) rồi thoát. Nơi sở hữu PHẢI requestCancel() + wait() trước khi hủy đối tượng -
/// bản trước chỉ wait(5000) trong khi yt-dlp được chờ tới 45 giây, tức có thể hủy một QThread còn đang
/// chạy (hành vi KHÔNG XÁC ĐỊNH theo tài liệu Qt, cùng lớp lỗi đã sửa ở DefenderScanWorker).
class YtDlpInfoWorker : public QThread
{
    Q_OBJECT

public:
    explicit YtDlpInfoWorker(QObject* parent = nullptr);

    /// Gọi TRƯỚC start() cho mỗi lần lấy thông tin - đồng thời xóa cờ hủy của lần trước (đối tượng được
    /// tái dùng; xóa ở đây chứ không phải đầu run() để không nuốt một requestCancel() gọi ngay sau start()).
    void setUrl(const QString& url)
    {
        m_url = url;
        m_cancelRequested = false;
    }

    /// An toàn gọi từ luồng khác. Kết quả vẫn báo qua infoFetched(false, {}, "Đã hủy.").
    void requestCancel() { m_cancelRequested = true; }

signals:
    /// Đổi tên khỏi `finished` - trùng tín hiệu có sẵn của QThread (cùng bài học đã áp dụng cho
    /// CleanupExecutor/VpnConnector/DefenderScanWorker).
    void infoFetched(bool ok, VideoInfo info, QString error);

protected:
    void run() override;

private:
    QString m_url;
    std::atomic_bool m_cancelRequested{false};
};

namespace YtDlpInfoWorkerInternal
{
/// Phân tích JSON thật của yt-dlp (một dòng `--dump-json`) - thuần, test được bằng dữ liệu mẫu.
VideoInfo parseInfoJson(const QByteArray& json, QString* error);

/// Đối số dòng lệnh cho lượt lấy thông tin - thuần, test được (xem ghi chú về "--"/--ignore-config ở
/// YtDlpDownloadWorkerInternal::buildArguments).
QStringList buildArguments(const QString& url);
} // namespace YtDlpInfoWorkerInternal
