#include "SettingsTab.h"

#include "WifiSettings.h"
#include "WifiUiStyle.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QVBoxLayout>

SettingsTab::SettingsTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();

    m_passwordEdit->setText(WifiSettings::defaultConnectPassword());
    m_fromEdit->setText(WifiSettings::labPasswordStart());
    m_toEdit->setText(WifiSettings::labPasswordEnd());
}

void SettingsTab::buildUi()
{
    setStyleSheet(
        "QLabel { color: #1f2328; background: transparent; }"
        + WifiUi::inputStyle());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    auto* title = new QLabel("Cài đặt mật khẩu kết nối lab", this);
    title->setStyleSheet("font-weight: 700; font-size: 14px;");
    root->addWidget(title);

    auto* desc = new QLabel(
        "Cấu hình mật khẩu mặc định và khoảng mã hệ 36. "
        "Khoảng mã sử dụng các ký tự 0-9 và A-Z. "
        "From và To phải có cùng độ dài. "
        "Các giá trị này chỉ được lưu làm cấu hình; "
        "việc chọn giá trị để kết nối được xử lý riêng.",
        this);
    desc->setWordWrap(true);
    desc->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(desc);

    auto* form = new QFormLayout();
    form->setSpacing(10);

    // Mật khẩu mặc định
    auto* pwRow = new QWidget(this);
    auto* pwLayout = new QHBoxLayout(pwRow);
    pwLayout->setContentsMargins(0, 0, 0, 0);
    pwLayout->setSpacing(6);

    m_passwordEdit = new QLineEdit(pwRow);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    m_passwordEdit->setPlaceholderText("Mật khẩu mặc định");

    m_toggleVisBtn = new QPushButton("👁", pwRow);
    m_toggleVisBtn->setFixedWidth(36);
    m_toggleVisBtn->setCursor(Qt::PointingHandCursor);
    m_toggleVisBtn->setStyleSheet(WifiUi::buttonStyle());

    connect(
        m_toggleVisBtn,
        &QPushButton::clicked,
        this,
        &SettingsTab::onTogglePasswordVisibility);

    pwLayout->addWidget(m_passwordEdit, 1);
    pwLayout->addWidget(m_toggleVisBtn);

    form->addRow("Mật khẩu mặc định:", pwRow);

    // Validator cho mã hệ 36: 0-9, A-Z
    auto* base36Validator = new QRegularExpressionValidator(
        QRegularExpression("[0-9A-Za-z]*"),
        this);

    // From
    m_fromEdit = new QLineEdit(this);
    m_fromEdit->setPlaceholderText("Ví dụ: A000");
    m_fromEdit->setMaxLength(64);
    m_fromEdit->setValidator(base36Validator);
    form->addRow("From (bắt đầu):", m_fromEdit);

    // To
    m_toEdit = new QLineEdit(this);
    m_toEdit->setPlaceholderText("Ví dụ: A00Z");
    m_toEdit->setMaxLength(64);
    m_toEdit->setValidator(
        new QRegularExpressionValidator(
            QRegularExpression("[0-9A-Za-z]*"),
            this));
    form->addRow("To (kết thúc):", m_toEdit);

    root->addLayout(form);

    auto* btnRow = new QHBoxLayout();

    m_saveBtn = new QPushButton("💾 Lưu", this);
    m_saveBtn->setStyleSheet(WifiUi::primaryButtonStyle());
    m_saveBtn->setCursor(Qt::PointingHandCursor);

    connect(
        m_saveBtn,
        &QPushButton::clicked,
        this,
        &SettingsTab::onSaveClicked);

    btnRow->addWidget(m_saveBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(
        "color: #1a7f37; font-size: 12px;");
    root->addWidget(m_statusLabel);

    root->addStretch();
}

void SettingsTab::onSaveClicked()
{
    const QString password = m_passwordEdit->text();
    const QString from = m_fromEdit->text().trimmed().toUpper();
    const QString to = m_toEdit->text().trimmed().toUpper();

    // Cho phép để trống cả hai đầu nếu chưa cấu hình khoảng.
    if (from.isEmpty() != to.isEmpty()) {
        QMessageBox::warning(
            this,
            "Cấu hình không hợp lệ",
            "Hãy nhập cả From và To, hoặc để trống cả hai.");
        return;
    }

    if (!from.isEmpty()) {
        if (!WifiSettings::isValidBase36(from)
            || !WifiSettings::isValidBase36(to)) {
            QMessageBox::warning(
                this,
                "Cấu hình không hợp lệ",
                "From và To chỉ được chứa ký tự 0-9 và A-Z.");
            return;
        }

        if (from.length() != to.length()) {
            QMessageBox::warning(
                this,
                "Cấu hình không hợp lệ",
                "From và To phải có cùng độ dài.");
            return;
        }

        // So sánh từ điển chỉ phù hợp với chuỗi cùng độ dài
        // khi thứ tự ký tự đúng theo bảng 0-9, A-Z.
        if (from > to) {
            QMessageBox::warning(
                this,
                "Cấu hình không hợp lệ",
                "From phải nhỏ hơn hoặc bằng To.");
            return;
        }
    }

    WifiSettings::setDefaultConnectPassword(password);
    WifiSettings::setLabPasswordStart(from);
    WifiSettings::setLabPasswordEnd(to);

    m_fromEdit->setText(from);
    m_toEdit->setText(to);

    m_statusLabel->setStyleSheet(
        "color: #1a7f37; font-size: 12px;");
    m_statusLabel->setText("✓ Đã lưu cấu hình.");
}

void SettingsTab::onTogglePasswordVisibility()
{
    const bool hidden =
        m_passwordEdit->echoMode() == QLineEdit::Password;

    m_passwordEdit->setEchoMode(
        hidden ? QLineEdit::Normal : QLineEdit::Password);

    m_toggleVisBtn->setText(hidden ? "🙈" : "👁");
}
