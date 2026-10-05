#pragma once

#include <chrono>
#include <string>
#include <vector>
#include <QString>

enum class ActionType
{
    MouseClick,
    MouseDrag,
    MouseHold,
    TypeText,
    Hotkey,
    KeyPress,
    Scroll
};

enum class MouseButtonType
{
    Left = 0,
    Right = 1,
    Middle = 2
};

enum class TextTypeMode
{
    Instant = 0,
    CharByChar = 1
};

enum class ScrollDirection
{
    Down = 0,
    Up = 1,
    Left = 2,
    Right = 3
};

struct Action
{
    ActionType type{ActionType::MouseClick};

    std::chrono::milliseconds waitBefore{0};
    std::chrono::milliseconds waitAfter{500};
    std::chrono::milliseconds duration{0};

    bool enabled{true};

    // Mouse coordinates
    int x{500};
    int y{300};
    int startX{100};
    int startY{100};
    int endX{500};
    int endY{500};
    MouseButtonType mouseButton{MouseButtonType::Left};

    // Keyboard & Text
    std::string text{"hello"};
    TextTypeMode textMode{TextTypeMode::Instant};
    int keyCode{13}; // Default to Enter (VK_RETURN)
    std::string keyName{"ENTER"};
    bool modCtrl{false};
    bool modAlt{false};
    bool modShift{false};
    bool modWin{false};

    // Scroll
    ScrollDirection scrollDirection{ScrollDirection::Down};
    int scrollAmount{5};

    // Helper functions
    QString typeName() const;
    QString description() const;
};
