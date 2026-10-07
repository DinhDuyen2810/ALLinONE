#pragma once

#include "../model/DownloadItem.h"

#include <QHash>
#include <QObject>

class QNetworkAccessManager;
class QNetworkReply;

/// Tải trực tiếp một hoặc nhiều tệp qua HTTP(S) - thuần Qt Network (QNetworkAccessManager/QNetworkReply),
/// chạy KHÔNG CẦN QThread vì bản thân đã bất đồng bộ qua tín hiệu Qt (đúng mẫu đã dùng cho
/// PublicIpChecker/SpeedTestRunner - QNetworkReply tự xử lý I/O ngầm, không chặn luồng giao diện).
/// Hỗ trợ TIẾP TỤC tải dở (HTTP Range) nếu tệp đích đã tồn tại một phần và server hỗ trợ Range (206
/// Partial Content); nếu server bỏ qua Range (trả 200 OK như tải mới), tự động tải lại từ đầu.
class FileDownloader : public QObject
{
    Q_OBJECT

public:
    explicit FileDownloader(QObject* parent = nullptr);
    ~FileDownloader() override;

    /// Thêm một tệp vào hàng đợi (chưa tải ngay - gọi start() để bắt đầu/tiếp tục). Trả về ID duy nhất
    /// để theo dõi/điều khiển mục này về sau.
    int enqueue(const QString& url, const QString& destPath);

    void start(int id);
    void pause(int id);
    void cancel(int id);

    /// Số lượng tải đồng thời tối đa (mặc định 3) - các mục còn lại trong hàng đợi tự động bắt đầu khi
    /// có "chỗ trống" (một tải khác hoàn tất/thất bại/bị hủy).
    void setMaxConcurrent(int n) { m_maxConcurrent = n; }
    void startAllQueued();

    const DownloadItem* item(int id) const;
    QList<int> allIds() const { return m_order; }

signals:
    void itemUpdated(int id);
    void itemFinished(int id, bool success);

private:
    void startNetworkRequest(int id);
    void tryStartNextQueued();

    QNetworkAccessManager* m_nam{nullptr};
    QHash<int, DownloadItem> m_items;
    QHash<int, QNetworkReply*> m_activeReplies;
    QHash<int, QString> m_tempWritePaths; // đường dẫn thật đang ghi (có thể khác destPath nếu cần)
    QList<int> m_order;
    int m_nextId{1};
    int m_maxConcurrent{3};
};
