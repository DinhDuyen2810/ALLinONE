#include "PairingTab.h"

#include "ConnectUiStyle.h"
#include "engine/ConnectSessionController.h"
#include "engine/PairingCode.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHostAddress>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

PairingTab::PairingTab(ConnectSessionController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();

    connect(m_controller, &ConnectSessionController::pairingCodeGenerated, this, &PairingTab::onCodeGenerated);
    connect(m_controller, &ConnectSessionController::pairingSessionClosed, this, &PairingTab::onSessionClosed);
    connect(m_controller, &ConnectSessionController::pairingSucceeded, this, &PairingTab::onPairingSucceeded);
    connect(m_controller, &ConnectSessionController::pairingFailed, this, &PairingTab::onPairingFailed);
    connect(m_controller, &ConnectSessionController::discoveredPeersChanged, this, &PairingTab::onDiscoveredChanged);

    m_countdownTimer = new QTimer(this);
    m_countdownTimer->setInterval(1000);
    connect(m_countdownTimer, &QTimer::timeout, this, &PairingTab::onCountdownTick);
}

void PairingTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(12);

    // ---------- Sinh mã để máy khác nhập ----------
    auto* genGroup = new QGroupBox("Ghép đôi từ máy khác (tạo mã ở đây, nhập trên máy kia)", this);
    genGroup->setStyleSheet(ConnectUi::groupStyle());
    auto* genLayout = new QVBoxLayout(genGroup);
    genLayout->setSpacing(10);

    auto* introLabel = new QLabel(
        "Mã chỉ dùng được MỘT LẦN và tự hết hạn sau 5 phút. Người ở máy kia phải tự nhập đúng mã này - "
        "không có cách nào ghép đôi mà không có sự đồng ý của cả hai bên.",
        genGroup);
    introLabel->setWordWrap(true);
    introLabel->setStyleSheet("color: #57606a; font-size: 11px; border: none; background: transparent;");
    genLayout->addWidget(introLabel);

    m_codeLabel = new QLabel("— — —", genGroup);
    m_codeLabel->setAlignment(Qt::AlignCenter);
    m_codeLabel->setStyleSheet(
        "color: #0969da; font-size: 32px; font-weight: bold; font-family: 'Consolas', monospace; "
        "border: none; background: transparent; letter-spacing: 2px;");
    genLayout->addWidget(m_codeLabel);

    m_countdownLabel = new QLabel(" ", genGroup);
    m_countdownLabel->setAlignment(Qt::AlignCenter);
    m_countdownLabel->setStyleSheet("color: #9a6700; font-size: 12px; border: none; background: transparent;");
    genLayout->addWidget(m_countdownLabel);

    auto* genBtnRow = new QHBoxLayout();
    genBtnRow->addStretch();
    m_generateBtn = new QPushButton("🔑 Tạo mã ghép đôi mới", genGroup);
    m_generateBtn->setStyleSheet(ConnectUi::primaryButtonStyle());
    m_generateBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn = new QPushButton("Hủy mã", genGroup);
    m_cancelBtn->setStyleSheet(ConnectUi::buttonStyle());
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setEnabled(false);
    connect(m_generateBtn, &QPushButton::clicked, this, &PairingTab::onGenerateClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &PairingTab::onCancelClicked);
    genBtnRow->addWidget(m_generateBtn);
    genBtnRow->addWidget(m_cancelBtn);
    genBtnRow->addStretch();
    genLayout->addLayout(genBtnRow);

    root->addWidget(genGroup);

    // ---------- Ghép đôi vào máy khác ----------
    auto* joinGroup = new QGroupBox("Ghép đôi vào máy khác (nhập mã hiển thị trên máy kia)", this);
    joinGroup->setStyleSheet(ConnectUi::groupStyle() + ConnectUi::inputStyle());
    auto* joinLayout = new QVBoxLayout(joinGroup);
    joinLayout->setSpacing(10);

    auto* discLabel = new QLabel("Máy tìm thấy trong mạng LAN (chọn để tự điền địa chỉ):", joinGroup);
    discLabel->setStyleSheet("color: #57606a; font-size: 11px; border: none; background: transparent;");
    joinLayout->addWidget(discLabel);

    m_discoveredTable = new QTableWidget(0, 3, joinGroup);
    m_discoveredTable->setHorizontalHeaderLabels({"Tên máy", "Địa chỉ", "Trạng thái"});
    m_discoveredTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_discoveredTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_discoveredTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_discoveredTable->verticalHeader()->setVisible(false);
    m_discoveredTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_discoveredTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_discoveredTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_discoveredTable->setMaximumHeight(120);
    m_discoveredTable->setStyleSheet(ConnectUi::tableStyle());
    connect(m_discoveredTable, &QTableWidget::itemSelectionChanged, this, &PairingTab::onDiscoveredSelectionChanged);
    joinLayout->addWidget(m_discoveredTable);

    auto* form = new QFormLayout();
    form->setSpacing(8);
    m_addressEdit = new QLineEdit(joinGroup);
    m_addressEdit->setPlaceholderText("192.168.1.x");
    m_addressEdit->setMinimumWidth(160);
    form->addRow("Địa chỉ IP:", m_addressEdit);

    m_portSpin = new QSpinBox(joinGroup);
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(47632);
    // QSpinBox cần đủ rộng cho 5 chữ số + nút tăng/giảm; stylesheet tùy chỉnh (padding) khiến
    // sizeHint() mặc định tính thiếu, chữ số bị đè lên nút tăng/giảm nếu không đặt rộng tối thiểu.
    m_portSpin->setMinimumWidth(110);
    form->addRow("Cổng:", m_portSpin);

    m_codeEdit = new QLineEdit(joinGroup);
    m_codeEdit->setPlaceholderText("123 456 789");
    m_codeEdit->setMaxLength(11); // 9 số + 2 khoảng trắng khi gõ có định dạng
    m_codeEdit->setMinimumWidth(160);
    form->addRow("Mã ghép đôi:", m_codeEdit);
    joinLayout->addLayout(form);

    auto* joinBtnRow = new QHBoxLayout();
    joinBtnRow->addStretch();
    m_pairBtn = new QPushButton("🔗 Ghép đôi", joinGroup);
    m_pairBtn->setStyleSheet(ConnectUi::primaryButtonStyle());
    m_pairBtn->setCursor(Qt::PointingHandCursor);
    connect(m_pairBtn, &QPushButton::clicked, this, &PairingTab::onPairClicked);
    joinBtnRow->addWidget(m_pairBtn);
    joinLayout->addLayout(joinBtnRow);

    m_pairStatusLabel = new QLabel(joinGroup);
    m_pairStatusLabel->setWordWrap(true);
    m_pairStatusLabel->setStyleSheet("color: #57606a; font-size: 11px; border: none; background: transparent;");
    joinLayout->addWidget(m_pairStatusLabel);

    root->addWidget(joinGroup);
    root->addStretch();
}

void PairingTab::onGenerateClicked()
{
    m_controller->beginPairingSession();
}

void PairingTab::onCancelClicked()
{
    m_controller->cancelPairingSession();
}

void PairingTab::onCodeGenerated(QString displayCode, int expirySeconds)
{
    m_codeLabel->setText(displayCode);
    m_secondsLeft = expirySeconds;
    m_countdownLabel->setText(QString("Hết hạn sau %1:%2").arg(m_secondsLeft / 60).arg(m_secondsLeft % 60, 2, 10, QChar('0')));
    m_countdownTimer->start();
    m_generateBtn->setEnabled(false);
    m_cancelBtn->setEnabled(true);
}

void PairingTab::onCountdownTick()
{
    --m_secondsLeft;
    if (m_secondsLeft <= 0)
    {
        m_countdownTimer->stop();
        return;
    }
    m_countdownLabel->setText(QString("Hết hạn sau %1:%2").arg(m_secondsLeft / 60).arg(m_secondsLeft % 60, 2, 10, QChar('0')));
}

void PairingTab::onSessionClosed()
{
    m_countdownTimer->stop();
    m_codeLabel->setText("— — —");
    m_countdownLabel->setText(" ");
    m_generateBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
}

void PairingTab::onPairingSucceeded(QString peerId, QString machineName)
{
    Q_UNUSED(peerId);
    m_pairStatusLabel->setStyleSheet("color: #1a7f37; font-size: 11px; font-weight: bold; border: none; background: transparent;");
    m_pairStatusLabel->setText("✓ Đã ghép đôi thành công với " + machineName + ". Vào tab \"Máy đã ghép đôi\" để xếp vị trí màn hình.");
    m_codeEdit->clear();
}

void PairingTab::onPairingFailed(QString reason)
{
    m_pairStatusLabel->setStyleSheet("color: #cf222e; font-size: 11px; border: none; background: transparent;");
    m_pairStatusLabel->setText("⚠ " + reason);
}

void PairingTab::onPairClicked()
{
    const QString addrText = m_addressEdit->text().trimmed();
    if (addrText.isEmpty())
    {
        m_pairStatusLabel->setStyleSheet("color: #cf222e; font-size: 11px; border: none; background: transparent;");
        m_pairStatusLabel->setText("⚠ Nhập địa chỉ IP của máy kia.");
        return;
    }
    QHostAddress addr(addrText);
    if (addr.isNull())
    {
        m_pairStatusLabel->setStyleSheet("color: #cf222e; font-size: 11px; border: none; background: transparent;");
        m_pairStatusLabel->setText("⚠ Địa chỉ IP không hợp lệ.");
        return;
    }

    m_pairStatusLabel->setStyleSheet("color: #57606a; font-size: 11px; border: none; background: transparent;");
    m_pairStatusLabel->setText("⏳ Đang ghép đôi...");
    m_controller->connectWithCode(addr, static_cast<quint16>(m_portSpin->value()), m_codeEdit->text());
}

void PairingTab::onDiscoveredChanged()
{
    reloadDiscoveredTable();
}

void PairingTab::reloadDiscoveredTable()
{
    const auto peers = m_controller->discoveredPeers();

    // Giữ lại dòng đang chọn (theo địa chỉ + cổng) qua lần dựng lại bảng.
    QString selectedKey;
    if (QTableWidgetItem* current = m_discoveredTable->item(m_discoveredTable->currentRow(), 0))
        selectedKey = current->data(Qt::UserRole).toString() + ":" + current->data(Qt::UserRole + 1).toString();
    int rowToSelect = -1;

    // Chặn tín hiệu trong lúc dựng lại: việc bảng tự đổi dòng chọn không được ghi đè ô địa chỉ/cổng mà
    // người dùng đang gõ tay bên dưới.
    const QSignalBlocker blocker(m_discoveredTable);
    m_discoveredTable->setRowCount(peers.size());
    for (int i = 0; i < peers.size(); ++i)
    {
        const auto& p = peers[i];
        auto* nameItem = new QTableWidgetItem(p.machineName.isEmpty() ? "(không tên)" : p.machineName);
        auto* addrItem = new QTableWidgetItem(p.address.toString());
        // Máy tự khai dùng phiên bản giao thức khác: nói rõ ngay ở đây, khỏi để người dùng thử ghép đôi rồi
        // nhận về một lỗi kết nối khó hiểu.
        auto* statusItem = new QTableWidgetItem(!p.compatible ? "Khác phiên bản - cần cập nhật"
                                                              : (p.alreadyPaired ? "Đã ghép đôi" : "Chưa ghép đôi"));
        nameItem->setData(Qt::UserRole, p.address.toString());
        nameItem->setData(Qt::UserRole + 1, p.port);
        m_discoveredTable->setItem(i, 0, nameItem);
        m_discoveredTable->setItem(i, 1, addrItem);
        m_discoveredTable->setItem(i, 2, statusItem);
        if (!selectedKey.isEmpty() && selectedKey == p.address.toString() + ":" + QString::number(p.port))
            rowToSelect = i;
    }
    if (rowToSelect >= 0)
        m_discoveredTable->selectRow(rowToSelect);
    else
        m_discoveredTable->clearSelection();
}

void PairingTab::onDiscoveredSelectionChanged()
{
    const int row = m_discoveredTable->currentRow();
    if (row < 0)
        return;
    QTableWidgetItem* nameItem = m_discoveredTable->item(row, 0);
    if (!nameItem)
        return;
    m_addressEdit->setText(nameItem->data(Qt::UserRole).toString());
    m_portSpin->setValue(nameItem->data(Qt::UserRole + 1).toInt());
}
