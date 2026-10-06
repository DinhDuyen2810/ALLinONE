#pragma once

#include <QString>

/// Vị trí của một máy lân cận so với máy hiện tại trong lưới bố trí (giống Mouse without Borders).
enum class ScreenSide
{
    None = 0,
    Left,
    Right,
    Top,
    Bottom
};

inline ScreenSide oppositeSide(ScreenSide s)
{
    switch (s)
    {
        case ScreenSide::Left:   return ScreenSide::Right;
        case ScreenSide::Right:  return ScreenSide::Left;
        case ScreenSide::Top:    return ScreenSide::Bottom;
        case ScreenSide::Bottom: return ScreenSide::Top;
        default:                 return ScreenSide::None;
    }
}

inline QString screenSideName(ScreenSide s)
{
    switch (s)
    {
        case ScreenSide::Left:   return "Trái";
        case ScreenSide::Right:  return "Phải";
        case ScreenSide::Top:    return "Trên";
        case ScreenSide::Bottom: return "Dưới";
        default:                 return "Không có";
    }
}
