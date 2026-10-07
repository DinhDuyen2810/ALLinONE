#pragma once

#include <QPoint>
#include <QRect>
#include <QSet>

#include "../model/ScreenSide.h"

/**
 * @brief Toán học thuần túy xác định khi nào chuột "chạm biên" để chuyển quyền điều khiển sang máy
 * lân cận, và điểm chuột xuất hiện ở đâu trên màn hình máy nhận. Không phụ thuộc Win32/Qt GUI thật sự
 * đang chạy - chỉ làm việc trên QRect/QPoint nên test độc lập được.
 */
namespace EdgeDetector
{
/// Trả về hướng đã chạm/vượt biên có láng giềng gán sẵn, None nếu con trỏ còn nằm trong vùng an toàn.
ScreenSide detectCrossing(const QPoint& cursor, const QRect& localBounds, const QSet<ScreenSide>& sidesWithNeighbor);

/// Vị trí dọc theo biên đã chạm, chuẩn hóa 0.0 (trên/trái cùng) .. 1.0 (dưới/phải cùng).
double normalizedPositionAlongEdge(const QPoint& cursor, const QRect& localBounds, ScreenSide side);

/// Điểm đặt con trỏ khi NHẬN quyền điều khiển, đi vào từ 'incomingFromSide' của localBounds, tại vị trí
/// chuẩn hóa đã nhận được từ máy gửi. Lùi vài pixel vào trong để không lập tức kích hoạt chuyển ngược lại.
QPoint entryPoint(const QRect& localBounds, ScreenSide incomingFromSide, double normalizedPosition);
}
