#include "EdgeDetector.h"

#include <algorithm>

ScreenSide EdgeDetector::detectCrossing(const QPoint& cursor, const QRect& localBounds,
                                        const QSet<ScreenSide>& sidesWithNeighbor)
{
    if (localBounds.isEmpty())
        return ScreenSide::None;

    if (cursor.x() <= localBounds.left() && sidesWithNeighbor.contains(ScreenSide::Left))
        return ScreenSide::Left;
    if (cursor.x() >= localBounds.right() && sidesWithNeighbor.contains(ScreenSide::Right))
        return ScreenSide::Right;
    if (cursor.y() <= localBounds.top() && sidesWithNeighbor.contains(ScreenSide::Top))
        return ScreenSide::Top;
    if (cursor.y() >= localBounds.bottom() && sidesWithNeighbor.contains(ScreenSide::Bottom))
        return ScreenSide::Bottom;

    return ScreenSide::None;
}

double EdgeDetector::normalizedPositionAlongEdge(const QPoint& cursor, const QRect& localBounds, ScreenSide side)
{
    if (localBounds.isEmpty())
        return 0.5;

    double v = 0.5;
    switch (side)
    {
        case ScreenSide::Left:
        case ScreenSide::Right:
            v = localBounds.height() > 0
                    ? static_cast<double>(cursor.y() - localBounds.top()) / localBounds.height()
                    : 0.5;
            break;
        case ScreenSide::Top:
        case ScreenSide::Bottom:
            v = localBounds.width() > 0 ? static_cast<double>(cursor.x() - localBounds.left()) / localBounds.width()
                                        : 0.5;
            break;
        default:
            return 0.5;
    }
    return std::clamp(v, 0.0, 1.0);
}

QPoint EdgeDetector::entryPoint(const QRect& localBounds, ScreenSide incomingFromSide, double normalizedPosition)
{
    const double p = std::clamp(normalizedPosition, 0.0, 1.0);
    constexpr int kInset = 3; // lùi vào trong vài pixel để tránh kích hoạt chuyển biên ngược lại ngay lập tức

    switch (incomingFromSide)
    {
        case ScreenSide::Left:
            return QPoint(localBounds.left() + kInset,
                         localBounds.top() + static_cast<int>(p * localBounds.height()));
        case ScreenSide::Right:
            return QPoint(localBounds.right() - kInset,
                         localBounds.top() + static_cast<int>(p * localBounds.height()));
        case ScreenSide::Top:
            return QPoint(localBounds.left() + static_cast<int>(p * localBounds.width()), localBounds.top() + kInset);
        case ScreenSide::Bottom:
            return QPoint(localBounds.left() + static_cast<int>(p * localBounds.width()),
                         localBounds.bottom() - kInset);
        default:
            return localBounds.center();
    }
}
