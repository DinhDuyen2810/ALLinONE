#include "WirelessPairDialog.h"

#include "AndroidUiStyle.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

WirelessPairDialog::WirelessPairDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Ghép đôi gỡ lỗi không dây");
    setMinimumWidth(420);
    setStyleSheet(
        "QDialog { background-color: #f6f8fa; }"
        "QLabel { color: #1f2328; background: transparent; }" + AndroidUi::inputStyle());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 16);
    root->setSpacing(12);

    auto* hint = new QLabel(
        "Trên điện thoại: Cài đặt → Tùy chọn nhà phát triển → Gỡ lỗi qua mạng → Ghép đôi bằng mã ghép "
        "đôi. Nhập đúng địa chỉ IP:Cổng và mã 6 số hiển thị (chỉ cần làm 1 lần cho mỗi mạng Wi-Fi).",
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(AndroidUi::bannerStyle("info"));
    root->addWidget(hint);

    auto* form = new QFormLayout();
    form->setSpacing(10);

    m_ipPortEdit = new QLineEdit(this);
    m_ipPortEdit->setPlaceholderText("192.168.1.23:41234");
    form->addRow("IP:Cổng ghép đôi:", m_ipPortEdit);

    m_codeEdit = new QLineEdit(this);
    m_codeEdit->setPlaceholderText("123456");
    m_codeEdit->setMaxLength(6);
    form->addRow("Mã ghép đôi:", m_codeEdit);

    root->addLayout(form);

    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton("Hủy", this);
    cancelBtn->setStyleSheet(AndroidUi::buttonStyle());
    auto* pairBtn = new QPushButton("Ghép đôi", this);
    pairBtn->setStyleSheet(AndroidUi::primaryButtonStyle());
    pairBtn->setDefault(true);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(pairBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(cancelBtn);
    btnRow->addWidget(pairBtn);
    root->addLayout(btnRow);
}

QString WirelessPairDialog::ipAndPort() const
{
    return m_ipPortEdit->text().trimmed();
}

QString WirelessPairDialog::pairingCode() const
{
    return m_codeEdit->text().trimmed();
}
