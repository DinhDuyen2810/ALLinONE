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

class QLabel;

class ActionEditorWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ActionEditorWidget(QWidget* parent = nullptr);

    void setAction(const Action& action, int actionIndex);
    Action getAction() const;
    void clear();

signals:
    void actionSaved(const Action& action, int actionIndex);
    void captureRequested(int targetField); // 0: single pos, 1: drag start, 2: drag end
    void dragGestureCaptureRequested(); // bắt trọn thao tác kéo thật (nhấn-kéo-thả), xem DragGestureCapture.h
    /// Phát khi có thay đổi CHƯA LƯU (dirty=true) hoặc vừa lưu/nạp xong (dirty=false) - AutoClickWindow
    /// dùng để hiện dấu "*" ở dòng tương ứng trong ActionListWidget.
    void dirtyChanged(bool dirty);

public slots:
    void onPositionCaptured(int x, int y);
    void onDragGestureCaptured(int startX, int startY, int endX, int endY);

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

    int m_currentIndex{-1};
    int m_capturingField{0}; // 0: X,Y; 1: startX,startY; 2: endX,endY
    bool m_loadingAction{false}; // true trong lúc setAction() đang nạp giá trị - tránh markDirty() giả
    bool m_dirty{false};
    bool m_loadedEnabled{true}; // Action::enabled của hành động đang sửa - editor không có ô nào cho nó

    QComboBox* m_typeCombo;
    QStackedWidget* m_pagesStack;

    // Timing widgets
    QSpinBox* m_waitBeforeSpin;
    QSpinBox* m_waitAfterSpin;
    QSpinBox* m_durationSpin;

    // Click widgets
    QComboBox* m_clickButtonCombo;
    QSpinBox* m_clickXSpin;
    QSpinBox* m_clickYSpin;
    QPushButton* m_captureClickPosBtn;

    // Drag widgets
    QSpinBox* m_dragStartXSpin;
    QSpinBox* m_dragStartYSpin;
    QPushButton* m_captureDragStartBtn;
    QSpinBox* m_dragEndXSpin;
    QSpinBox* m_dragEndYSpin;
    QPushButton* m_captureDragEndBtn;
    QPushButton* m_captureDragGestureBtn;

    // Hold widgets
    QComboBox* m_holdButtonCombo;
    QSpinBox* m_holdXSpin;
    QSpinBox* m_holdYSpin;
    QPushButton* m_captureHoldPosBtn;

    // Type text widgets
    QLineEdit* m_textEdit;
    QComboBox* m_textModeCombo;

    // Hotkey widgets
    QCheckBox* m_ctrlCheck;
    QCheckBox* m_altCheck;
    QCheckBox* m_shiftCheck;
    QCheckBox* m_winCheck;
    QComboBox* m_hotkeyKeyCombo;
    QPushButton* m_captureHotkeyBtn;
    QLabel* m_hotkeyCaptureStatus;
    HotkeyCapture* m_hotkeyCapture{nullptr};

    // Key Press widgets
    QComboBox* m_keyPressCombo;

    // Scroll widgets
    QComboBox* m_scrollDirectionCombo;
    QSpinBox* m_scrollAmountSpin;

    QPushButton* m_applyButton;
};
