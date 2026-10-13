#include "QRGenerateTab.h"

#include "QRHistoryStore.h"
#include "QRUiStyle.h"
#include "tools/vpn/engine/PublicIpChecker.h"
#include "tools/wifi/engine/WlanController.h"

#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace
{
QLineEdit* makeEdit(const QString& placeholder, QWidget* parent)
{
    auto* e = new QLineEdit(parent);
    e->setPlaceholderText(placeholder);
    return e;
}

QPlainTextEdit* makeMulti(const QString& placeholder, QWidget* parent, int height = 90)
{
    auto* e = new QPlainTextEdit(parent);
    e->setPlaceholderText(placeholder);
    e->setFixedHeight(height);
    e->setTabChangesFocus(true);
    return e;
}

QFormLayout* makeForm(QWidget* page)
{
    auto* f = new QFormLayout(page);
    f->setContentsMargins(0, 6, 0, 6);
    f->setSpacing(8);
    f->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    return f;
}
} // namespace

QRGenerateTab::QRGenerateTab(QWidget* parent)
    : QWidget(parent)
{
    m_timer = new QTimer(this);
    m_timer->setSingleShot(true);
    m_timer->setInterval(180);
    connect(m_timer, &QTimer::timeout, this, &QRGenerateTab::refresh);

    buildUi();
    updateColorButtons();
    refresh();
}

// ---------------------------------------------------------------- UI

QWidget* QRGenerateTab::makeTextPage()
{
    auto* w = new QWidget(this);
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0, 6, 0, 6);
    m_textEdit = makeMulti("Nhập văn bản bất kỳ (hỗ trợ tiếng Việt, emoji...)", w, 130);
    connect(m_textEdit, &QPlainTextEdit::textChanged, this, [this] {
        // Người dùng sửa ô văn bản: từ đây nội dung là thứ đang hiện trong ô, bản nguyên văn của "Tạo lại
        // mã" (xem setRawText) hết hiệu lực.
        if (!m_rawText.isNull() && m_textEdit->toPlainText() != m_rawTextShown)
            m_rawText = QString();
        scheduleRefresh();
    });
    l->addWidget(m_textEdit);
    return w;
}

QWidget* QRGenerateTab::makeUrlPage()
{
    auto* w = new QWidget(this);
    auto* f = makeForm(w);
    m_urlEdit = makeEdit("https://example.com", w);
    m_urlEdit->setObjectName("urlEdit");
    connect(m_urlEdit, &QLineEdit::textChanged, this, &QRGenerateTab::scheduleRefresh);
    f->addRow("Liên kết:", m_urlEdit);
    auto* hint = new QLabel("Nếu thiếu giao thức, https:// sẽ được tự thêm.", w);
    hint->setStyleSheet("color: #57606a; font-size: 11px; border: none; background: transparent;");
    f->addRow("", hint);
    return w;
}

QWidget* QRGenerateTab::makeWifiPage()
{
    auto* w = new QWidget(this);
    auto* f = makeForm(w);
    m_wifiSsid = makeEdit("Tên mạng WiFi (SSID)", w);
    m_wifiSsid->setObjectName("wifiSsid");
    m_wifiPass = makeEdit("Mật khẩu", w);
    m_wifiPass->setObjectName("wifiPass");
    // Che mật khẩu khi gõ (người đứng cạnh/đang chia sẻ màn hình), có nút 👁 để chủ động xem lại.
    m_wifiPass->setEchoMode(QLineEdit::Password);
    auto* wifiPassToggle = new QPushButton("👁", w);
    wifiPassToggle->setObjectName("wifiPassToggle");
    wifiPassToggle->setCheckable(true);
    wifiPassToggle->setFixedWidth(36);
    wifiPassToggle->setCursor(Qt::PointingHandCursor);
    wifiPassToggle->setToolTip("Hiện/ẩn mật khẩu");
    wifiPassToggle->setStyleSheet(QRUi::buttonStyle());
    connect(wifiPassToggle, &QPushButton::toggled, this, [this, wifiPassToggle](bool on) {
        m_wifiPass->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
        wifiPassToggle->setText(on ? "🙈" : "👁");
    });
    auto* wifiPassRow = new QWidget(w);
    auto* wifiPassLayout = new QHBoxLayout(wifiPassRow);
    wifiPassLayout->setContentsMargins(0, 0, 0, 0);
    wifiPassLayout->setSpacing(6);
    wifiPassLayout->addWidget(m_wifiPass, 1);
    wifiPassLayout->addWidget(wifiPassToggle);
    m_wifiSec = new QComboBox(w);
    m_wifiSec->addItem("WPA/WPA2/WPA3", "WPA");
    m_wifiSec->addItem("WEP", "WEP");
    m_wifiSec->addItem("Không mật khẩu", "nopass");
    m_wifiHidden = new QCheckBox("Mạng ẩn (hidden SSID)", w);
    connect(m_wifiSsid, &QLineEdit::textChanged, this, &QRGenerateTab::scheduleRefresh);
    connect(m_wifiPass, &QLineEdit::textChanged, this, &QRGenerateTab::scheduleRefresh);
    connect(m_wifiSec, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        m_wifiPass->setEnabled(m_wifiSec->currentData().toString() != "nopass");
        scheduleRefresh();
    });
    connect(m_wifiHidden, &QCheckBox::toggled, this, &QRGenerateTab::scheduleRefresh);

    // "Chọn từ WiFi đã lưu" - tự điền SSID/bảo mật/mật khẩu từ hồ sơ Windows đã lưu sẵn, đỡ phải tự gõ lại
    // (yêu cầu người dùng). Danh sách được nạp LẠI mỗi lần chọn loại WiFi (xem onTypeChanged()), không phải
    // một lần duy nhất ở đây - lúc constructor chạy, trang này còn chưa chắc được xem tới.
    m_wifiSavedCombo = new QComboBox(w);
    m_wifiSavedCombo->setStyleSheet(QRUi::inputStyle());
    connect(m_wifiSavedCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QRGenerateTab::onWifiProfileSelected);
    f->addRow("WiFi đã lưu:", m_wifiSavedCombo);
    m_wifiSavedStatus = new QLabel(w);
    m_wifiSavedStatus->setWordWrap(true);
    m_wifiSavedStatus->setStyleSheet("color: #9a6700; font-size: 11px; border: none; background: transparent;");
    m_wifiSavedStatus->setVisible(false);
    f->addRow("", m_wifiSavedStatus);
    f->addRow("SSID:", m_wifiSsid);
    f->addRow("Bảo mật:", m_wifiSec);
    f->addRow("Mật khẩu:", wifiPassRow);
    f->addRow("", m_wifiHidden);
    return w;
}

QWidget* QRGenerateTab::makePhonePage()
{
    auto* w = new QWidget(this);
    auto* f = makeForm(w);
    m_phoneNumber = makeEdit("+84912345678", w);
    connect(m_phoneNumber, &QLineEdit::textChanged, this, &QRGenerateTab::scheduleRefresh);
    f->addRow("Số điện thoại:", m_phoneNumber);
    return w;
}

QWidget* QRGenerateTab::makeGeoPage()
{
    auto* w = new QWidget(this);
    // Bố cục: MỖI hàng tọa độ = nhãn | ô giá trị | nút nằm ngay cạnh. Nút CO VỪA CHỮ (không giãn) để nhường chỗ
    // cho ô giá trị; ô giá trị có bề rộng tối thiểu tính theo ĐỘ RỘNG CHỮ THẬT của "-180.000000" (6 chữ số thập
    // phân, không dựa vào sizeHint - sizeHint lúc dựng chưa tính padding của stylesheet nên làm ô quá hẹp, cắt
    // mất chữ số cuối) và được giãn ra khi cột trái rộng hơn. Trước đây mỗi ô tọa độ chiếm trọn chiều ngang còn
    // hai nút xếp thành một hàng riêng - tổng chiều ngang vượt cột trái (cột trái là QScrollArea TẮT thanh cuộn
    // ngang) nên phần bên phải bị cắt mất. Mọi hàng dưới đây phải VỪA bề rộng tối thiểu của cột trái.
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 6, 0, 6);
    v->setSpacing(8);

    auto makeCoordLabel = [w](const QString& text, const QString& tip) {
        auto* l = new QLabel(text, w);
        l->setToolTip(tip);
        // Cùng một bề rộng nhãn cho cả hai hàng để các ô giá trị thẳng hàng với nhau.
        l->setFixedWidth(l->fontMetrics().horizontalAdvance("Kinh độ:") + 6);
        return l;
    };
    auto makeCoordSpin = [w](double lo, double hi, double value) {
        auto* sp = new QDoubleSpinBox(w);
        sp->setRange(lo, hi);
        sp->setDecimals(6);
        sp->setValue(value);
        sp->setButtonSymbols(QAbstractSpinBox::NoButtons);
        // Chữ rộng nhất ("-180.000000") + đệm ngang của stylesheet (8px mỗi bên) + viền (1px mỗi bên) + dư để
        // chữ số cuối không bị cắt (kể cả khi người dùng chỉnh DPI/cỡ chữ Windows).
        sp->setMinimumWidth(sp->fontMetrics().horizontalAdvance("-180.000000") + 16 + 2 + 20);
        sp->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        return sp;
    };
    // Nút thu gọn đệm ngang (8px thay vì 14px) để vừa cột trái ở cỡ cửa sổ nhỏ nhất.
    const QString compactButton = QRUi::buttonStyle() + "QPushButton { padding: 7px 8px; }";

    m_geoLat = makeCoordSpin(-90.0, 90.0, 21.028511);
    m_geoLon = makeCoordSpin(-180.0, 180.0, 105.804817);
    connect(m_geoLat, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &QRGenerateTab::scheduleRefresh);
    connect(m_geoLon, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &QRGenerateTab::scheduleRefresh);
    // "Lấy vị trí hiện tại": KHÔNG phải GPS thật - Windows chỉ cấp quyền "System location" cho app MSIX đã
    // duyệt, Win32 không đóng gói không gọi được dù có quyền Administrator (đã xác nhận ở module VPN &
    // Location, xem PublicIpChecker.h) - đây là vị trí ƯỚC LƯỢNG theo vùng phủ của địa chỉ IP công khai
    // (độ chính xác cỡ thành phố). Chữ trên nút được rút gọn cho vừa chiều ngang; lời cảnh báo "ước lượng
    // theo IP, không phải GPS" nằm ở tooltip, dòng chú thích bên dưới và thông báo kết quả.
    m_geoLocateButton = new QPushButton("📍 Lấy vị trí hiện tại", w);
    m_geoLocateButton->setToolTip("Ước lượng theo địa chỉ IP công khai (độ chính xác cỡ thành phố, KHÔNG phải GPS)");
    m_geoLocateButton->setStyleSheet(compactButton);
    m_geoMapButton = new QPushButton("🗺 Mở bản đồ", w);
    m_geoMapButton->setToolTip("Mở Google Maps tại tọa độ đang nhập bằng trình duyệt mặc định");
    m_geoMapButton->setStyleSheet(compactButton);
    // Nút CO VỪA CHỮ (đúng sizeHint, không giãn) - nhường chỗ cho hai ô tọa độ.
    for (QPushButton* b : {m_geoLocateButton, m_geoMapButton})
        b->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    connect(m_geoLocateButton, &QPushButton::clicked, this, &QRGenerateTab::onLocateMe);
    connect(m_geoMapButton, &QPushButton::clicked, this, &QRGenerateTab::onOpenMap);

    // Lưới 3 cột: nhãn | ô giá trị (cột giãn) | nút (co vừa chữ, canh trái) - hai ô tọa độ luôn cùng bề rộng
    // và thẳng hàng với nhau dù hai nút dài ngắn khác nhau.
    auto* coordGrid = new QGridLayout();
    coordGrid->setHorizontalSpacing(8);
    coordGrid->setVerticalSpacing(8);
    coordGrid->setColumnStretch(1, 1);
    coordGrid->addWidget(makeCoordLabel("Vĩ độ:", "Vĩ độ (Latitude), từ -90 đến 90"), 0, 0);
    coordGrid->addWidget(m_geoLat, 0, 1);
    coordGrid->addWidget(m_geoLocateButton, 0, 2, Qt::AlignLeft);
    coordGrid->addWidget(makeCoordLabel("Kinh độ:", "Kinh độ (Longitude), từ -180 đến 180"), 1, 0);
    coordGrid->addWidget(m_geoLon, 1, 1);
    coordGrid->addWidget(m_geoMapButton, 1, 2, Qt::AlignLeft);
    v->addLayout(coordGrid);

    // "Dán link Google Maps để lấy vị trí" (yêu cầu người dùng). Link ĐẦY ĐỦ: đọc tọa độ trực tiếp từ URL,
    // không gọi mạng. Link RÚT GỌN (maps.app.goo.gl/...): URL không chứa tọa độ nên phải mở 1 yêu cầu tới
    // Google để đọc đích chuyển hướng - xem resolveShortMapsLink().
    auto* linkCaption = new QLabel("Link Google Maps (kể cả link rút gọn maps.app.goo.gl):", w);
    linkCaption->setWordWrap(true);
    v->addWidget(linkCaption);
    auto* mapsLinkRow = new QHBoxLayout();
    mapsLinkRow->setSpacing(8);
    m_mapsLinkEdit = new QLineEdit(w);
    m_mapsLinkEdit->setPlaceholderText("Dán link Google Maps vào đây");
    m_mapsLinkEdit->setStyleSheet(QRUi::inputStyle());
    m_mapsLinkApplyButton = new QPushButton("Dùng link này", w);
    m_mapsLinkApplyButton->setStyleSheet(compactButton);
    mapsLinkRow->addWidget(m_mapsLinkEdit, 1);
    mapsLinkRow->addWidget(m_mapsLinkApplyButton);
    connect(m_mapsLinkApplyButton, &QPushButton::clicked, this, &QRGenerateTab::onApplyMapsLink);
    connect(m_mapsLinkEdit, &QLineEdit::returnPressed, this, &QRGenerateTab::onApplyMapsLink);
    v->addLayout(mapsLinkRow);

    m_geoLocateStatus = new QLabel("Vị trí hiện tại lấy theo IP công khai (cỡ thành phố, không phải GPS).", w);
    m_geoLocateStatus->setWordWrap(true);
    m_geoLocateStatus->setStyleSheet("color: #57606a; font-size: 11px; border: none; background: transparent;");
    v->addWidget(m_geoLocateStatus);
    return w;
}

void QRGenerateTab::buildUi()
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(12);

    // ---------- Cột trái: form (cuộn dọc) ----------
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background: transparent; border: none; }");
    scroll->setMinimumWidth(360);

    auto* left = new QWidget(scroll);
    left->setStyleSheet("background: transparent;");
    auto* leftLayout = new QVBoxLayout(left);
    leftLayout->setContentsMargins(0, 0, 6, 0);
    leftLayout->setSpacing(10);

    auto* contentGroup = new QGroupBox("Nội dung", left);
    contentGroup->setStyleSheet(QRUi::groupStyle() + QRUi::inputStyle());
    auto* contentLayout = new QVBoxLayout(contentGroup);

    // Chỉ 5 loại theo yêu cầu người dùng: Văn bản, URL, WiFi, SĐT, Vị trí (Geo) - Email/SMS/vCard vẫn còn
    // NHẬN DIỆN được lúc QUÉT (QRPayload::parse() không đổi gì) và "Tạo lại mã" từ Lịch sử vẫn hoạt động
    // (luôn đi qua trang Văn bản nguyên văn, xem setRawText()) - chỉ bỏ khỏi danh sách lúc TẠO MỚI.
    m_typeCombo = new QComboBox(contentGroup);
    const QList<QRContentType> types = {QRContentType::Text, QRContentType::Url, QRContentType::Wifi,
                                        QRContentType::Phone, QRContentType::Geo};
    for (QRContentType t : types)
        m_typeCombo->addItem(QRPayload::typeName(t), static_cast<int>(t));
    m_typeCombo->setObjectName("typeCombo");
    contentLayout->addWidget(m_typeCombo);

    m_pages = new QStackedWidget(contentGroup);
    m_pages->addWidget(makeTextPage());
    m_pages->addWidget(makeUrlPage());
    m_pages->addWidget(makeWifiPage());
    m_pages->addWidget(makePhonePage());
    m_pages->addWidget(makeGeoPage());
    contentLayout->addWidget(m_pages);
    leftLayout->addWidget(contentGroup);
    for (int i = 1; i < m_pages->count(); ++i)
        m_pages->widget(i)->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Ignored);
    m_pages->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::MinimumExpanding);
    connect(m_typeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QRGenerateTab::onTypeChanged);

    // ---------- Tùy chọn ----------
    auto* optGroup = new QGroupBox("Tùy chọn mã QR", left);
    optGroup->setStyleSheet(QRUi::groupStyle() + QRUi::inputStyle());
    auto* optForm = new QFormLayout(optGroup);
    optForm->setSpacing(8);

    m_eccCombo = new QComboBox(optGroup);
    for (QREcc e : {QREcc::Low, QREcc::Medium, QREcc::Quartile, QREcc::High})
        m_eccCombo->addItem("Sửa lỗi " + QRCodec::eccName(e), static_cast<int>(e));
    m_eccCombo->setObjectName("eccCombo");
    m_eccCombo->setCurrentIndex(1);
    connect(m_eccCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &QRGenerateTab::scheduleRefresh);
    optForm->addRow("Mức sửa lỗi:", m_eccCombo);

    m_sizeSpin = new QSpinBox(optGroup);
    m_sizeSpin->setRange(100, 4096);
    m_sizeSpin->setSingleStep(50);
    m_sizeSpin->setObjectName("sizeSpin");
    m_sizeSpin->setValue(512);
    m_sizeSpin->setSuffix(" px");
    m_sizeSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connect(m_sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &QRGenerateTab::scheduleRefresh);
    optForm->addRow("Kích thước ảnh:", m_sizeSpin);

    m_quietSpin = new QSpinBox(optGroup);
    m_quietSpin->setRange(0, 16);
    m_quietSpin->setValue(4);
    m_quietSpin->setSuffix(" ô");
    m_quietSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    connect(m_quietSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &QRGenerateTab::scheduleRefresh);
    optForm->addRow("Viền trắng:", m_quietSpin);

    auto* colorRow = new QHBoxLayout();
    m_fgButton = new QPushButton(optGroup);
    m_bgButton = new QPushButton(optGroup);
    m_swapButton = new QPushButton("⇄", optGroup);
    m_swapButton->setObjectName("swapButton");
    m_swapButton->setToolTip("Đổi chỗ màu mã và màu nền");
    m_swapButton->setFixedWidth(36);
    for (QPushButton* b : {m_fgButton, m_bgButton, m_swapButton})
        b->setCursor(Qt::PointingHandCursor);
    m_swapButton->setStyleSheet(QRUi::buttonStyle());
    colorRow->addWidget(m_fgButton, 1);
    colorRow->addWidget(m_bgButton, 1);
    colorRow->addWidget(m_swapButton);
    connect(m_fgButton, &QPushButton::clicked, this, &QRGenerateTab::onPickForeground);
    connect(m_bgButton, &QPushButton::clicked, this, &QRGenerateTab::onPickBackground);
    connect(m_swapButton, &QPushButton::clicked, this, &QRGenerateTab::onSwapColors);
    optForm->addRow("Màu mã / nền:", colorRow);

    m_roundedCheck = new QCheckBox("Ô bo tròn", optGroup);
    connect(m_roundedCheck, &QCheckBox::toggled, this, &QRGenerateTab::scheduleRefresh);
    optForm->addRow("Kiểu ô:", m_roundedCheck);

    auto* logoRow = new QHBoxLayout();
    m_logoButton = new QPushButton("Chọn logo...", optGroup);
    m_logoClearButton = new QPushButton("Xóa", optGroup);
    m_logoButton->setStyleSheet(QRUi::buttonStyle());
    m_logoClearButton->setStyleSheet(QRUi::buttonStyle());
    m_logoClearButton->setEnabled(false);
    logoRow->addWidget(m_logoButton, 1);
    logoRow->addWidget(m_logoClearButton);
    connect(m_logoButton, &QPushButton::clicked, this, &QRGenerateTab::onPickLogo);
    connect(m_logoClearButton, &QPushButton::clicked, this, &QRGenerateTab::onClearLogo);
    optForm->addRow("Logo giữa mã:", logoRow);

    m_logoSlider = new QSlider(Qt::Horizontal, optGroup);
    m_logoSlider->setRange(8, 30);
    m_logoSlider->setValue(20);
    m_logoSlider->setEnabled(false);
    m_logoLabel = new QLabel("20%", optGroup);
    m_logoLabel->setStyleSheet("border: none; background: transparent; color: #57606a;");
    auto* sliderRow = new QHBoxLayout();
    sliderRow->addWidget(m_logoSlider, 1);
    sliderRow->addWidget(m_logoLabel);
    connect(m_logoSlider, &QSlider::valueChanged, this, [this](int v) {
        m_logoLabel->setText(QString("%1%").arg(v));
        scheduleRefresh();
    });
    optForm->addRow("Cỡ logo:", sliderRow);

    leftLayout->addWidget(optGroup);
    leftLayout->addStretch();
    scroll->setWidget(left);
    root->addWidget(scroll, 4);

    // ---------- Cột phải: xem trước ----------
    auto* right = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(8);

    m_preview = new QLabel(right);
    m_preview->setAlignment(Qt::AlignCenter);
    m_preview->setWordWrap(true);
    m_preview->setMinimumSize(280, 280);
    m_preview->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_preview->setStyleSheet(QRUi::cardStyle() + " color: #8c959f; font-size: 13px; padding: 12px;");
    rightLayout->addWidget(m_preview, 1);

    m_info = new QLabel(right);
    m_info->setAlignment(Qt::AlignCenter);
    m_info->setStyleSheet("color: #57606a; font-size: 11px; border: none; background: transparent;");
    rightLayout->addWidget(m_info);

    m_warning = new QLabel(right);
    m_warning->setAlignment(Qt::AlignCenter);
    m_warning->setWordWrap(true);
    m_warning->setStyleSheet("color: #9a6700; font-size: 11px; border: none; background: transparent;");
    rightLayout->addWidget(m_warning);

    auto* btnRow = new QHBoxLayout();
    m_saveButton = new QPushButton("💾 Lưu ảnh...", right);
    m_copyButton = new QPushButton("📋 Sao chép ảnh", right);
    m_saveButton->setStyleSheet(QRUi::primaryButtonStyle());
    m_copyButton->setStyleSheet(QRUi::buttonStyle());
    m_saveButton->setCursor(Qt::PointingHandCursor);
    m_copyButton->setCursor(Qt::PointingHandCursor);
    connect(m_saveButton, &QPushButton::clicked, this, &QRGenerateTab::onSave);
    connect(m_copyButton, &QPushButton::clicked, this, &QRGenerateTab::onCopy);
    btnRow->addWidget(m_saveButton, 1);
    btnRow->addWidget(m_copyButton, 1);
    rightLayout->addLayout(btnRow);

    root->addWidget(right, 5);
}

// ---------------------------------------------------------------- Nội dung

QRContentType QRGenerateTab::currentType() const
{
    return static_cast<QRContentType>(m_typeCombo->currentData().toInt());
}

QString QRGenerateTab::buildContent(QString* problem) const
{
    auto fail = [problem](const QString& msg) {
        if (problem) *problem = msg;
        return QString();
    };

    switch (currentType())
    {
        case QRContentType::Text:
        {
            if (!m_rawText.isNull())
                return QRPayload::makeText(m_rawText); // nguyên văn từ "Tạo lại mã", ô văn bản chưa bị sửa
            const QString t = m_textEdit->toPlainText();
            if (t.isEmpty())
                return fail("Nhập nội dung ở bên trái để tạo mã QR.");
            return QRPayload::makeText(t);
        }
        case QRContentType::Url:
        {
            if (m_urlEdit->text().trimmed().isEmpty())
                return fail("Nhập liên kết để tạo mã QR.");
            return QRPayload::makeUrl(m_urlEdit->text());
        }
        case QRContentType::Wifi:
        {
            if (m_wifiSsid->text().isEmpty())
                return fail("Nhập tên mạng WiFi (SSID).");
            WifiInfo w;
            w.ssid = m_wifiSsid->text();
            w.password = m_wifiPass->text();
            w.security = m_wifiSec->currentData().toString();
            w.hidden = m_wifiHidden->isChecked();
            if (w.security != "nopass" && w.password.isEmpty())
                return fail("Nhập mật khẩu WiFi, hoặc chọn \"Không mật khẩu\".");
            return QRPayload::makeWifi(w);
        }
        case QRContentType::Phone:
        {
            if (m_phoneNumber->text().trimmed().isEmpty())
                return fail("Nhập số điện thoại.");
            return QRPayload::makePhone(m_phoneNumber->text());
        }
        case QRContentType::Geo:
            return QRPayload::makeGeo({m_geoLat->value(), m_geoLon->value()});
        // Email/SMS/vCard: không còn trong m_typeCombo (chỉ 5 loại theo yêu cầu người dùng, xem buildUi())
        // nên currentType() không bao giờ trả về các giá trị này qua đường UI bình thường - vẫn giữ lại
        // trong enum/QRPayload::makeXxx() vì QUÉT mã vẫn cần nhận diện được (QRPayload::parse() không đổi).
        case QRContentType::Email:
        case QRContentType::Sms:
        case QRContentType::VCard:
            break;
    }
    return fail("Loại nội dung không hợp lệ.");
}

QString QRGenerateTab::currentContent() const
{
    return m_content;
}

void QRGenerateTab::setRawText(const QString& text)
{
    m_typeCombo->setCurrentIndex(0); // Văn bản
    m_rawText = QString();
    m_textEdit->setPlainText(text);
    // QPlainTextEdit không giữ nguyên văn: toPlainText() đổi CRLF/CR/U+2028/U+2029 thành LF và khoảng trắng
    // không ngắt (U+00A0) thành dấu cách. "Tạo lại mã" từ một mã đã quét (vd vCard dùng CRLF) vì thế từng
    // sinh ra mã mang nội dung KHÁC mã gốc. Giữ bản nguyên văn và dùng nó chừng nào ô văn bản chưa bị sửa.
    m_rawTextShown = m_textEdit->toPlainText();
    if (!text.isEmpty() && m_rawTextShown != text)
        m_rawText = text;
    refresh();
}

QRStyle QRGenerateTab::currentStyle() const
{
    QRStyle s;
    s.targetSize = m_sizeSpin->value();
    s.quietZone = m_quietSpin->value();
    s.foreground = m_fg;
    s.background = m_bg;
    s.roundedModules = m_roundedCheck->isChecked();
    s.logo = m_logo;
    s.logoRatio = m_logoSlider->value() / 100.0;
    return s;
}

// ---------------------------------------------------------------- Xem trước

void QRGenerateTab::setPreviewMessage(const QString& msg)
{
    m_preview->setPixmap(QPixmap());
    m_preview->setText(msg);
}

void QRGenerateTab::scheduleRefresh()
{
    m_timer->start();
}

void QRGenerateTab::onTypeChanged(int index)
{
    m_pages->setCurrentIndex(index);
    // Chỉ trang đang hiển thị quyết định chiều cao, tránh khoảng trống do trang cao nhất
    for (int i = 0; i < m_pages->count(); ++i)
        m_pages->widget(i)->setSizePolicy(QSizePolicy::Preferred, i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    m_pages->updateGeometry();
    m_pages->adjustSize();
    // CHỈ nạp danh sách "WiFi đã lưu" một lần (lần đầu chuyển sang loại WiFi), không phải mỗi lần - đọc
    // mật khẩu dạng chữ cho MỌI hồ sơ không rẻ trên máy có nhiều hồ sơ đã lưu (xem comment m_wifiProfilesLoaded).
    if (currentType() == QRContentType::Wifi && !m_wifiProfilesLoaded)
    {
        refreshWifiSavedProfiles();
        m_wifiProfilesLoaded = true;
    }
    refresh();
}

void QRGenerateTab::refresh()
{
    m_timer->stop();

    m_matrix = QRMatrix();
    m_image = QImage();
    m_content.clear();
    m_info->clear();
    m_warning->clear();

    QString problem;
    const QString content = buildContent(&problem);
    if (content.isEmpty())
    {
        setPreviewMessage(problem);
        m_saveButton->setEnabled(false);
        m_copyButton->setEnabled(false);
        return;
    }

    const bool hasLogo = !m_logo.isNull();
    const QREcc ecc = hasLogo ? QREcc::High : static_cast<QREcc>(m_eccCombo->currentData().toInt());
    m_eccCombo->setEnabled(!hasLogo);

    QString error;
    const QRMatrix matrix = QRCodec::encode(content, ecc, &error);
    if (!matrix.isValid())
    {
        setPreviewMessage("⚠ " + error);
        m_saveButton->setEnabled(false);
        m_copyButton->setEnabled(false);
        return;
    }

    const QRStyle style = currentStyle();
    m_matrix = matrix;
    m_image = QRCodec::render(matrix, style);
    m_content = content;
    m_saveButton->setEnabled(true);
    m_copyButton->setEnabled(true);

    m_info->setText(QString("Version %1 • %2×%2 ô • Sửa lỗi %3%4 • Ảnh %5×%5 px")
                        .arg(matrix.version).arg(matrix.size).arg(QRCodec::eccName(matrix.ecc))
                        .arg(matrix.ecc != ecc ? " (tự nâng)" : "").arg(m_image.width()));

    QStringList warnings;
    if (hasLogo)
        warnings << "Có logo: tự dùng mức sửa lỗi H để mã vẫn quét được.";
    if (QRCodec::contrastRatio(m_fg, m_bg) < 3.0)
        warnings << "⚠ Độ tương phản giữa màu mã và nền thấp, mã có thể khó quét.";
    else if (m_fg.lightnessF() > m_bg.lightnessF())
        warnings << "ℹ Màu mã sáng hơn nền (đảo màu): một số ứng dụng quét không đọc được.";
    if (m_quietSpin->value() < 4)
        warnings << "ℹ Viền trắng dưới 4 ô có thể làm giảm khả năng quét.";
    m_warning->setText(warnings.join("\n"));

    // Kiểm tra lại: giải mã ngay ảnh vừa tạo để bảo đảm quét được
    const QList<QRDecoded> verify = QRCodec::decode(m_image);
    if (verify.isEmpty() || verify.first().text != content)
    {
        const QString extra = "⚠ Mã vừa tạo không tự quét lại được (màu/logo/kiểu ô quá khắt khe). Hãy tăng tương phản, giảm cỡ logo hoặc tắt ô bo tròn.";
        m_warning->setText(m_warning->text().isEmpty() ? extra : m_warning->text() + "\n" + extra);
    }

    updatePreviewPixmap();
}

void QRGenerateTab::updatePreviewPixmap()
{
    if (m_image.isNull())
        return;
    const QSize box = m_preview->size() - QSize(24, 24);
    const bool upscale = m_image.width() < box.width() && m_image.height() < box.height();
    m_preview->setText(QString());
    m_preview->setPixmap(QPixmap::fromImage(
        m_image.scaled(box, Qt::KeepAspectRatio, upscale ? Qt::FastTransformation : Qt::SmoothTransformation)));
}

void QRGenerateTab::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updatePreviewPixmap();
}

// ---------------------------------------------------------------- Màu / logo

void QRGenerateTab::updateColorButtons()
{
    auto style = [](const QColor& c) {
        const QString text = c.lightnessF() > 0.55 ? "#1f2328" : "#ffffff";
        return QString("QPushButton { background-color: %1; color: %2; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 8px; font-size: 11px; font-weight: bold; }"
                       "QPushButton:hover { border-color: #0969da; }")
            .arg(c.name(), text);
    };
    m_fgButton->setText("Mã " + m_fg.name().toUpper());
    m_bgButton->setText("Nền " + m_bg.name().toUpper());
    m_fgButton->setStyleSheet(style(m_fg));
    m_bgButton->setStyleSheet(style(m_bg));
}

void QRGenerateTab::onPickForeground()
{
    const QColor c = QColorDialog::getColor(m_fg, this, "Chọn màu mã QR");
    if (c.isValid())
    {
        m_fg = c;
        updateColorButtons();
        refresh();
    }
}

void QRGenerateTab::onPickBackground()
{
    const QColor c = QColorDialog::getColor(m_bg, this, "Chọn màu nền");
    if (c.isValid())
    {
        m_bg = c;
        updateColorButtons();
        refresh();
    }
}

void QRGenerateTab::onSwapColors()
{
    std::swap(m_fg, m_bg);
    updateColorButtons();
    refresh();
}

void QRGenerateTab::onPickLogo()
{
    const QString path = QFileDialog::getOpenFileName(this, "Chọn logo", QString(),
                                                      "Ảnh (*.png *.jpg *.jpeg *.bmp *.gif *.svg *.ico);;Tất cả (*.*)");
    if (path.isEmpty())
        return;

    QImage img(path);
    if (img.isNull())
    {
        QMessageBox::warning(this, "Logo", "Không đọc được file ảnh này.");
        return;
    }
    if (img.width() > 512 || img.height() > 512)
        img = img.scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    m_logo = img.convertToFormat(QImage::Format_ARGB32);
    m_logoClearButton->setEnabled(true);
    m_logoSlider->setEnabled(true);
    refresh();
}

void QRGenerateTab::onClearLogo()
{
    m_logo = QImage();
    m_logoClearButton->setEnabled(false);
    m_logoSlider->setEnabled(false);
    refresh();
}

// ---------------------------------------------------------------- Lưu / sao chép

bool QRGenerateTab::saveTo(const QString& pathIn, QString* error)
{
    auto fail = [error](const QString& msg) {
        if (error) *error = msg;
        return false;
    };

    if (!m_matrix.isValid() || m_image.isNull())
        return fail("Chưa có mã QR hợp lệ để lưu.");

    QString path = pathIn;
    QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix.isEmpty())
    {
        suffix = "png";
        path += ".png";
    }

    if (suffix == "svg")
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
            return fail("Không thể ghi file: " + f.errorString());
        f.write(QRCodec::toSvg(m_matrix, currentStyle()).toUtf8());
    }
    else
    {
        QImage out = m_image;
        if (suffix == "jpg" || suffix == "jpeg" || suffix == "bmp")
            out = m_image.convertToFormat(QImage::Format_RGB32);
        if (!out.save(path))
            return fail("Không thể lưu ảnh (định dạng không hỗ trợ hoặc không có quyền ghi).");
    }

    QRHistoryStore::instance().add("generate", QRPayload::typeName(currentType()), m_content);
    return true;
}

void QRGenerateTab::onSave()
{
    if (m_image.isNull())
        return;

    QString dir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (dir.isEmpty())
        dir = QDir::homePath();
    QString selected;
    const QString path = QFileDialog::getSaveFileName(
        this, "Lưu mã QR", dir + "/qrcode.png",
        "PNG (*.png);;JPEG (*.jpg *.jpeg);;BMP (*.bmp);;SVG vector (*.svg)", &selected);
    if (path.isEmpty())
        return;

    QString err;
    if (saveTo(path, &err))
        QMessageBox::information(this, "Lưu mã QR", "Đã lưu: " + path);
    else
        QMessageBox::critical(this, "Lỗi", err);
}

bool QRGenerateTab::copyToClipboard()
{
    if (m_image.isNull())
        return false;
    QGuiApplication::clipboard()->setImage(m_image);
    QRHistoryStore::instance().add("generate", QRPayload::typeName(currentType()), m_content);
    return true;
}

void QRGenerateTab::onCopy()
{
    if (copyToClipboard())
    {
        m_info->setText("✓ Đã sao chép ảnh mã QR vào clipboard");
        QTimer::singleShot(2500, this, [this] {
            if (!m_matrix.isValid())
                return;
            m_info->setText(QString("Version %1 • %2×%2 ô • Sửa lỗi %3 • Ảnh %4×%4 px")
                                .arg(m_matrix.version).arg(m_matrix.size)
                                .arg(QRCodec::eccName(m_matrix.ecc)).arg(m_image.width()));
        });
    }
}

// ---------------------------------------------------------------- WiFi đã lưu / Vị trí

void QRGenerateTab::refreshWifiSavedProfiles()
{
    m_wifiSavedCombo->blockSignals(true);
    m_wifiSavedCombo->clear();
    m_wifiSavedCombo->addItem("— Chọn từ WiFi đã lưu trên máy (tùy chọn) —", -1);
    m_wifiSavedProfiles.clear();

    if (!m_wlan)
        m_wlan = new WlanController(this);
    if (!m_wlan->isOpen() && !m_wlan->open())
    {
        m_wifiSavedCombo->blockSignals(false);
        return; // không có dịch vụ WLAN trên máy - để trống, vẫn nhập tay bình thường
    }

    const QList<WifiAdapterInfo> adapters = m_wlan->adapters();
    if (!adapters.isEmpty())
    {
        // withPlaintextPassword=true: cần quyền Administrator để ĐỌC ĐƯỢC mật khẩu thật - nếu không mỗi hồ
        // sơ tự có passwordAccessDenied=true (không làm hỏng cả danh sách), xử lý ở onWifiProfileSelected().
        const QList<WifiProfile> profiles = m_wlan->profiles(adapters.first().guidString, true);
        for (const WifiProfile& p : profiles)
        {
            m_wifiSavedProfiles << p;
            m_wifiSavedCombo->addItem(p.name, m_wifiSavedProfiles.size() - 1);
        }
    }
    m_wifiSavedCombo->blockSignals(false);
}

void QRGenerateTab::onWifiProfileSelected(int index)
{
    const int dataIdx = m_wifiSavedCombo->itemData(index).toInt();
    if (dataIdx < 0 || dataIdx >= m_wifiSavedProfiles.size())
    {
        m_wifiSavedStatus->setVisible(false); // mục mặc định "— Chọn từ WiFi đã lưu... —" - không điền gì
        return;
    }

    const WifiProfile& p = m_wifiSavedProfiles[dataIdx];
    m_wifiSsid->setText(p.ssid.isEmpty() ? p.name : p.ssid);

    QString secData = "WPA";
    if (p.security == WifiSecurity::Open)
        secData = "nopass";
    else if (p.security == WifiSecurity::Wep)
        secData = "WEP";
    // WpaPsk/Wpa2Psk/Wpa3Sae/Enterprise/Unknown: định dạng WIFI: chỉ có một mã "WPA" chung cho mọi thế hệ
    // WPA-Personal, không phân biệt được - đây là lựa chọn mặc định hợp lý nhất, người dùng vẫn tự đổi lại.
    const int secIdx = m_wifiSec->findData(secData);
    if (secIdx >= 0)
        m_wifiSec->setCurrentIndex(secIdx);

    if (p.hasPassword)
    {
        m_wifiPass->setText(p.password);
        m_wifiSavedStatus->setVisible(false);
    }
    else
    {
        m_wifiPass->clear();
        // KHÔNG dùng QMessageBox (hộp thoại chặn) ở đây - chỉ là một gợi ý, không phải lỗi cần người dùng
        // xác nhận; hộp thoại chặn còn có nguy cơ treo kiểm thử tự động nếu chạy trên máy thật có hồ sơ
        // thiếu quyền đọc mật khẩu. Hiện ngay dưới combo, tự ẩn khi chọn mục khác không bị thiếu quyền.
        m_wifiSavedStatus->setText(
            p.passwordAccessDenied
                ? "⚠ Windows từ chối cấp mật khẩu dạng chữ cho hồ sơ này (cần quyền Administrator để đọc) - "
                  "đã điền SSID/bảo mật, hãy tự nhập mật khẩu."
                : "ℹ Hồ sơ này không có mật khẩu lưu sẵn - hãy tự nhập nếu cần.");
        m_wifiSavedStatus->setVisible(true);
    }
    scheduleRefresh();
}

void QRGenerateTab::onLocateMe()
{
    if (!m_ipChecker)
    {
        m_ipChecker = new PublicIpChecker(this);
        connect(m_ipChecker, &PublicIpChecker::result, this, [this](PublicIpInfo info) {
            m_geoLocateButton->setEnabled(true);
            if (!info.hasLocation)
            {
                m_geoLocateStatus->setText("⚠ Dịch vụ tra cứu không trả về tọa độ cho địa chỉ IP này.");
                return;
            }
            m_geoLat->setValue(info.latitude);
            m_geoLon->setValue(info.longitude);
            const QString place = info.city.isEmpty() ? info.country : QString("%1, %2").arg(info.city, info.country);
            m_geoLocateStatus->setText(
                QString("✓ Vị trí ƯỚC LƯỢNG theo IP công khai (%1)%2 - độ chính xác cỡ thành phố, KHÔNG phải "
                        "GPS chính xác. Có thể tự sửa lại tọa độ bên trên nếu biết rõ hơn.")
                    .arg(info.ip, place.isEmpty() ? QString() : (" • " + place)));
        });
        connect(m_ipChecker, &PublicIpChecker::errorOccurred, this, [this](QString message) {
            m_geoLocateButton->setEnabled(true);
            m_geoLocateStatus->setText("⚠ " + message);
        });
    }
    m_geoLocateButton->setEnabled(false);
    m_geoLocateStatus->setText("Đang tra cứu vị trí theo địa chỉ IP công khai...");
    m_ipChecker->check();
}

void QRGenerateTab::onOpenMap()
{
    const QString url = QString("https://www.google.com/maps/search/?api=1&query=%1,%2")
                             .arg(m_geoLat->value(), 0, 'f', 6)
                             .arg(m_geoLon->value(), 0, 'f', 6);
    if (!QDesktopServices::openUrl(QUrl(url)))
        m_geoLocateStatus->setText("⚠ Không mở được trình duyệt để xem bản đồ.");
}

void QRGenerateTab::cancelMapsLookup()
{
    if (m_mapsReply)
    {
        QNetworkReply* r = m_mapsReply;
        m_mapsReply = nullptr; // đặt TRƯỚC abort(): finished() phát đồng bộ, lambda thấy khác m_mapsReply và thoát
        r->abort();
    }
    if (m_mapsLinkApplyButton)
        m_mapsLinkApplyButton->setEnabled(true);
}

void QRGenerateTab::applyMapsCoordinates(double lat, double lon, const QString& what)
{
    m_geoLat->setValue(lat);
    m_geoLon->setValue(lon);
    m_geoLocateStatus->setText(
        QString("✓ Đã đọc tọa độ (%1, %2) từ %3.").arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6).arg(what));
}

void QRGenerateTab::onApplyMapsLink()
{
    cancelMapsLookup(); // đang dở một lần mở link rút gọn trước đó thì bỏ, ưu tiên link vừa bấm
    const QString text = m_mapsLinkEdit->text().trimmed();
    double lat = 0.0, lon = 0.0;
    if (QRPayload::parseGoogleMapsCoordinates(text, &lat, &lon))
    {
        applyMapsCoordinates(lat, lon, "link Google Maps");
        return;
    }
    if (QRPayload::isGoogleMapsShortLink(text))
    {
        resolveShortMapsLink(QRPayload::normalizeLinkUrl(text), 0);
        return;
    }
    m_geoLocateStatus->setText(
        "⚠ Không đọc được tọa độ từ link này - hãy dán link Google Maps dạng \".../@vĩ_độ,kinh_độ,...\", "
        "\"?q=vĩ_độ,kinh_độ\" hoặc link rút gọn maps.app.goo.gl.");
}

void QRGenerateTab::resolveShortMapsLink(const QUrl& url, int hop)
{
    if (!m_mapsNet)
        m_mapsNet = new QNetworkAccessManager(this);
    QNetworkRequest req(url);
    // KHÔNG tự theo chuyển hướng: chỉ cần header Location của từng bước (không tải trang đích), và mỗi bước
    // phải được kiểm host Google trước khi đi tiếp (isAllowedMapsRedirectUrl).
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setTransferTimeout(10000);
    m_mapsLinkApplyButton->setEnabled(false);
    m_geoLocateStatus->setText("Đang mở link rút gọn để đọc tọa độ...");

    QNetworkReply* reply = m_mapsNet->get(req);
    m_mapsReply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, hop]() {
        reply->deleteLater();
        if (reply != m_mapsReply)
            return; // đã bị hủy / thay bằng yêu cầu mới
        m_mapsReply = nullptr;
        m_mapsLinkApplyButton->setEnabled(true);

        const QVariant redirect = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
        if (!redirect.isValid())
        {
            const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (http == 404 || http == 410)
                m_geoLocateStatus->setText("⚠ Link rút gọn này không tồn tại hoặc đã hết hạn - kiểm tra lại link.");
            else if (reply->error() != QNetworkReply::NoError)
                m_geoLocateStatus->setText(
                    QString("⚠ Không mở được link rút gọn (%1) - kiểm tra kết nối mạng hoặc dán link đầy đủ.")
                        .arg(reply->errorString()));
            else
                m_geoLocateStatus->setText("⚠ Link rút gọn này không dẫn tới vị trí nào.");
            return;
        }
        const QUrl target = reply->url().resolved(redirect.toUrl());
        double lat = 0.0, lon = 0.0;
        if (QRPayload::parseGoogleMapsCoordinates(target.toString(QUrl::FullyEncoded), &lat, &lon))
        {
            applyMapsCoordinates(lat, lon, "link rút gọn Google Maps");
            return;
        }
        if (hop < 5 && QRPayload::isAllowedMapsRedirectUrl(target))
        {
            resolveShortMapsLink(target, hop + 1);
            return;
        }
        m_geoLocateStatus->setText("⚠ Link rút gọn này không chứa tọa độ (không phải link địa điểm?) - "
                                   "hãy mở nó trên trình duyệt rồi dán link đầy đủ.");
    });
}
