#pragma once

#include <QList>
#include <QString>

/// Một thứ TẢI ĐƯỢC mà bộ phân giải link (LinkResolver) tìm ra từ một địa chỉ người dùng dán vào - struct
/// thuần, không phụ thuộc UI/mạng.
struct ResolvedMedia
{
    /// Cách tải: HttpFile = tải thẳng một tệp qua HTTP(S) (FileDownloader); YtDlp = giao cho yt-dlp.exe (video
    /// nền tảng, luồng HLS .m3u8/.mpd, bài đăng...).
    enum class Kind { HttpFile, YtDlp };

    Kind kind{Kind::HttpFile};
    QString url;         ///< HttpFile: URL tệp trực tiếp. YtDlp: URL trang/luồng để yt-dlp xử lý.
    QString title;       ///< Tiêu đề hiển thị (có thể rỗng).
    QString fileName;    ///< Tên tệp gợi ý (đã làm sạch; rỗng = tự suy ra lúc tải).
    QString mediaType;   ///< "Ảnh" / "Video" / "Âm thanh" / "Tài liệu" / "Tệp".
    qint64 size{-1};     ///< -1 = chưa biết.
    QString contentType;
    QString referer;     ///< Referer cần gửi khi tải (thường là trang nguồn) - nhiều CDN chặn nếu thiếu.
    QString note;        ///< Nguồn tìm ra, vd "tệp trực tiếp", "yt-dlp (Facebook)", "mã nguồn trang", "trình duyệt ẩn".
};

/// Kết quả một lần phân giải: hoặc có `items` (thành công, error rỗng) hoặc `error` (không ra được tệp nào).
struct ResolveResult
{
    QString sourceUrl;
    QList<ResolvedMedia> items;
    QString error;            ///< Lý do ĐỌC ĐƯỢC cho người dùng khi không có item nào (hiện ở cột "ERR").
    bool isCollection{false}; ///< Hồ sơ/kênh/danh sách phát (nhiều bài đăng).
    bool truncated{false};    ///< Danh sách bị cắt theo giới hạn số mục.
    QString warning;          ///< Lưu ý khi vẫn có kết quả nhưng CHƯA ĐẦY ĐỦ (vd hồ sơ chỉ lấy được phần công khai).

    bool ok() const { return error.isEmpty() && !items.isEmpty(); }
};
