#pragma once

#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QPainterPath>

/**
 * @brief Hỗ trợ bo tròn và tạo nền trắng cho toàn bộ icon công cụ.
 * Đảm bảo các icon không có nền (trong suốt) được hiển thị sắc nét,
 * đồng thời thống nhất kích thước và góc bo tròn hiện đại cho toàn hệ thống.
 */
class IconHelper
{
public:
    static QPixmap makeBadgedPixmap(const QString& iconPath, int size = 48, int radius = 10, int padding = 4)
    {
        QPixmap src(iconPath);
        if (src.isNull())
        {
            return QPixmap();
        }

        QPixmap dest(size, size);
        dest.fill(Qt::transparent);

        QPainter p(&dest);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);

        // Vẽ nền trắng bo tròn góc
        QPainterPath path;
        path.addRoundedRect(QRectF(0.5, 0.5, size - 1, size - 1), radius, radius);
        p.fillPath(path, Qt::white);

        // Giới hạn vùng vẽ (clip) theo hình bo tròn
        p.setClipPath(path);

        // Vẽ ảnh ở trung tâm với khoảng cách viền padding
        int actualPadding = padding;
        if (actualPadding < 0) actualPadding = 0;
        QRect targetRect(actualPadding, actualPadding, size - 2 * actualPadding, size - 2 * actualPadding);
        p.drawPixmap(targetRect, src.scaled(targetRect.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));

        // Bỏ clip để vẽ viền mảnh tinh tế bao quanh badge
        p.setClipping(false);
        p.setPen(QPen(QColor(208, 215, 222), 1));
        p.drawPath(path);
        p.end();

        return dest;
    }

    static QIcon makeBadgedIcon(const QString& iconPath, int size = 48, int radius = 10, int padding = 4)
    {
        return QIcon(makeBadgedPixmap(iconPath, size, radius, padding));
    }
};
