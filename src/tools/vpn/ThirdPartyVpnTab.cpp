#include "ThirdPartyVpnTab.h"

#include "VpnUiStyle.h"
#include "engine/ThirdPartyVpnScanner.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

ThirdPartyVpnTab::ThirdPartyVpnTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    reload();
}

void ThirdPartyVpnTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* hint = new QLabel(
        "Phát hiện các kết nối VPN đã được cấu hình sẵn trên máy bởi phần mềm bên thứ ba (FortiClient, "
        "GlobalProtect, OpenVPN, Cisco Secure Client) và cho bạn MỞ chính phần mềm đó để kết nối. Ứng dụng "
        "chỉ đọc TÊN và ĐỊA CHỈ MÁY CHỦ đã lưu - KHÔNG đọc/giải mã mật khẩu của bạn, KHÔNG tự kết nối thẳng "
        "(các VPN doanh nghiệp này dùng giao thức riêng). Việc đăng nhập/kết nối do chính phần mềm đó làm.",
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(VpnUi::bannerStyle("info"));
    root->addWidget(hint);

    auto* topRow = new QHBoxLayout();
    m_refreshBtn = new QPushButton("🔄 Quét lại", this);
    m_refreshBtn->setStyleSheet(VpnUi::buttonStyle());
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &ThirdPartyVpnTab::onRefreshClicked);
    topRow->addWidget(m_refreshBtn);
    topRow->addStretch();
    m_openBtn = new QPushButton("▶ Mở phần mềm để kết nối", this);
    m_openBtn->setStyleSheet(VpnUi::primaryButtonStyle());
    m_openBtn->setCursor(Qt::PointingHandCursor);
    m_openBtn->setEnabled(false);
    connect(m_openBtn, &QPushButton::clicked, this, &ThirdPartyVpnTab::onOpenClicked);
    topRow->addWidget(m_openBtn);
    root->addLayout(topRow);

    m_table = new QTableWidget(0, 3, this);
    m_table->setHorizontalHeaderLabels({"Phần mềm", "Tên hồ sơ", "Máy chủ / Portal"});
    m_table->setStyleSheet(VpnUi::tableStyle());
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &ThirdPartyVpnTab::onRowSelectionChanged);
    root->addWidget(m_table, 1);

    m_statusLabel = new QLabel("", this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_statusLabel);
}

void ThirdPartyVpnTab::reload()
{
    m_profiles = ThirdPartyVpnScanner::scan();
    m_table->setRowCount(m_profiles.size());
    for (int i = 0; i < m_profiles.size(); ++i)
    {
        const ThirdPartyVpnProfile& p = m_profiles[i];
        auto set = [&](int col, const QString& text, const QString& tip = QString()) {
            auto* item = new QTableWidgetItem(text);
            if (!tip.isEmpty())
                item->setToolTip(tip);
            m_table->setItem(i, col, item);
        };
        set(0, p.clientName, p.canLaunch() ? p.exePath : "Không tìm thấy chương trình để mở");
        set(1, p.name);
        set(2, p.server.isEmpty() ? "(không rõ)" : p.server, p.source);
    }
    if (m_profiles.isEmpty())
        m_statusLabel->setText("Không tìm thấy hồ sơ VPN nào từ FortiClient/GlobalProtect/OpenVPN/Cisco trên máy này. "
                               "Nếu bạn đã cài một trong số đó và đã cấu hình kết nối, hãy bấm \"Quét lại\".");
    else
        m_statusLabel->setText(QString("Tìm thấy %1 hồ sơ. Chọn một dòng rồi bấm \"Mở phần mềm để kết nối\" - việc đăng "
                                       "nhập do chính phần mềm đó thực hiện.")
                                   .arg(m_profiles.size()));
    onRowSelectionChanged();
}

const ThirdPartyVpnProfile* ThirdPartyVpnTab::selected() const
{
    const auto rows = m_table->selectionModel() ? m_table->selectionModel()->selectedRows() : QModelIndexList();
    if (rows.size() != 1)
        return nullptr;
    const int row = rows.first().row();
    if (row < 0 || row >= m_profiles.size())
        return nullptr;
    return &m_profiles[row];
}

void ThirdPartyVpnTab::onRowSelectionChanged()
{
    const ThirdPartyVpnProfile* p = selected();
    m_openBtn->setEnabled(p != nullptr && p->canLaunch());
}

void ThirdPartyVpnTab::onRefreshClicked()
{
    reload();
}

void ThirdPartyVpnTab::onOpenClicked()
{
    const ThirdPartyVpnProfile* p = selected();
    if (!p)
        return;
    QString error;
    if (ThirdPartyVpnScanner::launch(*p, &error))
        m_statusLabel->setText("✓ Đã mở " + p->clientName + ". Hãy đăng nhập/kết nối trong cửa sổ của phần mềm đó.");
    else
        QMessageBox::warning(this, "Không mở được", error);
}
