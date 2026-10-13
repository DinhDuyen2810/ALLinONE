#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <set>
#include <vector>

#include "../model/Action.h"

class QTimer;
class RuntimeOverlay;

/// Ghi lại TOÀN BỘ thao tác chuột+bàn phím THẬT của người dùng trên toàn máy (không giới hạn 1 cửa sổ) -
/// cài ĐỒNG THỜI WH_MOUSE_LL + WH_KEYBOARD_LL, CHỈ QUAN SÁT (không bao giờ nuốt sự kiện), theo đúng khuôn
/// DragGestureCapture.h - để thao tác thật của người dùng vẫn tác động lên màn hình như bình thường trong
/// lúc ứng dụng đã tự ẩn đi. Dùng cho ActionType::RecordedMacro ("Ghi nhớ chuỗi").
///
/// KHÁC mọi capture khác trong thư mục này:
///  - Esc KHÔNG hủy ghi - là phím hợp lệ người dùng có thể cần ghi lại (vd "nhấn Esc để đóng hộp thoại").
///    Dừng ghi CHỈ qua stop() (nút trên minibox, hoặc phím tắt dừng toàn cục StopHotkey qua AutoClickWindow).
///  - KHÔNG timeout ngắn (ghi có thể kéo dài nhiều phút) - chỉ có trần an toàn kEventCountSafetyLimit để
///    tự dừng nếu người dùng quên, tránh phình bộ nhớ/JSON vô hạn.
class ActionRecorder : public QObject
{
    Q_OBJECT

public:
    explicit ActionRecorder(QObject* parent = nullptr);
    ~ActionRecorder() override;

    /// Trả về false nếu KHÔNG cài được hook chuột (không có gì để ghi) - nơi gọi phải tự hiện lại cửa sổ
    /// và báo lỗi, không có tín hiệu nào phát ra trong trường hợp này. `excludedOverlay` (có thể null):
    /// vị trí hiện tại của cửa sổ này (minibox) được đọc LẠI ở MỖI sự kiện chuột (không chụp 1 lần, vì nó
    /// tự di chuyển để né chuột trong lúc ghi) - rơi vào đó thì KHÔNG ghi, nhưng sự kiện vẫn đi tiếp bình
    /// thường để nút Dừng trên minibox vẫn bấm được.
    bool start(RuntimeOverlay* excludedOverlay);
    /// Dừng ngay: tự chèn sự kiện nhả cho mọi phím/nút còn đang giữ dở (không để kẹt), rồi phát
    /// recordingFinished() QUA HÀNG ĐỢI sự kiện.
    void stop();
    bool isRecording() const { return m_mouseHook != nullptr; }

signals:
    /// Phát mỗi ~200ms trong lúc ghi (QTimer nội bộ) - cho minibox cập nhật số bước + thời gian đã ghi.
    /// Minibox lúc ghi ĐỨNG YÊN một chỗ cố định (yêu cầu người dùng - né chuột lúc ghi sẽ khiến nút "Kết
    /// thúc ghi" thành mục tiêu di động không bao giờ bấm trúng), nên tín hiệu này KHÔNG mang theo vị trí
    /// chuột - khác hẳn lúc CHẠY chuỗi (ActionRunner::actionStarted mang theo targetX/Y để né).
    void progressUpdated(int stepCount, qint64 elapsedMs);
    void recordingFinished(std::vector<RecordedEvent> events);

public:
    // Chỉ dành cho callback Win32 tĩnh trong .cpp gọi (xem DragGestureCapture.h cho quy ước tương tự).
    void onRawMouse(int message, long x, long y, short wheelDelta);
    void onRawKey(int vkCode, bool pressed);

private:
    void uninstallHook();
    void appendEvent(RecordedEventType type, int x, int y, MouseButtonType button, int wheelDelta,
                     bool wheelHorizontal, int keyCode);
    bool isInsideExcludedOverlay(int physicalX, int physicalY) const;
    void finish();

    void* m_mouseHook{nullptr};
    void* m_keyboardHook{nullptr};
    RuntimeOverlay* m_excludedOverlay{nullptr};

    std::vector<RecordedEvent> m_events;
    QElapsedTimer m_elapsed;
    qint64 m_lastEventMs{0};         // mốc (m_elapsed.elapsed()) của sự kiện VỪA ghi - tính delayMs cho sự kiện kế
    qint64 m_lastMoveThrottleMs{-1000}; // mốc MouseMove gần nhất ĐƯỢC GHI - giảm tải, không liên quan delayMs
    long m_lastKnownX{0};
    long m_lastKnownY{0};

    std::set<int> m_keysDown;
    std::set<MouseButtonType> m_buttonsDown;

    QTimer* m_progressTimer{nullptr};
};
