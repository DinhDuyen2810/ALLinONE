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

public slots:
    void onPositionCaptured(int x, int y);

private slots:
    void onTypeChanged(int index);
    void onApplyClicked();

private:
    void setupUi();
    QWidget* createClickPage();
    QWidget* createDragPage();
    QWidget* createHoldPage();
    QWidget* createTextPage();
    QWidget* createHotkeyPage();
    QWidget* createKeyPressPage();
    QWidget* createScrollPage();

    int m_currentIndex{-1};
    int m_capturingField{0}; // 0: X,Y; 1: startX,startY; 2: endX,endY

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

    // Key Press widgets
    QComboBox* m_keyPressCombo;

    // Scroll widgets
    QComboBox* m_scrollDirectionCombo;
    QSpinBox* m_scrollAmountSpin;

    QPushButton* m_applyButton;
};
