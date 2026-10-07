#pragma once

#include "VideoFormatInfo.h"

#include <QList>
#include <QString>

/// Thông tin một video lấy từ `yt-dlp --dump-json` (xem YtDlpController.h) - struct thuần.
struct VideoInfo
{
    QString title;
    double durationSeconds{0};
    QString thumbnailUrl;
    QList<VideoFormatInfo> formats; ///< Theo đúng thứ tự yt-dlp trả về: xấu nhất -> tốt nhất

    QString durationLabel() const
    {
        const int total = static_cast<int>(durationSeconds);
        const int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
        return h > 0 ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
                     : QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
    }
};
