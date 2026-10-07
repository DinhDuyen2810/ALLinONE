#include "ActionEditorWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>
#include <QScrollArea>

struct KeyInfo
{
    const char* name;
    int vk;
};

static const KeyInfo KEY_LIST[] = {
    {"ENTER", 13},
    {"TAB", 9},
    {"ESC", 27},
    {"SPACE", 32},
    {"BACKSPACE", 8},
    {"DELETE", 46},
    {"UP ARROW", 38},
    {"DOWN ARROW", 40},
    {"LEFT ARROW", 37},
    {"RIGHT ARROW", 39},
    {"HOME", 36},
    {"END", 35},
    {"PAGE UP", 33},
    {"PAGE DOWN", 34},
    {"F1", 112}, {"F2", 113}, {"F3", 114}, {"F4", 115},
    {"F5", 116}, {"F6", 117}, {"F7", 118}, {"F8", 119},
    {"F9", 120}, {"F10", 121}, {"F11", 122}, {"F12", 123},
    {"A", 65}, {"B", 66}, {"C", 67}, {"D", 68}, {"E", 69},
    {"F", 70}, {"G", 71}, {"H", 72}, {"I", 73}, {"J", 74},
    {"K", 75}, {"L", 76}, {"M", 77}, {"N", 78}, {"O", 79},
    {"P", 80}, {"Q", 81}, {"R", 82}, {"S", 83}, {"T", 84},
    {"U", 85}, {"V", 86}, {"W", 87}, {"X", 88}, {"Y", 89}, {"Z", 90}
};

ActionEditorWidget::ActionEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void ActionEditorWidget::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    auto* header = new QLabel("CÀI ĐẶT HÀNH ĐỘNG", this);
    QFont font = header->font();
    font.setBold(true);
    header->setFont(font);
    header->setStyleSheet("color: #0969da; font-size: 11px; font-weight: bold; letter-spacing: 1.5px; padding-left: 4px;");
    mainLayout->addWidget(header);

    // Scroll Area for settings: Vertical only, strictly no horizontal scroll
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setStyleSheet("QScrollArea { background: transparent; border: none; }");

    auto* scrollContent = new QWidget(scrollArea);
    scrollContent->setStyleSheet("background: transparent;");
    auto* contentLayout = new QVBoxLayout(scrollContent);
    contentLayout->setContentsMargins(0, 0, 4, 0);
    contentLayout->setSpacing(10);

    // Type selection
    auto* typeLayout = new QHBoxLayout();
    auto* typeLabel = new QLabel("Loại:", this);
    typeLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px;");
    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItems({"Click chuột", "Kéo chuột", "Giữ chuột", "Gõ văn bản", "Phím tắt", "Nhấn phím", "Cuộn"});
    m_typeCombo->setStyleSheet(
        "QComboBox { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 10px; font-size: 12px; }"
        "QComboBox:hover { border-color: #0969da; }"
        "QComboBox::drop-down { border: none; }"
    );
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(m_typeCombo, 1);
    contentLayout->addLayout(typeLayout);

    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ActionEditorWidget::onTypeChanged);

    // Stacked widget for action pages
    m_pagesStack = new QStackedWidget(this);
    m_pagesStack->addWidget(createClickPage());
    m_pagesStack->addWidget(createDragPage());
    m_pagesStack->addWidget(createHoldPage());
    m_pagesStack->addWidget(createTextPage());
    m_pagesStack->addWidget(createHotkeyPage());
    m_pagesStack->addWidget(createKeyPressPage());
    m_pagesStack->addWidget(createScrollPage());
    contentLayout->addWidget(m_pagesStack);

    // Common Timing Group
    auto* timingGroup = new QGroupBox("Thời gian (Timing)", this);
    timingGroup->setStyleSheet(
        "QGroupBox { color: #0969da; font-weight: bold; border: 1px solid #d0d7de; border-radius: 12px; margin-top: 10px; padding-top: 14px; background-color: #ffffff; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; font-size: 11px; }"
    );
    auto* timingLayout = new QFormLayout(timingGroup);
    timingLayout->setContentsMargins(12, 12, 12, 12);
    timingLayout->setSpacing(8);

    auto styleSpin = [](QSpinBox* spin) {
        spin->setRange(0, 3600000);
        spin->setSuffix(" ms");
        spin->setStyleSheet(
            "QSpinBox { background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 4px 8px; font-size: 12px; }"
            "QSpinBox:hover, QSpinBox:focus { border-color: #0969da; }"
        );
    };

    m_waitBeforeSpin = new QSpinBox(this);
    styleSpin(m_waitBeforeSpin);
    timingLayout->addRow("Chờ trước:", m_waitBeforeSpin);

    m_waitAfterSpin = new QSpinBox(this);
    styleSpin(m_waitAfterSpin);
    m_waitAfterSpin->setValue(500);
    timingLayout->addRow("Chờ sau:", m_waitAfterSpin);

    m_durationSpin = new QSpinBox(this);
    styleSpin(m_durationSpin);
    timingLayout->addRow("Thời lượng:", m_durationSpin);

    contentLayout->addWidget(timingGroup);
    contentLayout->addStretch();

    scrollArea->setWidget(scrollContent);
    mainLayout->addWidget(scrollArea, 1);

    // Apply Button
    m_applyButton = new QPushButton("✓ Lưu hành động (Apply)", this);
    m_applyButton->setFixedHeight(38);
    m_applyButton->setCursor(Qt::PointingHandCursor);
    m_applyButton->setStyleSheet(
        "QPushButton { background-color: #0969da; color: white; border: none; border-radius: 10px; font-weight: bold; font-size: 13px; }"
        "QPushButton:hover { background-color: #0854b0; }"
        "QPushButton:pressed { background-color: #053d82; }"
    );
    connect(m_applyButton, &QPushButton::clicked, this, &ActionEditorWidget::onApplyClicked);
    mainLayout->addWidget(m_applyButton);
}

QWidget* ActionEditorWidget::createClickPage()
{
    auto* w = new QWidget(this);
    auto* layout = new QFormLayout(w);
    layout->setContentsMargins(0, 6, 0, 6);
    layout->setSpacing(8);

    m_clickButtonCombo = new QComboBox(w);
    m_clickButtonCombo->addItems({"Trái", "Phải", "Giữa"});
    m_clickButtonCombo->setStyleSheet("background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 5px 8px; font-size: 12px;");
    layout->addRow("Nút:", m_clickButtonCombo);

    auto* posLayout = new QHBoxLayout();
    m_clickXSpin = new QSpinBox(w);
    m_clickXSpin->setRange(-10000, 10000);
    m_clickXSpin->setPrefix("X: ");
    m_clickYSpin = new QSpinBox(w);
    m_clickYSpin->setRange(-10000, 10000);
    m_clickYSpin->setPrefix("Y: ");

    QString spinStyle = "QSpinBox { background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 4px 6px; font-size: 12px; } QSpinBox:focus { border-color: #0969da; }";
    m_clickXSpin->setStyleSheet(spinStyle);
    m_clickYSpin->setStyleSheet(spinStyle);

    m_captureClickPosBtn = new QPushButton("🎯 Lấy tọa độ", w);
    m_captureClickPosBtn->setCursor(Qt::PointingHandCursor);
    m_captureClickPosBtn->setStyleSheet("QPushButton { background-color: #0969da; color: white; border: none; border-radius: 8px; padding: 5px 12px; font-size: 11px; font-weight: bold; } QPushButton:hover { background-color: #0854b0; }");
    connect(m_captureClickPosBtn, &QPushButton::clicked, this, [this]() {
        m_capturingField = 0;
        emit captureRequested(0);
    });

    posLayout->addWidget(m_clickXSpin);
    posLayout->addWidget(m_clickYSpin);
    posLayout->addWidget(m_captureClickPosBtn);
    layout->addRow("Vị trí:", posLayout);

    return w;
}

QWidget* ActionEditorWidget::createDragPage()
{
    auto* w = new QWidget(this);
    auto* layout = new QFormLayout(w);
    layout->setContentsMargins(0, 6, 0, 6);
    layout->setSpacing(8);

    QString spinStyle = "QSpinBox { background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 4px 6px; font-size: 12px; } QSpinBox:focus { border-color: #0969da; }";

    auto* startLayout = new QHBoxLayout();
    m_dragStartXSpin = new QSpinBox(w);
    m_dragStartXSpin->setRange(-10000, 10000);
    m_dragStartXSpin->setPrefix("X: ");
    m_dragStartYSpin = new QSpinBox(w);
    m_dragStartYSpin->setRange(-10000, 10000);
    m_dragStartYSpin->setPrefix("Y: ");
    m_dragStartXSpin->setStyleSheet(spinStyle);
    m_dragStartYSpin->setStyleSheet(spinStyle);

    m_captureDragStartBtn = new QPushButton("🎯 Lấy tọa độ", w);
    m_captureDragStartBtn->setCursor(Qt::PointingHandCursor);
    m_captureDragStartBtn->setStyleSheet("QPushButton { background-color: #0969da; color: white; border: none; border-radius: 8px; padding: 5px 12px; font-size: 11px; font-weight: bold; } QPushButton:hover { background-color: #0854b0; }");
    connect(m_captureDragStartBtn, &QPushButton::clicked, this, [this]() {
        m_capturingField = 1;
        emit captureRequested(1);
    });

    startLayout->addWidget(m_dragStartXSpin);
    startLayout->addWidget(m_dragStartYSpin);
    startLayout->addWidget(m_captureDragStartBtn);
    layout->addRow("Điểm đầu:", startLayout);

    auto* endLayout = new QHBoxLayout();
    m_dragEndXSpin = new QSpinBox(w);
    m_dragEndXSpin->setRange(-10000, 10000);
    m_dragEndXSpin->setPrefix("X: ");
    m_dragEndYSpin = new QSpinBox(w);
    m_dragEndYSpin->setRange(-10000, 10000);
    m_dragEndYSpin->setPrefix("Y: ");
    m_dragEndXSpin->setStyleSheet(spinStyle);
    m_dragEndYSpin->setStyleSheet(spinStyle);

    m_captureDragEndBtn = new QPushButton("🎯 Lấy tọa độ", w);
    m_captureDragEndBtn->setCursor(Qt::PointingHandCursor);
    m_captureDragEndBtn->setStyleSheet("QPushButton { background-color: #0969da; color: white; border: none; border-radius: 8px; padding: 5px 12px; font-size: 11px; font-weight: bold; } QPushButton:hover { background-color: #0854b0; }");
    connect(m_captureDragEndBtn, &QPushButton::clicked, this, [this]() {
        m_capturingField = 2;
        emit captureRequested(2);
    });

    endLayout->addWidget(m_dragEndXSpin);
    endLayout->addWidget(m_dragEndYSpin);
    endLayout->addWidget(m_captureDragEndBtn);
    layout->addRow("Điểm cuối:", endLayout);

    return w;
}

QWidget* ActionEditorWidget::createHoldPage()
{
    auto* w = new QWidget(this);
    auto* layout = new QFormLayout(w);
    layout->setContentsMargins(0, 6, 0, 6);
    layout->setSpacing(8);

    m_holdButtonCombo = new QComboBox(w);
    m_holdButtonCombo->addItems({"Trái", "Phải", "Giữa"});
    m_holdButtonCombo->setStyleSheet("background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 5px 8px; font-size: 12px;");
    layout->addRow("Nút:", m_holdButtonCombo);

    auto* posLayout = new QHBoxLayout();
    m_holdXSpin = new QSpinBox(w);
    m_holdXSpin->setRange(-10000, 10000);
    m_holdXSpin->setPrefix("X: ");
    m_holdYSpin = new QSpinBox(w);
    m_holdYSpin->setRange(-10000, 10000);
    m_holdYSpin->setPrefix("Y: ");
    QString spinStyle = "QSpinBox { background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 4px 6px; font-size: 12px; } QSpinBox:focus { border-color: #0969da; }";
    m_holdXSpin->setStyleSheet(spinStyle);
    m_holdYSpin->setStyleSheet(spinStyle);

    m_captureHoldPosBtn = new QPushButton("🎯 Lấy tọa độ", w);
    m_captureHoldPosBtn->setCursor(Qt::PointingHandCursor);
    m_captureHoldPosBtn->setStyleSheet("QPushButton { background-color: #0969da; color: white; border: none; border-radius: 8px; padding: 5px 12px; font-size: 11px; font-weight: bold; } QPushButton:hover { background-color: #0854b0; }");
    connect(m_captureHoldPosBtn, &QPushButton::clicked, this, [this]() {
        m_capturingField = 0;
        emit captureRequested(0);
    });

    posLayout->addWidget(m_holdXSpin);
    posLayout->addWidget(m_holdYSpin);
    posLayout->addWidget(m_captureHoldPosBtn);
    layout->addRow("Vị trí:", posLayout);

    return w;
}

QWidget* ActionEditorWidget::createTextPage()
{
    auto* w = new QWidget(this);
    auto* layout = new QFormLayout(w);
    layout->setContentsMargins(0, 6, 0, 6);
    layout->setSpacing(8);

    m_textEdit = new QLineEdit(w);
    m_textEdit->setPlaceholderText("Nhập văn bản cần gõ...");
    m_textEdit->setStyleSheet("background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 10px; font-size: 12px;");
    layout->addRow("Văn bản:", m_textEdit);

    m_textModeCombo = new QComboBox(w);
    m_textModeCombo->addItems({"Instant (Gõ lập tức)", "Char-by-Char (Gõ từng ký tự)"});
    m_textModeCombo->setStyleSheet("background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 5px 8px; font-size: 12px;");
    layout->addRow("Chế độ:", m_textModeCombo);

    return w;
}

QWidget* ActionEditorWidget::createHotkeyPage()
{
    auto* w = new QWidget(this);
    auto* layout = new QVBoxLayout(w);
    layout->setContentsMargins(0, 6, 0, 6);
    layout->setSpacing(8);

    auto* modsLayout = new QHBoxLayout();
    m_ctrlCheck = new QCheckBox("Ctrl", w);
    m_altCheck = new QCheckBox("Alt", w);
    m_shiftCheck = new QCheckBox("Shift", w);
    m_winCheck = new QCheckBox("Win", w);

    QString chkStyle =
        "QCheckBox { color: #1f2328; font-weight: 600; font-size: 12px; spacing: 6px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; border: 1px solid #d0d7de; background: #f6f8fa; }"
        "QCheckBox::indicator:checked { background: #0969da; border-color: #0969da; }";
    m_ctrlCheck->setStyleSheet(chkStyle);
    m_altCheck->setStyleSheet(chkStyle);
    m_shiftCheck->setStyleSheet(chkStyle);
    m_winCheck->setStyleSheet(chkStyle);

    modsLayout->addWidget(m_ctrlCheck);
    modsLayout->addWidget(m_altCheck);
    modsLayout->addWidget(m_shiftCheck);
    modsLayout->addWidget(m_winCheck);
    layout->addLayout(modsLayout);

    auto* keyLayout = new QHBoxLayout();
    auto* keyLabel = new QLabel("Phím:", w);
    keyLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px;");
    m_hotkeyKeyCombo = new QComboBox(w);
    for (const auto& k : KEY_LIST)
    {
        m_hotkeyKeyCombo->addItem(k.name, k.vk);
    }
    m_hotkeyKeyCombo->setStyleSheet("background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 5px 8px; font-size: 12px;");
    keyLayout->addWidget(keyLabel);
    keyLayout->addWidget(m_hotkeyKeyCombo, 1);
    layout->addLayout(keyLayout);

    return w;
}

QWidget* ActionEditorWidget::createKeyPressPage()
{
    auto* w = new QWidget(this);
    auto* layout = new QFormLayout(w);
    layout->setContentsMargins(0, 6, 0, 6);
    layout->setSpacing(8);

    m_keyPressCombo = new QComboBox(w);
    for (const auto& k : KEY_LIST)
    {
        m_keyPressCombo->addItem(k.name, k.vk);
    }
    m_keyPressCombo->setStyleSheet("background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 5px 8px; font-size: 12px;");
    layout->addRow("Phím:", m_keyPressCombo);

    return w;
}

QWidget* ActionEditorWidget::createScrollPage()
{
    auto* w = new QWidget(this);
    auto* layout = new QFormLayout(w);
    layout->setContentsMargins(0, 6, 0, 6);
    layout->setSpacing(8);

    m_scrollDirectionCombo = new QComboBox(w);
    m_scrollDirectionCombo->addItems({"Down (Xuống)", "Up (Lên)", "Left (Trái)", "Right (Phải)"});
    m_scrollDirectionCombo->setStyleSheet("background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 5px 8px; font-size: 12px;");
    layout->addRow("Hướng:", m_scrollDirectionCombo);

    m_scrollAmountSpin = new QSpinBox(w);
    m_scrollAmountSpin->setRange(1, 100);
    m_scrollAmountSpin->setValue(5);
    m_scrollAmountSpin->setStyleSheet("background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 4px 8px; font-size: 12px;");
    layout->addRow("Số bước:", m_scrollAmountSpin);

    return w;
}

void ActionEditorWidget::onTypeChanged(int index)
{
    m_pagesStack->setCurrentIndex(index);
}

void ActionEditorWidget::setAction(const Action& action, int actionIndex)
{
    m_currentIndex = actionIndex;
    m_typeCombo->setCurrentIndex(static_cast<int>(action.type));
    m_pagesStack->setCurrentIndex(static_cast<int>(action.type));

    m_waitBeforeSpin->setValue(static_cast<int>(action.waitBefore.count()));
    m_waitAfterSpin->setValue(static_cast<int>(action.waitAfter.count()));
    m_durationSpin->setValue(static_cast<int>(action.duration.count()));

    m_clickButtonCombo->setCurrentIndex(static_cast<int>(action.mouseButton));
    m_clickXSpin->setValue(action.x);
    m_clickYSpin->setValue(action.y);

    m_dragStartXSpin->setValue(action.startX);
    m_dragStartYSpin->setValue(action.startY);
    m_dragEndXSpin->setValue(action.endX);
    m_dragEndYSpin->setValue(action.endY);

    m_holdButtonCombo->setCurrentIndex(static_cast<int>(action.mouseButton));
    m_holdXSpin->setValue(action.x);
    m_holdYSpin->setValue(action.y);

    m_textEdit->setText(QString::fromStdString(action.text));
    m_textModeCombo->setCurrentIndex(static_cast<int>(action.textMode));

    m_ctrlCheck->setChecked(action.modCtrl);
    m_altCheck->setChecked(action.modAlt);
    m_shiftCheck->setChecked(action.modShift);
    m_winCheck->setChecked(action.modWin);

    int hotkeyIdx = m_hotkeyKeyCombo->findData(action.keyCode);
    if (hotkeyIdx >= 0) m_hotkeyKeyCombo->setCurrentIndex(hotkeyIdx);

    int keyPressIdx = m_keyPressCombo->findData(action.keyCode);
    if (keyPressIdx >= 0) m_keyPressCombo->setCurrentIndex(keyPressIdx);

    m_scrollDirectionCombo->setCurrentIndex(static_cast<int>(action.scrollDirection));
    m_scrollAmountSpin->setValue(action.scrollAmount);
}

Action ActionEditorWidget::getAction() const
{
    Action act;
    act.type = static_cast<ActionType>(m_typeCombo->currentIndex());

    act.waitBefore = std::chrono::milliseconds(m_waitBeforeSpin->value());
    act.waitAfter = std::chrono::milliseconds(m_waitAfterSpin->value());
    act.duration = std::chrono::milliseconds(m_durationSpin->value());

    act.mouseButton = static_cast<MouseButtonType>(m_clickButtonCombo->currentIndex());
    act.x = m_clickXSpin->value();
    act.y = m_clickYSpin->value();

    act.startX = m_dragStartXSpin->value();
    act.startY = m_dragStartYSpin->value();
    act.endX = m_dragEndXSpin->value();
    act.endY = m_dragEndYSpin->value();

    if (act.type == ActionType::MouseHold)
    {
        act.mouseButton = static_cast<MouseButtonType>(m_holdButtonCombo->currentIndex());
        act.x = m_holdXSpin->value();
        act.y = m_holdYSpin->value();
    }

    act.text = m_textEdit->text().toStdString();
    act.textMode = static_cast<TextTypeMode>(m_textModeCombo->currentIndex());

    act.modCtrl = m_ctrlCheck->isChecked();
    act.modAlt = m_altCheck->isChecked();
    act.modShift = m_shiftCheck->isChecked();
    act.modWin = m_winCheck->isChecked();
    act.keyCode = m_hotkeyKeyCombo->currentData().toInt();
    act.keyName = m_hotkeyKeyCombo->currentText().toStdString();

    if (act.type == ActionType::KeyPress)
    {
        act.keyCode = m_keyPressCombo->currentData().toInt();
        act.keyName = m_keyPressCombo->currentText().toStdString();
    }

    act.scrollDirection = static_cast<ScrollDirection>(m_scrollDirectionCombo->currentIndex());
    act.scrollAmount = m_scrollAmountSpin->value();

    return act;
}

void ActionEditorWidget::onPositionCaptured(int x, int y)
{
    if (m_capturingField == 0) // click or hold
    {
        m_clickXSpin->setValue(x);
        m_clickYSpin->setValue(y);
        m_holdXSpin->setValue(x);
        m_holdYSpin->setValue(y);
    }
    else if (m_capturingField == 1) // drag start
    {
        m_dragStartXSpin->setValue(x);
        m_dragStartYSpin->setValue(y);
    }
    else if (m_capturingField == 2) // drag end
    {
        m_dragEndXSpin->setValue(x);
        m_dragEndYSpin->setValue(y);
    }
}

void ActionEditorWidget::onApplyClicked()
{
    emit actionSaved(getAction(), m_currentIndex);
}

void ActionEditorWidget::clear()
{
    m_currentIndex = -1;
    Action defaultAct;
    setAction(defaultAct, -1);
}
