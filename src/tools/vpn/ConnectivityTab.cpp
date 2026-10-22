#include "ConnectivityTab.h"

#include "VpnUiStyle.h"
#include "engine/ConnectivityTester.h"

#include <QColor>
#include <QDateTime>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

ConnectivityTab::ConnectivityTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    m_tester = new ConnectivityTester(this);
    connect(m_tester, &ConnectivityTester::finished, this, &ConnectivityTab::onTestFinished);
}

void ConnectivityTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* hint = new QLabel(
        "Kiểm tra có \"chạm\" được tới một máy chủ không bằng cách mở thử một kết nối TCP, kèm độ trễ - để lọc "
        "nhanh máy chủ đã chết trước khi kết nối. Cổng 443 chạm được nghĩa là máy chủ còn sống và SSTP có thể dùng; "
        "L2TP chạy trên UDP nên phép thử này không khẳng định chắc L2TP kết nối được.",
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(VpnUi::bannerStyle("info"));
    root->addWidget(hint);

    auto* row = new QHBoxLayout();
    row->addWidget(new QLabel("Máy chủ (IP hoặc tên miền):", this));
    m_hostEdit = new QLineEdit(this);
    m_hostEdit->setStyleSheet(VpnUi::inputStyle());
    m_hostEdit->setPlaceholderText("vd: 219.100.37.1 hoặc vpn.example.com");
    connect(m_hostEdit, &QLineEdit::returnPressed, this, &ConnectivityTab::onCheckClicked);
    row->addWidget(m_hostEdit, 1);

    row->addWidget(new QLabel("Cổng:", this));
    m_portSpin = new QSpinBox(this);
    m_portSpin->setStyleSheet(VpnUi::inputStyle());
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(443);
    // Bỏ mũi tên tăng/giảm: stylesheet bo góc + đệm của ô nhập làm vỡ cụm mũi tên (chỉ còn một vạch dọc - thấy
    // khi rà giao diện bằng ảnh chụp). Cổng là số gõ tay, không cần bấm tăng/giảm.
    m_portSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_portSpin->setAlignment(Qt::AlignCenter);
    m_portSpin->setFixedWidth(80);
    row->addWidget(m_portSpin);

    m_checkBtn = new QPushButton("🔎 Kiểm tra", this);
    m_checkBtn->setStyleSheet(VpnUi::primaryButtonStyle());
    m_checkBtn->setCursor(Qt::PointingHandCursor);
    connect(m_checkBtn, &QPushButton::clicked, this, &ConnectivityTab::onCheckClicked);
    row->addWidget(m_checkBtn);
    root->addLayout(row);

    m_resultLabel = new QLabel("Nhập địa chỉ máy chủ rồi bấm \"Kiểm tra\".", this);
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setStyleSheet("color: #57606a; font-size: 13px;");
    root->addWidget(m_resultLabel);

    m_log = new QTableWidget(0, 5, this);
    m_log->setHorizontalHeaderLabels({"Thời điểm", "Máy chủ", "Cổng", "Kết quả", "Độ trễ"});
    m_log->setStyleSheet(VpnUi::tableStyle());
    m_log->setSelectionMode(QAbstractItemView::NoSelection);
    m_log->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_log->verticalHeader()->setVisible(false);
    m_log->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_log->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    root->addWidget(m_log, 1);
}

void ConnectivityTab::setTarget(const QString& host, quint16 port)
{
    m_hostEdit->setText(host);
    m_portSpin->setValue(port);
}

void ConnectivityTab::setBusy(bool busy)
{
    m_checkBtn->setEnabled(!busy);
    m_hostEdit->setEnabled(!busy);
    m_portSpin->setEnabled(!busy);
    m_checkBtn->setText(busy ? "⏳ Đang kiểm tra..." : "🔎 Kiểm tra");
}

void ConnectivityTab::onCheckClicked()
{
    if (m_tester->isRunning())
        return;
    const QString host = m_hostEdit->text().trimmed();
    if (host.isEmpty())
    {
        m_resultLabel->setStyleSheet("color: #cf222e; font-size: 13px;");
        m_resultLabel->setText("⚠ Vui lòng nhập địa chỉ máy chủ.");
        return;
    }
    m_pendingHost = host;
    m_pendingPort = m_portSpin->value();
    setBusy(true);
    m_resultLabel->setStyleSheet("color: #57606a; font-size: 13px;");
    m_resultLabel->setText(QString("⏳ Đang thử kết nối tới %1:%2 ...").arg(host).arg(m_pendingPort));
    m_tester->test(host, static_cast<quint16>(m_pendingPort), 5000);
}

void ConnectivityTab::onTestFinished(bool reachable, int elapsedMs, QString detail)
{
    setBusy(false);
    if (reachable)
    {
        m_resultLabel->setStyleSheet("color: #1a7f37; font-size: 13px; font-weight: bold;");
        m_resultLabel->setText(QString("✓ Chạm được %1:%2 trong %3 ms. %4")
                                   .arg(m_pendingHost).arg(m_pendingPort).arg(elapsedMs).arg(detail));
    }
    else
    {
        m_resultLabel->setStyleSheet("color: #cf222e; font-size: 13px;");
        m_resultLabel->setText(QString("✖ %1:%2 - %3").arg(m_pendingHost).arg(m_pendingPort).arg(detail));
    }
    addLogRow(m_pendingHost, m_pendingPort, reachable, elapsedMs, detail);
}

void ConnectivityTab::addLogRow(const QString& host, int port, bool reachable, int ms, const QString& detail)
{
    m_log->insertRow(0); // mới nhất lên đầu
    auto set = [&](int col, const QString& text, const QString& color = QString()) {
        auto* item = new QTableWidgetItem(text);
        if (!color.isEmpty())
            item->setForeground(QColor(color));
        m_log->setItem(0, col, item);
    };
    set(0, QDateTime::currentDateTime().toString("HH:mm:ss"));
    set(1, host);
    set(2, QString::number(port));
    set(3, reachable ? "✓ Chạm được" : "✖ Không được", reachable ? "#1a7f37" : "#cf222e");
    set(4, reachable ? (QString::number(ms) + " ms") : "—");
    // Giữ tối đa 50 dòng nhật ký.
    while (m_log->rowCount() > 50)
        m_log->removeRow(m_log->rowCount() - 1);
}
