#pragma once

#include <QWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QStackedWidget>
#include "../model/Action.h"
#include "../capture/MouseCapture.h"
#include "../capture/HotkeyCapture.h"
#include "ui/widgets/DurationInput.h"

class QLabel;

class ActionEditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ActionEditorWidget(QWidget* parent = nullptr);

    void setAction(const Action& action, int actionIndex);
    Action getAction() const;
    void clear();
    /// true nếu đang có thay đổi CHƯA bấm "Lưu hành động (Apply)" - AutoClickWindow dùng để cache lại
    /// (getAction()) trước khi đổi hành động/chuỗi đang xem, không để mất ngầm (xem m_pendingEdits).
    bool isDirty() const { return m_dirty; }
    /// Chỉ số hành động ĐANG NẠP (tham số actionIndex của lần setAction() gần nhất) - DÙNG THAY
    /// ActionListWidget::selectedActionIndex() khi cache chỉnh sửa dở: currentCellChanged của bảng đổi
    /// SANG hàng mới TRƯỚC KHI slot xử lý chạy, nên lúc đó selectedActionIndex() đã là hàng MỚI chứ không
    /// còn là hàng đang sửa dở cần cache - m_currentIndex chỉ đổi khi CHÍNH setAction() chạy, nên luôn đúng
    /// là hàng mà nội dung editor hiện tại THỰC SỰ thuộc về.
    int currentActionIndex() const { return m_currentIndex; }

signals:
    void actionSaved(const Action& action, int actionIndex);
    void captureRequested(int targetField); // 0: Click/Hold; 3: Tổ hợp phím + Click chuột (field 1/2 không còn dùng)
    void dragGestureCaptureRequested(); // bắt trọn thao tác kéo thật (nhấn-kéo-thả), xem DragGestureCapture.h
    /// Bấm "Bắt đầu ghi" ở trang "Ghi nhớ chuỗi" - xem capture/ActionRecorder.h.
    void macroRecordRequested();
    /// Phát khi có thay đổi CHƯA LƯU (dirty=true) hoặc vừa lưu/nạp xong (dirty=false) - AutoClickWindow
    /// dùng để hiện dấu "*" ở dòng tương ứng trong ActionListWidget.
    void dirtyChanged(bool dirty);

public slots:
    void onPositionCaptured(int x, int y);
    void onDragGestureCaptured(int startX, int startY, int endX, int endY);
    /// AutoClickWindow gọi khi ActionRecorder ghi xong (dù bởi nút Dừng hay trần an toàn số sự kiện).
    void onMacroRecorded(const std::vector<RecordedEvent>& events);

protected:
    void hideEvent(QHideEvent* event) override;

private slots:
    void onTypeChanged(int index);
    void onApplyClicked();
    void markDirty();

private:
    void setupUi();
    void connectDirtyTracking();
    void cancelHotkeyCapture();
    void refreshRecordMacroStatusLabel();
    /// Chọn phím `vkCode` trong combo phím; nếu phím đó KHÔNG có trong danh sách hỗ trợ sẵn thì thêm một
    /// mục tạm "(phím khác: ...)" giữ nguyên mã + tên gốc, xem setAction().
    static void selectKeyInCombo(QComboBox* combo, int vkCode, const QString& keyName);
    static QString keyNameFromCombo(const QComboBox* combo);
    QWidget* createClickPage();
    QWidget* createDragPage();
    QWidget* createHoldPage();
    QWidget* createTextPage();
    QWidget* createHotkeyPage();
    QWidget* createKeyPressPage();
    QWidget* createScrollPage();
    QWidget* createRecordedMacroPage();

    int m_currentIndex{-1};
    int m_capturingField{0}; // 0: X,Y (Click/Hold); 3: X,Y (Tổ hợp phím + Click chuột)
    bool m_loadingAction{false}; // true trong lúc setAction() đang nạp giá trị - tránh markDirty() giả
    bool m_dirty{false};
    bool m_loadedEnabled{true}; // Action::enabled của hành động đang sửa - editor không có ô nào cho nó

    QComboBox* m_typeCombo;
    QStackedWidget* m_pagesStack;

    // Timing widgets
    DurationInput* m_waitBeforeInput;
    DurationInput* m_waitAfterInput;
    DurationInput* m_durationInput;

    // Click widgets
    QComboBox* m_clickButtonCombo;
    QSpinBox* m_clickXSpin;
    QSpinBox* m_clickYSpin;
    QPushButton* m_captureClickPosBtn;

    // Drag widgets - 4 ô tọa độ chỉ còn HIỂN THỊ kết quả (đọc-only), nhập bằng nút "Bắt thao tác kéo
    // thật" duy nhất bên dưới (xem createDragPage()).
    QSpinBox* m_dragStartXSpin;
    QSpinBox* m_dragStartYSpin;
    QSpinBox* m_dragEndXSpin;
    QSpinBox* m_dragEndYSpin;
    QPushButton* m_captureDragGestureBtn;

    // Hold widgets
    QComboBox* m_holdButtonCombo;
    QSpinBox* m_holdXSpin;
    QSpinBox* m_holdYSpin;
    QPushButton* m_captureHoldPosBtn;

    // Type text widgets
    QLineEdit* m_textEdit;
    QComboBox* m_textModeCombo;

    // Hotkey (Tổ hợp phím) widgets
    QCheckBox* m_ctrlCheck;
    QCheckBox* m_altCheck;
    QCheckBox* m_shiftCheck;
    QCheckBox* m_winCheck;
    QComboBox* m_hotkeyKeyCombo;
    QPushButton* m_captureHotkeyBtn;
    QLabel* m_hotkeyCaptureStatus;
    HotkeyCapture* m_hotkeyCapture{nullptr};
    // Thứ tự THẬT các modifier đã bấm chọn (tay qua checkbox, hoặc bắt thật qua HotkeyCapture) - nguồn
    // DUY NHẤT ghi vào Action::modOrder lúc Apply (getAction()), xem refreshModifierCheckboxLabels().
    std::vector<ModifierKey> m_hotkeyModOrder;
    // true trong lúc TỰ set giá trị checkbox (từ setAction() hoặc kết quả HotkeyCapture) - chặn
    // onModifierToggled() tự ý thêm/xóa khỏi m_hotkeyModOrder (nơi gọi đã tự quản lý list đó), nhưng
    // KHÔNG chặn markDirty() (connect toggled->markDirty chạy độc lập, không dùng cờ này) - khác
    // m_loadingAction (chặn CẢ HAI) vì lúc áp kết quả bắt tổ hợp thật VẪN phải tính là có thay đổi.
    bool m_syncingModifierChecks{false};
    void refreshModifierCheckboxLabels(); // đổi text mỗi checkbox đang check thành "Ctrl (1)" theo vị trí trong m_hotkeyModOrder
    void onModifierToggled(ModifierKey key, bool checked);

    // "Kết thúc bằng" của Tổ hợp phím: Phím chính (mặc định, trang 0 dùng m_hotkeyKeyCombo ở trên) / Cuộn
    // chuột (trang 1) / Click chuột (trang 2) - xem Action::HotkeyTrigger.
    QComboBox* m_hotkeyTriggerCombo;
    QStackedWidget* m_hotkeyTriggerStack;
    QComboBox* m_hotkeyScrollDirectionCombo;
    QSpinBox* m_hotkeyScrollAmountSpin;
    QComboBox* m_hotkeyClickButtonCombo;
    QSpinBox* m_hotkeyClickXSpin;
    QSpinBox* m_hotkeyClickYSpin;

    // Key Press widgets
    QComboBox* m_keyPressCombo;

    // Scroll widgets
    QComboBox* m_scrollDirectionCombo;
    QSpinBox* m_scrollAmountSpin;

    // Ghi nhớ chuỗi (RecordedMacro) widgets - không có control nào cho TỪNG sự kiện, chỉ 1 nút ghi + 1
    // label trạng thái; dữ liệu thật nằm trong m_recordedEvents, xem onMacroRecorded()/setAction()/getAction().
    QPushButton* m_recordMacroBtn;
    QLabel* m_recordMacroStatusLabel;
    std::vector<RecordedEvent> m_recordedEvents;

    QPushButton* m_applyButton;
};
