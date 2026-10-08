#pragma once

#include <QObject>
#include <QString>

class FileDownloader;

/// Tải về + khởi chạy trình cài đặt bản mới (OneForAll_Setup.exe) - dùng lại FileDownloader đã có sẵn
/// (hỗ trợ tiếp tục tải dở qua HTTP Range, đã test kỹ ở Downloader) thay vì viết lại logic tải HTTP.
/// Sau khi tải xong, khởi chạy trình cài đặt ở chế độ ÂM THẦM (/VERYSILENT) kèm /CLOSEAPPLICATIONS +
/// /RESTARTAPPLICATIONS - Windows Restart Manager tự phát hiện OneForAll.exe đang chạy (qua AppMutex
/// khai báo trong installer/OneForAll.iss), tự đóng rồi tự khởi động lại sau khi cài xong - đúng hành vi
/// "tự cập nhật như một sản phẩm bình thường" mà không cần người dùng bấm Next/Finish.
class UpdateInstaller : public QObject
{
    Q_OBJECT

public:
    explicit UpdateInstaller(QObject* parent = nullptr);
    ~UpdateInstaller() override;

    void downloadAndInstall(const QString& downloadUrl);

signals:
    /// bytesTotal có thể là 0 nếu GitHub không trả kích thước trước - UI nên hiện dạng không xác định.
    void progress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFailed(QString error);
    /// Phát ra NGAY SAU KHI đã khởi chạy thành công tiến trình cài đặt (QProcess::startDetached) - nơi
    /// gọi PHẢI tự đóng ứng dụng (qApp->quit()) ngay khi nhận tín hiệu này để trình cài đặt ghi đè được
    /// các file đang bị chính OneForAll.exe giữ (nếu không tự đóng, /CLOSEAPPLICATIONS của Restart
    /// Manager vẫn xử lý được, nhưng tự đóng gọn gàng hơn và tránh cửa sổ "đang đóng..." xuất hiện).
    void aboutToRestart();

private:
    FileDownloader* m_downloader{nullptr};
    QString m_installerPath;
};
