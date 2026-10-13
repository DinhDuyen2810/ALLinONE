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
    Scroll,
    /// "Ghi nhớ chuỗi" - ghi lại THẬT thao tác chuột+bàn phím của người dùng (xem capture/ActionRecorder.h)
    /// rồi phát lại nguyên vẹn. PHẢI ở CUỐI enum: ActionEditorWidget dùng static_cast<int>(type) làm chỉ
    /// số cho cả m_typeCombo lẫn m_pagesStack, thêm ở giữa sẽ làm lệch mọi giá trị đã lưu trong hồ sơ cũ.
    RecordedMacro
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

/// Phím bổ trợ của Tổ hợp phím - tách khỏi 4 bool modCtrl/Alt/Shift/Win cũ để biểu diễn được THỨ TỰ người
/// dùng đã bấm chọn (xem Action::modOrder bên dưới).
enum class ModifierKey
{
    Ctrl,
    Alt,
    Shift,
    Win
};

/// ActionType::Hotkey (đổi tên hiển thị thành "Tổ hợp phím") kết thúc bằng gì sau khi giữ các modifier -
/// trước đây CHỈ có thể là 1 phím chính; giờ thêm Cuộn chuột/Click chuột (vd Ctrl+Cuộn lên = zoom in,
/// yêu cầu người dùng v1.19.7). Scroll dùng lại scrollDirection/scrollAmount (đã có sẵn, dùng chung với
/// ActionType::Scroll); Click dùng lại x/y/mouseButton (đã có sẵn, dùng chung với ActionType::MouseClick)
/// - không thêm field tọa độ/hướng cuộn riêng.
enum class HotkeyTrigger
{
    KeyPress = 0,
    Scroll = 1,
    Click = 2
};

/// Một sự kiện THÔ trong chuỗi đã ghi (ActionType::RecordedMacro) - đúng những gì hook WH_MOUSE_LL/
/// WH_KEYBOARD_LL bắt được, theo thứ tự, kèm khoảng chờ tới sự kiện kế tiếp để phát lại đúng nhịp.
enum class RecordedEventType
{
    MouseMove,
    MouseDown,
    MouseUp,
    MouseWheel,
    KeyDown,
    KeyUp
};

struct RecordedEvent
{
    RecordedEventType type{RecordedEventType::MouseMove};
    /// Thời gian chờ kể từ sự kiện NGAY TRƯỚC nó trong cùng chuỗi (không phải từ lúc bắt đầu ghi) - phát
    /// lại chỉ cần chờ đúng khoảng này rồi thực hiện, xem InputController::replayRecordedEvents().
    qint64 delayMs{0};
    int x{0};  // MouseMove/MouseDown/MouseUp - tọa độ vật lý (giống Action::x/y)
    int y{0};
    MouseButtonType button{MouseButtonType::Left}; // MouseDown/MouseUp
    int wheelDelta{0};           // MouseWheel - bội số WHEEL_DELTA (120), dấu = hướng
    bool wheelHorizontal{false}; // MouseWheel - true nếu là cuộn ngang
    int keyCode{0};              // KeyDown/KeyUp - mã VK
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
    /// Thứ tự THẬT người dùng đã bấm chọn các modifier (tự chọn tay qua UI hoặc bắt tổ hợp thật qua
    /// HotkeyCapture) - RỖNG nghĩa là CHƯA BIẾT thứ tự (hồ sơ JSON cũ trước v1.19.7, hoặc Action dựng tay
    /// chỉ gán 4 bool ở trên, như demo chain/test). KHÔNG đọc field này trực tiếp - luôn gọi
    /// effectiveModOrder(). Luôn đồng bộ với 4 bool ở trên qua setModifiers(), không tự gán rời rạc.
    std::vector<ModifierKey> modOrder;
    /// Chỉ có ý nghĩa khi type==Hotkey - xem enum HotkeyTrigger.
    HotkeyTrigger hotkeyTrigger{HotkeyTrigger::KeyPress};

    // Scroll
    ScrollDirection scrollDirection{ScrollDirection::Down};
    int scrollAmount{5};

    /// Chỉ có ý nghĩa khi type==RecordedMacro - dãy sự kiện thô đã ghi, phát lại đúng thứ tự/nhịp.
    std::vector<RecordedEvent> recordedEvents;

    /// Gán modOrder VÀ 4 bool modCtrl/Alt/Shift/Win CÙNG LÚC, luôn khớp nhau - dùng ở MỌI nơi gán tổ hợp
    /// phím (UI tự chọn tay, HotkeyCapture bắt thật). Không gán rời rạc 4 bool ở nơi khác.
    void setModifiers(const std::vector<ModifierKey>& order);
    /// modOrder nếu KHÔNG rỗng (nguồn thật); ngược lại SUY LUẬN lại thứ tự cố định Ctrl→Alt→Shift→Win từ
    /// 4 bool cũ - tương thích hồ sơ JSON cũ (chưa có modOrder) và mọi Action dựng tay chỉ gán 4 bool,
    /// hành vi y hệt trước v1.19.7. MỌI nơi đọc thứ tự (description(), ActionRunner, Serializer lúc ghi)
    /// phải gọi hàm này, không bao giờ đọc modOrder trực tiếp.
    std::vector<ModifierKey> effectiveModOrder() const;

    // Helper functions
    QString typeName() const;
    QString description() const;
    /// Như description() nhưng AN TOÀN ĐỂ GHI LOG: hành động "Gõ văn bản" chỉ ghi SỐ KÝ TỰ, không ghi
    /// nội dung - nội dung gõ thường là mật khẩu/mã đăng nhập (xem mục 72 tài liệu thiết kế), trước đây
    /// bị ghi nguyên văn vào logs/autoclick.log ở MỖI hành động của MỖI vòng lặp.
    QString logDescription() const;
};
