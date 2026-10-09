#include "WebProtectionTab.h"

#include "SecurityUiStyle.h"
#include "engine/HostsBlocklist.h"
#include "core/ToolManager.h"
#include "core/WinElevation.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

WebProtectionTab::WebProtectionTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    updateElevationBanner();
    refreshStatus();
    refreshBlocklist();
}

void WebProtectionTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    m_elevationBanner = new QLabel(this);
    m_elevationBanner->setWordWrap(true);
    m_elevationBanner->setVisible(false);
    root->addWidget(m_elevationBanner);

    m_relaunchBtn = new QPushButton("🛡 Chạy lại với quyền Quản trị...", this);
    m_relaunchBtn->setStyleSheet(SecurityUi::primaryButtonStyle());
    m_relaunchBtn->setCursor(Qt::PointingHandCursor);
    m_relaunchBtn->setVisible(false);
    connect(m_relaunchBtn, &QPushButton::clicked, this, &WebProtectionTab::onRelaunchElevatedClicked);
    root->addWidget(m_relaunchBtn);

    auto* introLabel = new QLabel(
        "Network Protection chặn kết nối tới trang/máy chủ được Microsoft đánh giá có hại (lừa đảo, phát "
        "tán mã độc, máy chủ điều khiển tấn công...) ngay ở tầng hệ điều hành - trước khi trình duyệt "
        "hay bất kỳ ứng dụng nào kịp tải nội dung từ đó. Áp dụng cho MỌI trình duyệt/ứng dụng trên máy.",
        this);
    introLabel->setWordWrap(true);
    introLabel->setStyleSheet(SecurityUi::bannerStyle("info"));
    root->addWidget(introLabel);

    auto* statusRow = new QHBoxLayout();
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #1f2328; font-size: 12px;");
    statusRow->addWidget(m_statusLabel, 1);
    m_refreshBtn = new QPushButton("🔄 Làm mới", this);
    m_refreshBtn->setStyleSheet(SecurityUi::buttonStyle());
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &WebProtectionTab::onRefreshClicked);
    statusRow->addWidget(m_refreshBtn);
    root->addLayout(statusRow);

    m_tamperWarning = new QLabel(this);
    m_tamperWarning->setWordWrap(true);
    m_tamperWarning->setStyleSheet(SecurityUi::bannerStyle("warn"));
    m_tamperWarning->setVisible(false);
    root->addWidget(m_tamperWarning);

    m_toggleBtn = new QPushButton(this);
    m_toggleBtn->setStyleSheet(SecurityUi::primaryButtonStyle());
    m_toggleBtn->setCursor(Qt::PointingHandCursor);
    connect(m_toggleBtn, &QPushButton::clicked, this, &WebProtectionTab::onToggleNetworkProtectionClicked);
    root->addWidget(m_toggleBtn);

    auto* blockLabel = new QLabel(
        "Danh sách chặn tùy chỉnh (bổ sung thủ công - chặn cục bộ qua hosts file, hoạt động cả khi "
        "không có Internet, không phụ thuộc Defender):",
        this);
    blockLabel->setWordWrap(true);
    blockLabel->setStyleSheet("color: #57606a; font-size: 12px; margin-top: 6px;");
    root->addWidget(blockLabel);

    m_blocklistErrorLabel = new QLabel(this);
    m_blocklistErrorLabel->setWordWrap(true);
    m_blocklistErrorLabel->setStyleSheet(SecurityUi::bannerStyle("danger"));
    m_blocklistErrorLabel->setVisible(false);
    root->addWidget(m_blocklistErrorLabel);

    m_domainList = new QListWidget(this);
    m_domainList->setStyleSheet(
        "QListWidget { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; }");
    root->addWidget(m_domainList, 1);

    auto* addRow = new QHBoxLayout();
    m_domainEdit = new QLineEdit(this);
    m_domainEdit->setStyleSheet(SecurityUi::inputStyle());
    m_domainEdit->setPlaceholderText("vd: vi-du-doc-hai.com");
    connect(m_domainEdit, &QLineEdit::returnPressed, this, &WebProtectionTab::onAddDomainClicked);
    addRow->addWidget(m_domainEdit, 1);
    m_addDomainBtn = new QPushButton("+ Thêm", this);
    m_addDomainBtn->setStyleSheet(SecurityUi::buttonStyle());
    m_addDomainBtn->setCursor(Qt::PointingHandCursor);
    connect(m_addDomainBtn, &QPushButton::clicked, this, &WebProtectionTab::onAddDomainClicked);
    addRow->addWidget(m_addDomainBtn);
    m_removeDomainBtn = new QPushButton("🗑 Xóa đã chọn", this);
    m_removeDomainBtn->setStyleSheet(SecurityUi::dangerButtonStyle());
    m_removeDomainBtn->setCursor(Qt::PointingHandCursor);
    connect(m_removeDomainBtn, &QPushButton::clicked, this, &WebProtectionTab::onRemoveDomainClicked);
    addRow->addWidget(m_removeDomainBtn);
    root->addLayout(addRow);
}

void WebProtectionTab::updateElevationBanner()
{
    const bool elevated = WinElevation::isElevated();
    m_elevationBanner->setVisible(!elevated);
    m_relaunchBtn->setVisible(!elevated);
    if (!elevated)
    {
        m_elevationBanner->setText(
            "🔒 Đang chạy KHÔNG có quyền Administrator - có thể xem trạng thái, nhưng cần quyền "
            "Administrator để bật/tắt Network Protection hoặc sửa danh sách chặn tùy chỉnh.");
        m_elevationBanner->setStyleSheet(SecurityUi::bannerStyle("warn"));
    }
}

void WebProtectionTab::onRelaunchElevatedClicked()
{
    // qApp->quit() ở cuối hàm này KHÔNG tự gọi closeEvent() của các cửa sổ tool KHÁC đang mở (xem
    // MainWindow.cpp) - nếu một cửa sổ khác (vd Disk Cleanup) đang đổi kích thước phân vùng thật, buộc
    // thoát ngay bây giờ sẽ TerminateProcess giữa chừng một thao tác không an toàn để hủy - CHẶN trước.
    QString busyReason;
    if (ToolManager::instance().anyToolWindowBusy(&busyReason))
    {
        QMessageBox::warning(this, "Không thể chạy lại lúc này",
            "Đang có một thao tác không an toàn để hủy giữa chừng (" + busyReason + ") - chạy lại ứng "
            "dụng lúc này có thể làm hỏng dữ liệu. Vui lòng đợi thao tác đó hoàn tất rồi thử lại.");
        return;
    }

    QString error;
    if (!WinElevation::relaunchElevated(&error))
    {
        QMessageBox::warning(this, "Chạy lại với quyền Quản trị", error.isEmpty() ? "Không thực hiện được." : error);
        return;
    }
    qApp->quit();
}

void WebProtectionTab::setRelaunchAllowed(bool allowed)
{
    m_relaunchBtn->setEnabled(allowed);
}

void WebProtectionTab::onRefreshClicked()
{
    updateElevationBanner();
    refreshStatus();
    refreshBlocklist();
}

void WebProtectionTab::refreshStatus()
{
    QString error;
    m_lastStatus = DefenderController::getStatus(&error);

    if (!error.isEmpty())
    {
        m_statusLabel->setText("⚠ Không đọc được trạng thái Windows Defender: " + error);
        m_toggleBtn->setVisible(false);
        m_tamperWarning->setVisible(false);
        return;
    }

    QString modeNote;
    if (!m_lastStatus.isActivelyProtecting())
    {
        modeNote = QString(" ⚠ Defender đang ở chế độ \"%1\" (không phải AV chính trên máy - có thể do "
                            "phần mềm diệt virus khác đang hoạt động) - các công tắc dưới đây có thể không "
                            "còn nhiều ý nghĩa thực tế.")
                       .arg(m_lastStatus.runningMode.isEmpty() ? "Không rõ" : m_lastStatus.runningMode);
    }
    m_statusLabel->setText(
        QString("Windows Defender: %1 | Bảo vệ thời gian thực: %2 | Network Protection: %3%4")
            .arg(m_lastStatus.antivirusEnabled ? "Đang bật" : "Đang tắt",
                 m_lastStatus.realTimeProtectionEnabled ? "Đang bật" : "Đang tắt",
                 m_lastStatus.networkProtectionMode.isEmpty() ? "Không rõ" : m_lastStatus.networkProtectionMode,
                 modeNote));

    m_tamperWarning->setVisible(m_lastStatus.isTamperProtected);
    if (m_lastStatus.isTamperProtected)
        m_tamperWarning->setText(
            "🛡 Tamper Protection đang bật: Windows có thể ÂM THẦM không áp dụng thay đổi bạn bấm dưới "
            "đây (không báo lỗi) để chống mã độc tự tắt bảo vệ. Sau khi bấm, hãy bấm \"Làm mới\" để xem "
            "trạng thái THẬT - nếu không đổi, đó là do Tamper Protection, không phải lỗi ứng dụng.");

    const bool enabled = m_lastStatus.networkProtectionEnabled();
    m_toggleBtn->setText(enabled ? "🔓 Tắt Network Protection" : "🔒 Bật Network Protection");
    m_toggleBtn->setStyleSheet(enabled ? SecurityUi::dangerButtonStyle() : SecurityUi::primaryButtonStyle());
    m_toggleBtn->setVisible(true);
}

void WebProtectionTab::onToggleNetworkProtectionClicked()
{
    const bool wantEnable = !m_lastStatus.networkProtectionEnabled();
    m_toggleBtn->setEnabled(false);
    QString error;
    bool verified = false;
    DefenderController::setNetworkProtectionEnabled(wantEnable, &verified, &error);
    m_toggleBtn->setEnabled(true);

    refreshStatus();

    if (verified == wantEnable)
    {
        m_statusLabel->setText(m_statusLabel->text() + (wantEnable ? "  ✓ Đã bật." : "  ✓ Đã tắt."));
    }
    else
    {
        QMessageBox::warning(this, "Network Protection",
            "Thay đổi KHÔNG áp dụng được - trạng thái thật hiện tại vẫn là \"" +
                (m_lastStatus.networkProtectionEnabled() ? QString("Đang bật") : QString("Đang tắt")) + "\"." +
                (error.isEmpty() ? "" : ("\n\nChi tiết: " + error)) +
                (m_lastStatus.isTamperProtected ? "\n\nNhiều khả năng do Tamper Protection đang chặn - xem "
                                                  "cảnh báo phía trên."
                                                : ""));
    }
}

void WebProtectionTab::refreshBlocklist()
{
    QString error;
    const auto domains = HostsBlocklist::listBlockedDomains(&error);
    // Lỗi đọc phải HIỆN RA - danh sách trống mà không kèm lời nào trông y hệt "chưa chặn tên miền nào".
    m_blocklistErrorLabel->setVisible(!error.isEmpty());
    if (!error.isEmpty())
        m_blocklistErrorLabel->setText("⚠ " + error + " Danh sách bên dưới có thể không đúng với thực tế.");
    m_domainList->clear();
    for (const QString& d : domains)
        m_domainList->addItem(d);
}

void WebProtectionTab::onAddDomainClicked()
{
    const QString domain = m_domainEdit->text().trimmed();
    if (domain.isEmpty())
        return;
    QString error;
    if (!HostsBlocklist::addDomain(domain, &error))
    {
        QMessageBox::warning(this, "Thêm tên miền chặn", error.isEmpty() ? "Không thực hiện được." : error);
        return;
    }
    m_domainEdit->clear();
    refreshBlocklist();
}

void WebProtectionTab::onRemoveDomainClicked()
{
    auto* item = m_domainList->currentItem();
    if (!item)
        return;
    QString error;
    if (!HostsBlocklist::removeDomain(item->text(), &error))
    {
        QMessageBox::warning(this, "Xóa tên miền chặn", error.isEmpty() ? "Không thực hiện được." : error);
        return;
    }
    refreshBlocklist();
}
