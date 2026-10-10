#include "ActionEditorWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>
#include <QHideEvent>
#include <QScrollArea>
#include <algorithm>

// Miền tọa độ của các ô X/Y: toàn bộ màn hình ảo Windows (tọa độ 16-bit có dấu). Trước đây là ±10000 - ba
// màn hình 4K đặt ngang đã rộng 11520 px (ứng dụng chạy Per-Monitor DPI nên đây là pixel vật lý), tọa độ
// bắt được ở màn hình ngoài cùng bị ô nhập âm thầm cắt về 10000 và click sai chỗ.
static constexpr int kMinScreenCoord = -32768;
static constexpr int kMaxScreenCoord = 32767;

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
    , m_hotkeyCapture(new HotkeyCapture(this))
{
    setupUi();

    connect(m_hotkeyCapture, &HotkeyCapture::hotkeyCaptured, this,
            [this](const std::vector<ModifierKey>& order, int vkCode) {
        m_syncingModifierChecks = true;
        m_ctrlCheck->setChecked(std::find(order.begin(), order.end(), ModifierKey::Ctrl) != order.end());
        m_altCheck->setChecked(std::find(order.begin(), order.end(), ModifierKey::Alt) != order.end());
        m_shiftCheck->setChecked(std::find(order.begin(), order.end(), ModifierKey::Shift) != order.end());
        m_winCheck->setChecked(std::find(order.begin(), order.end(), ModifierKey::Win) != order.end());
        m_syncingModifierChecks = false;
        m_hotkeyModOrder = order; // nguồn thật = đúng thứ tự hook ghi được, thắng mọi thứ tự cũ
        refreshModifierCheckboxLabels();

        const int idx = m_hotkeyKeyCombo->findData(vkCode);
        if (idx >= 0)
        {
            m_hotkeyKeyCombo->setCurrentIndex(idx);
            m_hotkeyCaptureStatus->setText("✓ Đã bắt được tổ hợp phím.");
        }
        else
        {
            m_hotkeyCaptureStatus->setText(
                QString("⚠ Đã bắt phím bổ trợ (Ctrl/Alt/Shift/Win) nhưng phím chính (mã %1) chưa có "
                        "trong danh sách hỗ trợ - chọn phím thủ công ở dưới.").arg(vkCode));
        }
        m_captureHotkeyBtn->setText("🎯 Bắt tổ hợp phím");
        m_captureHotkeyBtn->setEnabled(true);
    });
    connect(m_hotkeyCapture, &HotkeyCapture::captureCancelled, this, [this]() {
        m_hotkeyCaptureStatus->setText("Đã hủy bắt tổ hợp phím.");
        m_captureHotkeyBtn->setText("🎯 Bắt tổ hợp phím");
        m_captureHotkeyBtn->setEnabled(true);
    });
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
    m_typeCombo->addItems({"Click chuột", "Kéo chuột", "Giữ chuột", "Gõ văn bản", "Tổ hợp phím", "Nhấn phím", "Cuộn"});
    m_typeCombo->setStyleSheet(
        "QComboBox { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 10px; font-size: 12px; }"
        "QComboBox:hover { border-color: #0969da; }"
        "QComboBox::drop-down { border: none; }"
    );
    typeLayout->addWidget(typeLabel);
    typeLayout->addWidget(m_typeCombo, 1);
    contentLayout->addLayout(typeLayout);

    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ActionEditorWidget::onTypeChanged);

    // Stacked widget for action pages. Đo sizeHint() của TỪNG trang TRƯỚC khi add vào QStackedWidget (vẫn
    // hợp lệ dù chưa add - sizeHint tính từ layout riêng của mỗi trang) để lấy trang rộng nhất (thường là
    // "Kéo chuột": 2 hàng tọa độ, mỗi hàng 2 SpinBox + nút "Lấy tọa độ") làm minimumWidth cho CẢ panel -
    // tránh set cố định một con số đoán mò rồi bị cắt mất khi nội dung thực tế rộng hơn (xem comment ở
    // scrollArea->setHorizontalScrollBarPolicy bên trên: panel này CHỦ Ý không cho cuộn ngang, nên phải
    // luôn đủ rộng cho trang rộng nhất, không được hẹp hơn).
    QWidget* clickPage = createClickPage();
    QWidget* dragPage = createDragPage();
    QWidget* holdPage = createHoldPage();
    QWidget* textPage = createTextPage();
    QWidget* hotkeyPage = createHotkeyPage();
    QWidget* keyPressPage = createKeyPressPage();
    QWidget* scrollPage = createScrollPage();

    int widestPageWidth = 0;
    for (QWidget* page : {clickPage, dragPage, holdPage, textPage, hotkeyPage, keyPressPage, scrollPage})
        widestPageWidth = std::max(widestPageWidth, page->sizeHint().width());

    m_pagesStack = new QStackedWidget(this);
    m_pagesStack->addWidget(clickPage);
    m_pagesStack->addWidget(dragPage);
    m_pagesStack->addWidget(holdPage);
    m_pagesStack->addWidget(textPage);
    m_pagesStack->addWidget(hotkeyPage);
    m_pagesStack->addWidget(keyPressPage);
    m_pagesStack->addWidget(scrollPage);
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

    // Trước đây QSpinBox hiển thị thẳng mili-giây thô kèm nút mũi tên tăng/giảm - khó đọc với khoảng thời
    // gian dài, nút mũi tên không đồng bộ phong cách hiện đại của phần còn lại ứng dụng (yêu cầu người
    // dùng, v1.19.7). DurationInput (src/ui/widgets/) cho nhập số + chọn đơn vị ms/giây/phút/giờ.
    m_waitBeforeInput = new DurationInput(this);
    m_waitBeforeInput->setObjectName("waitBeforeInput");
    timingLayout->addRow("Chờ trước:", m_waitBeforeInput);

    m_waitAfterInput = new DurationInput(this);
    m_waitAfterInput->setObjectName("waitAfterInput");
    m_waitAfterInput->setValueMs(500);
    timingLayout->addRow("Chờ sau:", m_waitAfterInput);

    m_durationInput = new DurationInput(this);
    m_durationInput->setObjectName("durationInput");
    timingLayout->addRow("Thời lượng:", m_durationInput);

    contentLayout->addWidget(timingGroup);
    contentLayout->addStretch();

    // Panel KHÔNG được cuộn ngang (chủ ý, xem scrollArea->setHorizontalScrollBarPolicy ở trên) nên phải
    // tự đủ rộng cho nội dung rộng nhất - cộng margin hai bên của mainLayout+contentLayout (8+8+0+4) và
    // chỗ cho thanh cuộn dọc (luôn xuất hiện vì danh sách field dài hơn khung nhìn) + đệm an toàn.
    const int widestContentWidth = std::max(widestPageWidth, timingGroup->sizeHint().width());
    setMinimumWidth(widestContentWidth + 20 /*margins*/ + 18 /*thanh cuộn dọc*/ + 12 /*đệm an toàn*/);

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

    connectDirtyTracking();
}

void ActionEditorWidget::connectDirtyTracking()
{
    // Theo yêu cầu người dùng: khi sửa một hành động mà chưa bấm "Lưu hành động (Apply)", dòng tương
    // ứng trong danh sách phải hiện dấu "*" để biết còn thay đổi chưa lưu - nối TẤT CẢ điều khiển có
    // thể sửa vào markDirty(). m_loadingAction chặn các lần gọi giả do chính setAction() tự đặt giá trị
    // (setValue/setCurrentIndex cũng phát tín hiệu valueChanged/currentIndexChanged như người dùng gõ thật).
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ActionEditorWidget::markDirty);
    connect(m_waitBeforeInput, &DurationInput::valueChanged, this, &ActionEditorWidget::markDirty);
    connect(m_waitAfterInput, &DurationInput::valueChanged, this, &ActionEditorWidget::markDirty);
    connect(m_durationInput, &DurationInput::valueChanged, this, &ActionEditorWidget::markDirty);

    connect(m_clickButtonCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ActionEditorWidget::markDirty);
    connect(m_clickXSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ActionEditorWidget::markDirty);
    connect(m_clickYSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ActionEditorWidget::markDirty);

    connect(m_dragStartXSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ActionEditorWidget::markDirty);
    connect(m_dragStartYSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ActionEditorWidget::markDirty);
    connect(m_dragEndXSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ActionEditorWidget::markDirty);
    connect(m_dragEndYSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ActionEditorWidget::markDirty);

    connect(m_holdButtonCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ActionEditorWidget::markDirty);
    connect(m_holdXSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ActionEditorWidget::markDirty);
    connect(m_holdYSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ActionEditorWidget::markDirty);

    connect(m_textEdit, &QLineEdit::textChanged, this, &ActionEditorWidget::markDirty);
    connect(m_textModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ActionEditorWidget::markDirty);

    // Mỗi checkbox nối CẢ HAI: markDirty() (như mọi điều khiển khác) VÀ onModifierToggled() (tự thêm/bớt
    // khỏi m_hotkeyModOrder + đổi text hiện số thứ tự) - 2 việc độc lập, chạy song song.
    connect(m_ctrlCheck, &QCheckBox::toggled, this, &ActionEditorWidget::markDirty);
    connect(m_altCheck, &QCheckBox::toggled, this, &ActionEditorWidget::markDirty);
    connect(m_shiftCheck, &QCheckBox::toggled, this, &ActionEditorWidget::markDirty);
    connect(m_winCheck, &QCheckBox::toggled, this, &ActionEditorWidget::markDirty);
    connect(m_ctrlCheck, &QCheckBox::toggled, this, [this](bool c) { onModifierToggled(ModifierKey::Ctrl, c); });
    connect(m_altCheck, &QCheckBox::toggled, this, [this](bool c) { onModifierToggled(ModifierKey::Alt, c); });
    connect(m_shiftCheck, &QCheckBox::toggled, this, [this](bool c) { onModifierToggled(ModifierKey::Shift, c); });
    connect(m_winCheck, &QCheckBox::toggled, this, [this](bool c) { onModifierToggled(ModifierKey::Win, c); });
    connect(m_hotkeyKeyCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ActionEditorWidget::markDirty);
    connect(m_hotkeyTriggerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ActionEditorWidget::markDirty);
    connect(m_hotkeyScrollDirectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ActionEditorWidget::markDirty);
    connect(m_hotkeyScrollAmountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ActionEditorWidget::markDirty);
    connect(m_hotkeyClickButtonCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ActionEditorWidget::markDirty);
    connect(m_hotkeyClickXSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ActionEditorWidget::markDirty);
    connect(m_hotkeyClickYSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ActionEditorWidget::markDirty);

    connect(m_keyPressCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ActionEditorWidget::markDirty);

    connect(m_scrollDirectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ActionEditorWidget::markDirty);
    connect(m_scrollAmountSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ActionEditorWidget::markDirty);
}

void ActionEditorWidget::refreshModifierCheckboxLabels()
{
    auto applyLabel = [this](QCheckBox* box, const QString& name, ModifierKey key) {
        const auto it = std::find(m_hotkeyModOrder.begin(), m_hotkeyModOrder.end(), key);
        box->setText(it != m_hotkeyModOrder.end()
                         ? QString("%1 (%2)").arg(name).arg(std::distance(m_hotkeyModOrder.begin(), it) + 1)
                         : name);
    };
    applyLabel(m_ctrlCheck, "Ctrl", ModifierKey::Ctrl);
    applyLabel(m_altCheck, "Alt", ModifierKey::Alt);
    applyLabel(m_shiftCheck, "Shift", ModifierKey::Shift);
    applyLabel(m_winCheck, "Win", ModifierKey::Win);
}

void ActionEditorWidget::onModifierToggled(ModifierKey key, bool checked)
{
    // Đang TỰ gán giá trị (setAction() hoặc kết quả HotkeyCapture) - nơi gọi đã tự quản lý
    // m_hotkeyModOrder, không để nhánh này can thiệp thêm lần nữa.
    if (m_loadingAction || m_syncingModifierChecks)
        return;
    if (checked)
    {
        // Bấm thêm - số MỚI luôn ở CUỐI danh sách (đúng yêu cầu: bấm theo thứ tự nào thì đánh số thứ tự đó).
        if (std::find(m_hotkeyModOrder.begin(), m_hotkeyModOrder.end(), key) == m_hotkeyModOrder.end())
            m_hotkeyModOrder.push_back(key);
    }
    else
    {
        // Bỏ chọn ở giữa - các số sau tự dồn vì refreshModifierCheckboxLabels() đánh lại số từ đầu mỗi lần.
        m_hotkeyModOrder.erase(std::remove(m_hotkeyModOrder.begin(), m_hotkeyModOrder.end(), key), m_hotkeyModOrder.end());
    }
    refreshModifierCheckboxLabels();
}

void ActionEditorWidget::markDirty()
{
    if (m_loadingAction || m_dirty)
        return;
    m_dirty = true;
    emit dirtyChanged(true);
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
    m_clickXSpin->setRange(kMinScreenCoord, kMaxScreenCoord);
    m_clickXSpin->setPrefix("X: ");
    m_clickYSpin = new QSpinBox(w);
    m_clickYSpin->setRange(kMinScreenCoord, kMaxScreenCoord);
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

    // Trước đây có thêm 2 nút "Lấy tọa độ" riêng cho điểm đầu/cuối (bắt từng điểm bằng 1 cú click) cạnh
    // nút "Bắt thao tác kéo thật" (bắt TRỌN cả 2 điểm bằng 1 lần kéo-thả thật) - thừa, dễ nhầm lẫn 2 cách
    // nhập cùng lúc (yêu cầu người dùng, v1.19.7). Chỉ còn đúng 1 cách: bắt thao tác kéo thật. 4 ô tọa độ
    // GIỮ LẠI làm hiển thị KẾT QUẢ (đọc-only) - readOnly không chặn gọi setValue() bằng code nên
    // onDragGestureCaptured()/getAction()/setAction() không cần đổi gì.
    QString spinStyle =
        "QSpinBox { background-color: #eaeef2; color: #57606a; border: 1px solid #d0d7de; border-radius: 8px; padding: 4px 6px; font-size: 12px; }";
    auto makeReadOnlyCoordSpin = [&](const QString& prefix) {
        auto* spin = new QSpinBox(w);
        spin->setRange(kMinScreenCoord, kMaxScreenCoord);
        spin->setPrefix(prefix);
        spin->setStyleSheet(spinStyle);
        spin->setReadOnly(true);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setFocusPolicy(Qt::NoFocus);
        return spin;
    };

    auto* startLayout = new QHBoxLayout();
    m_dragStartXSpin = makeReadOnlyCoordSpin("X: ");
    m_dragStartYSpin = makeReadOnlyCoordSpin("Y: ");
    startLayout->addWidget(m_dragStartXSpin);
    startLayout->addWidget(m_dragStartYSpin);
    layout->addRow("Điểm đầu:", startLayout);

    auto* endLayout = new QHBoxLayout();
    m_dragEndXSpin = makeReadOnlyCoordSpin("X: ");
    m_dragEndYSpin = makeReadOnlyCoordSpin("Y: ");
    endLayout->addWidget(m_dragEndXSpin);
    endLayout->addWidget(m_dragEndYSpin);
    layout->addRow("Điểm cuối:", endLayout);

    // Bắt TRỌN thao tác kéo thật (nhấn-kéo-thả): ứng dụng tự ẩn đi, người dùng kéo chuột thật như bình
    // thường (màn hình/ứng dụng phía dưới vẫn nhận được thao tác thật), thả ra là xong - ứng dụng tự điền
    // cả 2 điểm ở trên rồi tự hiện lại.
    m_captureDragGestureBtn = new QPushButton("🖐 Bắt thao tác kéo thật", w);
    m_captureDragGestureBtn->setCursor(Qt::PointingHandCursor);
    m_captureDragGestureBtn->setStyleSheet(
        "QPushButton { background-color: #1f883d; color: white; border: none; border-radius: 8px; padding: 7px 12px; font-size: 11px; font-weight: bold; } "
        "QPushButton:hover { background-color: #1a7f37; }");
    connect(m_captureDragGestureBtn, &QPushButton::clicked, this, [this]() {
        emit dragGestureCaptureRequested();
    });
    layout->addRow("", m_captureDragGestureBtn);

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
    m_holdXSpin->setRange(kMinScreenCoord, kMaxScreenCoord);
    m_holdXSpin->setPrefix("X: ");
    m_holdYSpin = new QSpinBox(w);
    m_holdYSpin->setRange(kMinScreenCoord, kMaxScreenCoord);
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
    m_textEdit->setStyleSheet("background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 10px; font-size: 12px; placeholder-text-color: #8c959f;");
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

    // "Kết thúc bằng" - trước đây tổ hợp phím CHỈ có thể kết thúc bằng 1 phím chính; giờ thêm Cuộn chuột/
    // Click chuột (vd Ctrl+Cuộn lên = zoom in, yêu cầu người dùng v1.19.7). 3 trang con dùng CHUNG style
    // với createClickPage()/createScrollPage() nhưng widget instance RIÊNG (1 widget chỉ thuộc 1 layout).
    auto* triggerLayout = new QHBoxLayout();
    auto* triggerLabel = new QLabel("Kết thúc bằng:", w);
    triggerLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px;");
    m_hotkeyTriggerCombo = new QComboBox(w);
    m_hotkeyTriggerCombo->addItems({"Phím chính", "Cuộn chuột", "Click chuột"});
    m_hotkeyTriggerCombo->setStyleSheet("background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 5px 8px; font-size: 12px;");
    triggerLayout->addWidget(triggerLabel);
    triggerLayout->addWidget(m_hotkeyTriggerCombo, 1);
    layout->addLayout(triggerLayout);

    m_hotkeyTriggerStack = new QStackedWidget(w);

    // Trang 0: Phím chính (hành vi gốc, không đổi)
    auto* keyPage = new QWidget(w);
    auto* keyLayout = new QFormLayout(keyPage);
    keyLayout->setContentsMargins(0, 0, 0, 0);
    keyLayout->setSpacing(8);
    m_hotkeyKeyCombo = new QComboBox(keyPage);
    for (const auto& k : KEY_LIST)
    {
        m_hotkeyKeyCombo->addItem(k.name, k.vk);
    }
    m_hotkeyKeyCombo->setStyleSheet("background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 5px 8px; font-size: 12px;");
    keyLayout->addRow("Phím:", m_hotkeyKeyCombo);
    m_hotkeyTriggerStack->addWidget(keyPage);

    // Trang 1: Cuộn chuột - cùng kiểu điều khiển với createScrollPage()
    auto* scrollPage = new QWidget(w);
    auto* scrollLayout = new QFormLayout(scrollPage);
    scrollLayout->setContentsMargins(0, 0, 0, 0);
    scrollLayout->setSpacing(8);
    m_hotkeyScrollDirectionCombo = new QComboBox(scrollPage);
    m_hotkeyScrollDirectionCombo->addItems({"Down (Xuống)", "Up (Lên)", "Left (Trái)", "Right (Phải)"});
    m_hotkeyScrollDirectionCombo->setStyleSheet("background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 5px 8px; font-size: 12px;");
    scrollLayout->addRow("Hướng:", m_hotkeyScrollDirectionCombo);
    m_hotkeyScrollAmountSpin = new QSpinBox(scrollPage);
    m_hotkeyScrollAmountSpin->setRange(1, 100);
    m_hotkeyScrollAmountSpin->setValue(5);
    m_hotkeyScrollAmountSpin->setStyleSheet("background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 4px 8px; font-size: 12px;");
    scrollLayout->addRow("Số bước:", m_hotkeyScrollAmountSpin);
    m_hotkeyTriggerStack->addWidget(scrollPage);

    // Trang 2: Click chuột - cùng kiểu điều khiển với createClickPage()
    auto* clickPage = new QWidget(w);
    auto* clickLayout = new QFormLayout(clickPage);
    clickLayout->setContentsMargins(0, 0, 0, 0);
    clickLayout->setSpacing(8);
    m_hotkeyClickButtonCombo = new QComboBox(clickPage);
    m_hotkeyClickButtonCombo->addItems({"Trái", "Phải", "Giữa"});
    m_hotkeyClickButtonCombo->setStyleSheet("background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 5px 8px; font-size: 12px;");
    clickLayout->addRow("Nút:", m_hotkeyClickButtonCombo);

    auto* hotkeyClickPosLayout = new QHBoxLayout();
    QString hotkeySpinStyle = "QSpinBox { background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 4px 6px; font-size: 12px; } QSpinBox:focus { border-color: #0969da; }";
    m_hotkeyClickXSpin = new QSpinBox(clickPage);
    m_hotkeyClickXSpin->setRange(kMinScreenCoord, kMaxScreenCoord);
    m_hotkeyClickXSpin->setPrefix("X: ");
    m_hotkeyClickXSpin->setStyleSheet(hotkeySpinStyle);
    m_hotkeyClickYSpin = new QSpinBox(clickPage);
    m_hotkeyClickYSpin->setRange(kMinScreenCoord, kMaxScreenCoord);
    m_hotkeyClickYSpin->setPrefix("Y: ");
    m_hotkeyClickYSpin->setStyleSheet(hotkeySpinStyle);
    auto* captureHotkeyClickPosBtn = new QPushButton("🎯 Lấy tọa độ", clickPage);
    captureHotkeyClickPosBtn->setCursor(Qt::PointingHandCursor);
    captureHotkeyClickPosBtn->setStyleSheet("QPushButton { background-color: #0969da; color: white; border: none; border-radius: 8px; padding: 5px 12px; font-size: 11px; font-weight: bold; } QPushButton:hover { background-color: #0854b0; }");
    connect(captureHotkeyClickPosBtn, &QPushButton::clicked, this, [this]() {
        m_capturingField = 3;
        emit captureRequested(3);
    });
    hotkeyClickPosLayout->addWidget(m_hotkeyClickXSpin);
    hotkeyClickPosLayout->addWidget(m_hotkeyClickYSpin);
    hotkeyClickPosLayout->addWidget(captureHotkeyClickPosBtn);
    clickLayout->addRow("Vị trí:", hotkeyClickPosLayout);
    m_hotkeyTriggerStack->addWidget(clickPage);

    connect(m_hotkeyTriggerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), m_hotkeyTriggerStack, &QStackedWidget::setCurrentIndex);
    layout->addWidget(m_hotkeyTriggerStack);

    // Bắt tổ hợp phím THẬT (vd Ctrl+Shift+S) thay vì phải tự tick từng ô - bấm nút rồi nhấn tổ hợp
    // phím mong muốn một lần, ứng dụng tự điền đúng các ô bên trên. Checkbox vẫn độc lập với nhau nên
    // tổ hợp 3 phím bổ trợ cùng lúc (Ctrl+Shift+Win+...) vẫn luôn được hỗ trợ dù bắt tay hay bắt THẬT.
    m_captureHotkeyBtn = new QPushButton("🎯 Bắt tổ hợp phím", w);
    m_captureHotkeyBtn->setCursor(Qt::PointingHandCursor);
    m_captureHotkeyBtn->setStyleSheet("QPushButton { background-color: #0969da; color: white; border: none; border-radius: 8px; padding: 6px 12px; font-size: 11px; font-weight: bold; } QPushButton:hover { background-color: #0854b0; }");
    connect(m_captureHotkeyBtn, &QPushButton::clicked, this, [this]() {
        m_captureHotkeyBtn->setText("Đang chờ... nhấn tổ hợp phím (Esc để hủy)");
        m_captureHotkeyBtn->setEnabled(false);
        m_hotkeyCaptureStatus->setText("");
        m_hotkeyCapture->startCapture();
        // startCapture() không báo gì khi Windows từ chối cài hook bàn phím - không tự kiểm tra thì nút cứ ở
        // trạng thái "Đang chờ..." và bị vô hiệu hóa mãi (không có tín hiệu nào tới để bật lại).
        if (!m_hotkeyCapture->isCapturing())
        {
            m_hotkeyCaptureStatus->setText("⚠ Không bắt được: Windows từ chối cài hook bàn phím - chọn phím thủ công ở trên.");
            m_captureHotkeyBtn->setText("🎯 Bắt tổ hợp phím");
            m_captureHotkeyBtn->setEnabled(true);
        }
    });
    layout->addWidget(m_captureHotkeyBtn);

    m_hotkeyCaptureStatus = new QLabel(w);
    m_hotkeyCaptureStatus->setWordWrap(true);
    m_hotkeyCaptureStatus->setStyleSheet("color: #57606a; font-size: 11px;");
    layout->addWidget(m_hotkeyCaptureStatus);

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
    // Rời trang "Phím tắt" thì không còn gì để bắt nữa - xem cancelHotkeyCapture().
    if (index != static_cast<int>(ActionType::Hotkey))
        cancelHotkeyCapture();
}

void ActionEditorWidget::cancelHotkeyCapture()
{
    // Hook bàn phím của HotkeyCapture NUỐT MỌI PHÍM trên toàn hệ thống trong lúc đang bắt. Trước đây chỉ
    // có Esc/hết 20 giây mới gỡ nó: bấm "Bắt tổ hợp phím" rồi chọn hàng khác, đóng hay thu nhỏ cửa sổ thì
    // bàn phím cả máy vẫn "chết" tới hết 20 giây, và tổ hợp bắt được sau đó lại điền vào một hành động
    // KHÁC với hành động lúc bấm nút.
    if (m_hotkeyCapture && m_hotkeyCapture->isCapturing())
        m_hotkeyCapture->cancelCapture();
}

void ActionEditorWidget::hideEvent(QHideEvent* event)
{
    cancelHotkeyCapture(); // đóng/ẩn/thu nhỏ cửa sổ Auto Click
    QWidget::hideEvent(event);
}

namespace
{
// Vai trò dữ liệu đánh dấu mục TẠM "(phím khác: ...)" trong combo phím và giữ tên phím gốc của nó.
constexpr int kCustomKeyNameRole = Qt::UserRole + 1;
}

void ActionEditorWidget::selectKeyInCombo(QComboBox* combo, int vkCode, const QString& keyName)
{
    // Bỏ mục tạm của hành động nạp trước đó (nếu có) - mỗi lúc chỉ có tối đa một mục tạm.
    for (int i = combo->count() - 1; i >= 0; --i)
    {
        if (combo->itemData(i, kCustomKeyNameRole).isValid())
            combo->removeItem(i);
    }

    int idx = combo->findData(vkCode);
    if (idx < 0)
    {
        // Phím nằm NGOÀI danh sách hỗ trợ sẵn (hồ sơ nhập từ tệp JSON, vd phím số/numpad). Trước đây
        // combo cứ giữ nguyên lựa chọn của hành động xem trước đó, nên bấm "Lưu hành động" là âm thầm
        // đổi phím của hành động này thành phím kia. Thêm một mục tạm mang đúng mã + tên gốc để Lưu mà
        // không đụng tới combo thì phím giữ nguyên.
        const QString shown = keyName.isEmpty() ? QString("mã %1").arg(vkCode) : keyName;
        combo->addItem(QString("(phím khác: %1)").arg(shown), vkCode);
        idx = combo->count() - 1;
        combo->setItemData(idx, keyName, kCustomKeyNameRole);
    }
    combo->setCurrentIndex(idx);
}

QString ActionEditorWidget::keyNameFromCombo(const QComboBox* combo)
{
    const QVariant custom = combo->currentData(kCustomKeyNameRole);
    return custom.isValid() ? custom.toString() : combo->currentText();
}

void ActionEditorWidget::setAction(const Action& action, int actionIndex)
{
    cancelHotkeyCapture(); // đang chờ bắt tổ hợp phím cho hành động CŨ thì hủy, không điền nhầm sang hành động mới

    m_loadingAction = true; // chặn markDirty() trong lúc TỰ nạp giá trị dưới đây
    m_currentIndex = actionIndex;
    m_loadedEnabled = action.enabled;
    m_typeCombo->setCurrentIndex(static_cast<int>(action.type));
    m_pagesStack->setCurrentIndex(static_cast<int>(action.type));

    m_waitBeforeInput->setValueMs(static_cast<int>(action.waitBefore.count()));
    m_waitAfterInput->setValueMs(static_cast<int>(action.waitAfter.count()));
    m_durationInput->setValueMs(static_cast<int>(action.duration.count()));

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

    m_hotkeyModOrder = action.effectiveModOrder();
    m_syncingModifierChecks = true;
    m_ctrlCheck->setChecked(std::find(m_hotkeyModOrder.begin(), m_hotkeyModOrder.end(), ModifierKey::Ctrl) != m_hotkeyModOrder.end());
    m_altCheck->setChecked(std::find(m_hotkeyModOrder.begin(), m_hotkeyModOrder.end(), ModifierKey::Alt) != m_hotkeyModOrder.end());
    m_shiftCheck->setChecked(std::find(m_hotkeyModOrder.begin(), m_hotkeyModOrder.end(), ModifierKey::Shift) != m_hotkeyModOrder.end());
    m_winCheck->setChecked(std::find(m_hotkeyModOrder.begin(), m_hotkeyModOrder.end(), ModifierKey::Win) != m_hotkeyModOrder.end());
    m_syncingModifierChecks = false;
    refreshModifierCheckboxLabels();

    const QString keyName = QString::fromStdString(action.keyName);
    selectKeyInCombo(m_hotkeyKeyCombo, action.keyCode, keyName);
    selectKeyInCombo(m_keyPressCombo, action.keyCode, keyName);

    m_hotkeyTriggerCombo->setCurrentIndex(static_cast<int>(action.hotkeyTrigger));
    m_hotkeyTriggerStack->setCurrentIndex(static_cast<int>(action.hotkeyTrigger));
    // Luôn nạp CẢ 2 sub-page từ field baseline của action (scrollDirection/scrollAmount, x/y/mouseButton)
    // bất kể hotkeyTrigger hiện tại là gì - cùng kiểu "nạp đồng thời cho mọi trang" đã làm với x/y (dùng
    // chung cho Click VÀ Hold) ở trên, tránh mất dữ liệu khi người dùng đổi qua lại giữa các lựa chọn.
    m_hotkeyScrollDirectionCombo->setCurrentIndex(static_cast<int>(action.scrollDirection));
    m_hotkeyScrollAmountSpin->setValue(action.scrollAmount);
    m_hotkeyClickButtonCombo->setCurrentIndex(static_cast<int>(action.mouseButton));
    m_hotkeyClickXSpin->setValue(action.x);
    m_hotkeyClickYSpin->setValue(action.y);

    m_scrollDirectionCombo->setCurrentIndex(static_cast<int>(action.scrollDirection));
    m_scrollAmountSpin->setValue(action.scrollAmount);

    m_loadingAction = false;
    if (m_dirty)
    {
        m_dirty = false;
        emit dirtyChanged(false);
    }
}

Action ActionEditorWidget::getAction() const
{
    Action act;
    act.type = static_cast<ActionType>(m_typeCombo->currentIndex());
    // Editor không có ô nào cho `enabled` - phải giữ nguyên giá trị của hành động đang sửa. Trước đây
    // Action mới tạo ở trên luôn mang enabled=true, nên "Lưu hành động" âm thầm BẬT LẠI một hành động đã
    // tắt trong hồ sơ.
    act.enabled = m_loadedEnabled;

    act.waitBefore = std::chrono::milliseconds(m_waitBeforeInput->valueMs());
    act.waitAfter = std::chrono::milliseconds(m_waitAfterInput->valueMs());
    act.duration = std::chrono::milliseconds(m_durationInput->valueMs());

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

    act.setModifiers(m_hotkeyModOrder);
    act.keyCode = m_hotkeyKeyCombo->currentData().toInt();
    act.keyName = keyNameFromCombo(m_hotkeyKeyCombo).toStdString();

    if (act.type == ActionType::KeyPress)
    {
        act.keyCode = m_keyPressCombo->currentData().toInt();
        act.keyName = keyNameFromCombo(m_keyPressCombo).toStdString();
    }

    act.scrollDirection = static_cast<ScrollDirection>(m_scrollDirectionCombo->currentIndex());
    act.scrollAmount = m_scrollAmountSpin->value();

    act.hotkeyTrigger = static_cast<HotkeyTrigger>(m_hotkeyTriggerCombo->currentIndex());
    if (act.type == ActionType::Hotkey && act.hotkeyTrigger == HotkeyTrigger::Scroll)
    {
        act.scrollDirection = static_cast<ScrollDirection>(m_hotkeyScrollDirectionCombo->currentIndex());
        act.scrollAmount = m_hotkeyScrollAmountSpin->value();
    }
    else if (act.type == ActionType::Hotkey && act.hotkeyTrigger == HotkeyTrigger::Click)
    {
        act.mouseButton = static_cast<MouseButtonType>(m_hotkeyClickButtonCombo->currentIndex());
        act.x = m_hotkeyClickXSpin->value();
        act.y = m_hotkeyClickYSpin->value();
    }

    return act;
}

void ActionEditorWidget::onPositionCaptured(int x, int y)
{
    // field 1/2 (điểm đầu/cuối Kéo chuột riêng lẻ) đã bỏ - trang Kéo chuột giờ chỉ còn "Bắt thao tác kéo
    // thật" (dragGestureCaptureRequested, xem onDragGestureCaptured), không còn emit captureRequested(1)/
    // (2) ở đâu nữa (v1.19.7).
    if (m_capturingField == 0) // click or hold
    {
        m_clickXSpin->setValue(x);
        m_clickYSpin->setValue(y);
        m_holdXSpin->setValue(x);
        m_holdYSpin->setValue(y);
    }
    else if (m_capturingField == 3) // Tổ hợp phím + Click chuột
    {
        m_hotkeyClickXSpin->setValue(x);
        m_hotkeyClickYSpin->setValue(y);
    }
}

void ActionEditorWidget::onDragGestureCaptured(int startX, int startY, int endX, int endY)
{
    m_dragStartXSpin->setValue(startX);
    m_dragStartYSpin->setValue(startY);
    m_dragEndXSpin->setValue(endX);
    m_dragEndYSpin->setValue(endY);
}

void ActionEditorWidget::onApplyClicked()
{
    emit actionSaved(getAction(), m_currentIndex);
    if (m_dirty)
    {
        m_dirty = false;
        emit dirtyChanged(false);
    }
}

void ActionEditorWidget::clear()
{
    m_currentIndex = -1;
    Action defaultAct;
    setAction(defaultAct, -1);
}
