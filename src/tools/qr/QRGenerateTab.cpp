#include "QRGenerateTab.h"

#include "QRHistoryStore.h"
#include "QRUiStyle.h"

#include <QCheckBox>
#include <QClipboard>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTimer>
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
    connect(m_textEdit, &QPlainTextEdit::textChanged, this, &QRGenerateTab::scheduleRefresh);
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
    f->addRow("SSID:", m_wifiSsid);
    f->addRow("Bảo mật:", m_wifiSec);
    f->addRow("Mật khẩu:", wifiPassRow);
    f->addRow("", m_wifiHidden);
    return w;
}

QWidget* QRGenerateTab::makeEmailPage()
{
    auto* w = new QWidget(this);
    auto* f = makeForm(w);
    m_mailTo = makeEdit("nguoinhan@example.com", w);
    m_mailSubject = makeEdit("Tiêu đề (tùy chọn)", w);
    m_mailBody = makeMulti("Nội dung (tùy chọn)", w);
    connect(m_mailTo, &QLineEdit::textChanged, this, &QRGenerateTab::scheduleRefresh);
    connect(m_mailSubject, &QLineEdit::textChanged, this, &QRGenerateTab::scheduleRefresh);
    connect(m_mailBody, &QPlainTextEdit::textChanged, this, &QRGenerateTab::scheduleRefresh);
    f->addRow("Đến:", m_mailTo);
    f->addRow("Tiêu đề:", m_mailSubject);
    f->addRow("Nội dung:", m_mailBody);
    return w;
}

QWidget* QRGenerateTab::makeSmsPage()
{
    auto* w = new QWidget(this);
    auto* f = makeForm(w);
    m_smsNumber = makeEdit("+84912345678", w);
    m_smsMessage = makeMulti("Nội dung tin nhắn (tùy chọn)", w);
    connect(m_smsNumber, &QLineEdit::textChanged, this, &QRGenerateTab::scheduleRefresh);
    connect(m_smsMessage, &QPlainTextEdit::textChanged, this, &QRGenerateTab::scheduleRefresh);
    f->addRow("Số điện thoại:", m_smsNumber);
    f->addRow("Tin nhắn:", m_smsMessage);
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
    auto* f = makeForm(w);
    m_geoLat = new QDoubleSpinBox(w);
    m_geoLat->setRange(-90.0, 90.0);
    m_geoLat->setDecimals(6);
    m_geoLat->setValue(21.028511);
    m_geoLon = new QDoubleSpinBox(w);
    m_geoLon->setRange(-180.0, 180.0);
    m_geoLon->setDecimals(6);
    m_geoLon->setValue(105.804817);
    connect(m_geoLat, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &QRGenerateTab::scheduleRefresh);
    connect(m_geoLon, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &QRGenerateTab::scheduleRefresh);
    f->addRow("Vĩ độ (Latitude):", m_geoLat);
    f->addRow("Kinh độ (Longitude):", m_geoLon);
    return w;
}

QWidget* QRGenerateTab::makeVCardPage()
{
    auto* w = new QWidget(this);
    auto* f = makeForm(w);
    m_vcFirst = makeEdit("Tên", w);
    m_vcLast = makeEdit("Họ", w);
    m_vcOrg = makeEdit("Công ty / tổ chức", w);
    m_vcTitle = makeEdit("Chức danh", w);
    m_vcPhone = makeEdit("+84912345678", w);
    m_vcEmail = makeEdit("email@example.com", w);
    m_vcUrl = makeEdit("https://example.com", w);
    m_vcAddress = makeEdit("Địa chỉ", w);
    for (QLineEdit* e : {m_vcFirst, m_vcLast, m_vcOrg, m_vcTitle, m_vcPhone, m_vcEmail, m_vcUrl, m_vcAddress})
        connect(e, &QLineEdit::textChanged, this, &QRGenerateTab::scheduleRefresh);
    f->addRow("Tên:", m_vcFirst);
    f->addRow("Họ:", m_vcLast);
    f->addRow("Công ty:", m_vcOrg);
    f->addRow("Chức danh:", m_vcTitle);
    f->addRow("Điện thoại:", m_vcPhone);
    f->addRow("Email:", m_vcEmail);
    f->addRow("Website:", m_vcUrl);
    f->addRow("Địa chỉ:", m_vcAddress);
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

    m_typeCombo = new QComboBox(contentGroup);
    const QList<QRContentType> types = {QRContentType::Text, QRContentType::Url, QRContentType::Wifi,
                                        QRContentType::Email, QRContentType::Sms, QRContentType::Phone,
                                        QRContentType::Geo, QRContentType::VCard};
    for (QRContentType t : types)
        m_typeCombo->addItem(QRPayload::typeName(t), static_cast<int>(t));
    m_typeCombo->setObjectName("typeCombo");
    contentLayout->addWidget(m_typeCombo);

    m_pages = new QStackedWidget(contentGroup);
    m_pages->addWidget(makeTextPage());
    m_pages->addWidget(makeUrlPage());
    m_pages->addWidget(makeWifiPage());
    m_pages->addWidget(makeEmailPage());
    m_pages->addWidget(makeSmsPage());
    m_pages->addWidget(makePhonePage());
    m_pages->addWidget(makeGeoPage());
    m_pages->addWidget(makeVCardPage());
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
    connect(m_sizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &QRGenerateTab::scheduleRefresh);
    optForm->addRow("Kích thước ảnh:", m_sizeSpin);

    m_quietSpin = new QSpinBox(optGroup);
    m_quietSpin->setRange(0, 16);
    m_quietSpin->setValue(4);
    m_quietSpin->setSuffix(" ô");
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
        case QRContentType::Email:
        {
            const QString to = m_mailTo->text().trimmed();
            if (to.isEmpty() || !to.contains('@'))
                return fail("Nhập địa chỉ email người nhận hợp lệ.");
            return QRPayload::makeEmail({to, m_mailSubject->text(), m_mailBody->toPlainText()});
        }
        case QRContentType::Sms:
        {
            if (m_smsNumber->text().trimmed().isEmpty())
                return fail("Nhập số điện thoại nhận tin nhắn.");
            return QRPayload::makeSms({m_smsNumber->text(), m_smsMessage->toPlainText()});
        }
        case QRContentType::Phone:
        {
            if (m_phoneNumber->text().trimmed().isEmpty())
                return fail("Nhập số điện thoại.");
            return QRPayload::makePhone(m_phoneNumber->text());
        }
        case QRContentType::Geo:
            return QRPayload::makeGeo({m_geoLat->value(), m_geoLon->value()});
        case QRContentType::VCard:
        {
            VCardInfo v;
            v.firstName = m_vcFirst->text().trimmed();
            v.lastName = m_vcLast->text().trimmed();
            v.organization = m_vcOrg->text().trimmed();
            v.title = m_vcTitle->text().trimmed();
            v.phone = m_vcPhone->text().trimmed();
            v.email = m_vcEmail->text().trimmed();
            v.url = m_vcUrl->text().trimmed();
            v.address = m_vcAddress->text().trimmed();
            if (v.firstName.isEmpty() && v.lastName.isEmpty() && v.phone.isEmpty() && v.email.isEmpty())
                return fail("Nhập ít nhất họ tên, số điện thoại hoặc email.");
            return QRPayload::makeVCard(v);
        }
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
    m_textEdit->setPlainText(text);
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
    // Chỉ trang đang hiển thị quyết định chiều cao, tránh khoảng trống do trang cao nhất (vCard)
    for (int i = 0; i < m_pages->count(); ++i)
        m_pages->widget(i)->setSizePolicy(QSizePolicy::Preferred, i == index ? QSizePolicy::Preferred : QSizePolicy::Ignored);
    m_pages->updateGeometry();
    m_pages->adjustSize();
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
