#pragma once

#include <QString>

/// Một định dạng/chất lượng khả dụng của video nền tảng (YouTube/Facebook/TikTok...) - lấy từ
/// `yt-dlp --dump-json`'s formats[] (xem YtDlpController.h). Struct thuần.
struct VideoFormatInfo
{
    QString formatId;    ///< format_id thật của yt-dlp - dùng để chọn tải (-f <formatId>)
    QString ext;
    QString resolution;  ///< vd "1920x1080", hoặc "audio only"
    QString note;        ///< format_note thật, vd "1080p", "DASH video"
    qint64 approxSizeBytes{-1}; ///< filesize hoặc filesize_approx, -1 nếu không rõ
    bool hasVideo{false};
    bool hasAudio{false};
    double fps{0};

    /// Mô tả ngắn gọn để hiển thị trong danh sách chọn chất lượng.
    QString displayLabel() const
    {
        QString kind = (hasVideo && hasAudio) ? "Video+Âm thanh" : hasVideo ? "Chỉ video" : hasAudio ? "Chỉ âm thanh" : "?";
        QString label = QString("%1 - %2 (%3)").arg(resolution, ext, kind);
        if (!note.isEmpty())
            label += " - " + note;
        return label;
    }
};
