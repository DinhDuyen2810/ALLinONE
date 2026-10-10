#include "CommandGatewayTab.h"

#include "SecurityUiStyle.h"
#include "engine/CommandLauncher.h"
#include "core/WinElevation.h"

#include <QCheckBox>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

CommandGatewayTab::CommandGatewayTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    m_launcher = new CommandLauncher(this);
    connect(m_launcher, &CommandLauncher::finished, this, &CommandGatewayTab::onLaunchFinished);
}

void CommandGatewayTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* introLabel = new QLabel(
        "Dán một lệnh/đoạn script PowerShell khả nghi vào ô dưới để kiểm tra TRƯỚC KHI chạy thật. Ứng "
        "dụng chỉ PHÂN TÍCH CHỮ (không chạy thử) rồi cho biết có dấu hiệu bất thường hay không - việc "
        "chạy thật luôn diễn ra trên một cửa sổ PowerShell THẬT, riêng biệt, và bạn luôn là người tự "
        "bấm Enter cuối cùng. Ứng dụng không bao giờ tự chạy lệnh thay bạn. Lưu ý: với lệnh NHIỀU DÒNG, "
        "console có thể tự chạy các dòng trước dòng cuối ngay khi dán - ứng dụng sẽ hỏi lại trước khi dán.",
        this);
    introLabel->setWordWrap(true);
    introLabel->setStyleSheet(SecurityUi::bannerStyle("info"));
    root->addWidget(introLabel);

    m_commandEdit = new QPlainTextEdit(this);
    m_commandEdit->setPlaceholderText("Dán lệnh PowerShell vào đây...");
    m_commandEdit->setStyleSheet(
        "QPlainTextEdit { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; "
        "border-radius: 8px; padding: 8px; font-family: Consolas, monospace; font-size: 12px; "
        "placeholder-text-color: #8c959f; }");
    m_commandEdit->setMinimumHeight(140);
    connect(m_commandEdit, &QPlainTextEdit::textChanged, this, &CommandGatewayTab::onTextChanged);
    root->addWidget(m_commandEdit, 1);

    m_analyzeBtn = new QPushButton("🔍 Kiểm tra lệnh này", this);
    m_analyzeBtn->setStyleSheet(SecurityUi::primaryButtonStyle());
    m_analyzeBtn->setCursor(Qt::PointingHandCursor);
    connect(m_analyzeBtn, &QPushButton::clicked, this, &CommandGatewayTab::onAnalyzeClicked);
    root->addWidget(m_analyzeBtn);

    m_verdictBanner = new QLabel(this);
    m_verdictBanner->setWordWrap(true);
    m_verdictBanner->setVisible(false);
    root->addWidget(m_verdictBanner);

    m_reasonsLabel = new QLabel(this);
    m_reasonsLabel->setWordWrap(true);
    m_reasonsLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    m_reasonsLabel->setVisible(false);
    root->addWidget(m_reasonsLabel);

    m_acknowledgeCheck = new QCheckBox(
        "Tôi đã xem cảnh báo ở trên, vẫn muốn mở PowerShell và dán lệnh này vào", this);
    m_acknowledgeCheck->setStyleSheet("color: #cf222e; font-size: 12px;");
    m_acknowledgeCheck->setVisible(false);
    connect(m_acknowledgeCheck, &QCheckBox::toggled, this, &CommandGatewayTab::onAcknowledgeToggled);
    root->addWidget(m_acknowledgeCheck);

    m_launchBtn = new QPushButton("📋 Mở PowerShell & dán lệnh", this);
    m_launchBtn->setStyleSheet(SecurityUi::buttonStyle());
    m_launchBtn->setCursor(Qt::PointingHandCursor);
    m_launchBtn->setEnabled(false);
    connect(m_launchBtn, &QPushButton::clicked, this, &CommandGatewayTab::onLaunchClicked);
    root->addWidget(m_launchBtn);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    root->addStretch();
}

void CommandGatewayTab::onTextChanged()
{
    // Đổi nội dung sau khi đã kiểm tra - bắt kiểm tra lại, không cho dùng kết quả kiểm tra cũ với lệnh mới.
    m_analyzed = false;
    m_verdictBanner->setVisible(false);
    m_reasonsLabel->setVisible(false);
    m_acknowledgeCheck->setVisible(false);
    m_acknowledgeCheck->setChecked(false);
    updateLaunchButtonState();
}

void CommandGatewayTab::onAnalyzeClicked()
{
    const QString command = m_commandEdit->toPlainText();
    m_lastVerdict = CommandAnalyzer::analyze(command);
    m_analyzed = true;

    const QString kindStyle = m_lastVerdict.level == CommandAnalyzer::RiskLevel::Dangerous    ? "danger"
                              : m_lastVerdict.level == CommandAnalyzer::RiskLevel::Suspicious ? "warn"
                                                                                                : "info";
    const QString icon = m_lastVerdict.level == CommandAnalyzer::RiskLevel::Dangerous    ? "⛔"
                         : m_lastVerdict.level == CommandAnalyzer::RiskLevel::Suspicious ? "⚠"
                                                                                           : "✓";
    m_verdictBanner->setText(QString("%1 %2").arg(icon, CommandAnalyzer::riskLevelLabel(m_lastVerdict.level)));
    m_verdictBanner->setStyleSheet(SecurityUi::bannerStyle(kindStyle));
    m_verdictBanner->setVisible(true);

    if (m_lastVerdict.reasons.isEmpty())
    {
        m_reasonsLabel->setText("Không phát hiện dấu hiệu bất thường nào trong phân tích tĩnh. Đây KHÔNG phải "
                                 "bảo đảm tuyệt đối - hãy vẫn xem lại lệnh trước khi chạy.");
    }
    else
    {
        QString text = "Lý do:\n";
        for (const QString& r : m_lastVerdict.reasons)
            text += "• " + r + "\n";
        m_reasonsLabel->setText(text.trimmed());
    }
    m_reasonsLabel->setVisible(true);

    m_acknowledgeCheck->setVisible(m_lastVerdict.level == CommandAnalyzer::RiskLevel::Dangerous);
    m_acknowledgeCheck->setChecked(false);

    updateLaunchButtonState();
}

void CommandGatewayTab::onAcknowledgeToggled(bool)
{
    updateLaunchButtonState();
}

void CommandGatewayTab::updateLaunchButtonState()
{
    if (!m_analyzed || m_commandEdit->toPlainText().trimmed().isEmpty())
    {
        m_launchBtn->setEnabled(false);
        return;
    }
    if (m_lastVerdict.level == CommandAnalyzer::RiskLevel::Dangerous)
        m_launchBtn->setEnabled(m_acknowledgeCheck->isChecked());
    else
        m_launchBtn->setEnabled(true);
}

void CommandGatewayTab::onLaunchClicked()
{
    // Hai tình huống mà "dán hộ" không còn vô hại như mô tả ở đầu tab - nói rõ và để người dùng quyết
    // định TRƯỚC khi mở cửa sổ (xem CommandLauncherInternal::pasteWarnings).
    const QStringList warnings = CommandLauncherInternal::pasteWarnings(
        CommandLauncherInternal::prepareForPaste(m_commandEdit->toPlainText()), WinElevation::isElevated());
    if (!warnings.isEmpty())
    {
        QString text;
        for (const QString& w : warnings)
            text += "• " + w + "\n\n";
        const auto answer = QMessageBox::warning(
            this, "Mở PowerShell & dán lệnh",
            text + "Vẫn mở PowerShell và dán lệnh này? (Chọn \"No\" nếu muốn tự sao chép và dán từng phần.)",
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    m_launchBtn->setEnabled(false);
    m_statusLabel->setText("⏳ Đang mở PowerShell...");
    m_launcher->launchAndPaste(m_commandEdit->toPlainText());
}

void CommandGatewayTab::onLaunchFinished(bool ok, QString error)
{
    updateLaunchButtonState();
    if (ok)
        m_statusLabel->setText("✓ Đã mở PowerShell và dán lệnh vào - tự xem lại rồi bấm Enter để chạy.");
    else
        m_statusLabel->setText("⚠ " + error);
}
