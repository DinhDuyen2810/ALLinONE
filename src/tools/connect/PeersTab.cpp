#include "PeersTab.h"

#include "ConnectUiStyle.h"
#include "engine/ConnectSessionController.h"

#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace
{
const QList<ScreenSide> kSides = {ScreenSide::None, ScreenSide::Left, ScreenSide::Right, ScreenSide::Top, ScreenSide::Bottom};
}

PeersTab::PeersTab(ConnectSessionController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();
    connect(m_controller, &ConnectSessionController::pairedPeersChanged, this, &PeersTab::reload);
    connect(m_controller, &ConnectSessionController::peerConnectionChanged, this, &PeersTab::onPeerConnectionChanged);
    reload();
}

void PeersTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    m_hintLabel = new QLabel(
        "Xếp mỗi máy vào một hướng (Trái/Phải/Trên/Dưới) so với máy này. Khi đã xếp hướng và máy kia "
        "đang kết nối, đưa chuột chạm mép màn hình tương ứng để chuyển quyền điều khiển sang máy đó. "
        "Nhấn Ctrl+Alt+Home để lấy lại quyền điều khiển bất cứ lúc nào.",
        this);
    m_hintLabel->setWordWrap(true);
    m_hintLabel->setStyleSheet("color: #57606a; font-size: 11px;");
    root->addWidget(m_hintLabel);

    m_table = new QTableWidget(0, 5, this);
    m_table->setHorizontalHeaderLabels({"Tên máy", "Vị trí", "Tự kết nối", "Trạng thái", ""});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setStyleSheet(ConnectUi::tableStyle());
    root->addWidget(m_table, 1);

    auto* btnRow = new QHBoxLayout();
    m_forgetBtn = new QPushButton("🗑 Quên máy đã chọn", this);
    m_forgetBtn->setStyleSheet(ConnectUi::dangerButtonStyle());
    m_forgetBtn->setCursor(Qt::PointingHandCursor);
    m_forgetBtn->setEnabled(false);
    connect(m_forgetBtn, &QPushButton::clicked, this, &PeersTab::onForgetClicked);
    connect(m_table, &QTableWidget::itemSelectionChanged, this,
           [this] { m_forgetBtn->setEnabled(m_table->currentRow() >= 0); });
    btnRow->addWidget(m_forgetBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);
}

void PeersTab::reload()
{
    const auto peers = m_controller->pairedPeers();
    m_table->setRowCount(peers.size());

    for (int i = 0; i < peers.size(); ++i)
    {
        const PairedPeer& p = peers[i];

        auto* nameItem = new QTableWidgetItem(p.machineName);
        nameItem->setData(Qt::UserRole, p.id);
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 0, nameItem);

        auto* sideCombo = new QComboBox(m_table);
        sideCombo->setStyleSheet(ConnectUi::inputStyle());
        for (ScreenSide s : kSides)
            sideCombo->addItem(screenSideName(s), static_cast<int>(s));
        sideCombo->setCurrentIndex(static_cast<int>(p.side));
        const QString peerId = p.id;
        connect(sideCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
               [this, peerId, sideCombo](int) {
                   m_controller->setPeerSide(peerId, static_cast<ScreenSide>(sideCombo->currentData().toInt()));
               });
        m_table->setCellWidget(i, 1, sideCombo);

        auto* autoCheck = new QCheckBox(m_table);
        autoCheck->setChecked(p.autoConnect);
        connect(autoCheck, &QCheckBox::toggled, this,
               [this, peerId](bool on) { m_controller->setPeerAutoConnect(peerId, on); });
        auto* autoWrap = new QWidget(m_table);
        auto* autoLayout = new QHBoxLayout(autoWrap);
        autoLayout->setContentsMargins(0, 0, 0, 0);
        autoLayout->setAlignment(Qt::AlignCenter);
        autoLayout->addWidget(autoCheck);
        m_table->setCellWidget(i, 2, autoWrap);

        const bool connected = m_connectionStatus.value(p.id, false);
        auto* statusItem = new QTableWidgetItem(connected ? "● Đã kết nối" : "○ Ngoại tuyến");
        statusItem->setForeground(connected ? QColor("#1a7f37") : QColor("#8c959f"));
        statusItem->setFlags(statusItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(i, 3, statusItem);

        m_table->setItem(i, 4, new QTableWidgetItem(QString()));
    }

    m_forgetBtn->setEnabled(m_table->currentRow() >= 0);
}

void PeersTab::onPeerConnectionChanged(QString peerId, bool connected)
{
    m_connectionStatus[peerId] = connected;
    reload();
}

void PeersTab::onForgetClicked()
{
    const int row = m_table->currentRow();
    if (row < 0)
        return;
    QTableWidgetItem* item = m_table->item(row, 0);
    if (!item)
        return;
    const QString peerId = item->data(Qt::UserRole).toString();
    const QString name = item->text();

    if (QMessageBox::question(this, "Quên máy", QString("Xóa ghép đôi với \"%1\"? Cần ghép đôi lại từ đầu nếu muốn dùng lại.").arg(name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    m_controller->forgetPeer(peerId);
}
