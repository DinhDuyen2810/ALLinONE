#pragma once

#include <QString>

/// Một mục trong hàng đợi tải trực tiếp (Direct Download/quét trang web) - struct thuần, không phụ
/// thuộc UI/mạng. Khác với tải video nền tảng (yt-dlp) - đây là tải HTTP(S) thẳng một tệp.
struct DownloadItem
{
    enum class Status { Queued, Downloading, Paused, Completed, Failed, Canceled };

    QString url;
    QString destPath;      ///< Đường dẫn đầy đủ nơi lưu tệp
    Status status{Status::Queued};
    qint64 totalBytes{-1};    ///< -1 = chưa biết (server không trả Content-Length)
    qint64 receivedBytes{0};
    qint64 speedBytesPerSec{0};
    QString error;

    QString fileName() const
    {
        const int slash = destPath.lastIndexOf('/');
        return slash >= 0 ? destPath.mid(slash + 1) : destPath;
    }

    static QString statusLabel(Status s)
    {
        switch (s)
        {
            case Status::Queued: return "Đang chờ";
            case Status::Downloading: return "Đang tải";
            case Status::Paused: return "Tạm dừng";
            case Status::Completed: return "Hoàn tất";
            case Status::Failed: return "Thất bại";
            case Status::Canceled: return "Đã hủy";
        }
        return {};
    }
};
