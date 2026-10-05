#include "QRHistoryTab.h"

#include "QRHistoryStore.h"
#include "QRUiStyle.h"

#include <QGuiApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

QRHistoryTab::QRHistoryTab(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({"Thời gian", "Nguồn", "Loại", "Nội dung"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->setShowGrid(false);
    m_table->setStyleSheet(QRUi::tableStyle());
    root->addWidget(m_table, 1);

    auto* row = new QHBoxLayout();
    m_copyBtn = new QPushButton("📋 Sao chép", this);
    m_recreateBtn = new QPushButton("🔁 Tạo lại mã", this);
    m_deleteBtn = new QPushButton("Xóa mục", this);
    m_clearBtn = new QPushButton("Xóa tất cả", this);
    for (QPushButton* b : {m_copyBtn, m_recreateBtn, m_deleteBtn, m_clearBtn})
    {
        b->setStyleSheet(QRUi::buttonStyle());
        b->setCursor(Qt::PointingHandCursor);
        row->addWidget(b);
    }
    row->addStretch();
    root->addLayout(row);

    connect(m_copyBtn, &QPushButton::clicked, this, &QRHistoryTab::copySelected);
    connect(m_recreateBtn, &QPushButton::clicked, this, &QRHistoryTab::recreateSelected);
    connect(m_deleteBtn, &QPushButton::clicked, this, &QRHistoryTab::deleteSelected);
    connect(m_clearBtn, &QPushButton::clicked, this, &QRHistoryTab::clearAll);
    connect(m_table, &QTableWidget::cellDoubleClicked, this, [this](int, int) { copySelected(); });
    connect(&QRHistoryStore::instance(), &QRHistoryStore::changed, this, &QRHistoryTab::reload);

    reload();
}

int QRHistoryTab::rowCount() const
{
    return m_table->rowCount();
}

int QRHistoryTab::selectedIndex() const
{
    return m_table->currentRow();
}

void QRHistoryTab::reload()
{
    const auto& entries = QRHistoryStore::instance().entries();
    m_table->setRowCount(entries.size());
    for (int i = 0; i < entries.size(); ++i)
    {
        const QRHistoryEntry& e = entries[i];
        m_table->setItem(i, 0, new QTableWidgetItem(e.time.toString("dd/MM/yyyy HH:mm:ss")));
        m_table->setItem(i, 1, new QTableWidgetItem(e.source == "scan" ? "Quét" : "Tạo"));
        m_table->setItem(i, 2, new QTableWidgetItem(e.typeName));
        auto* content = new QTableWidgetItem(e.content.simplified());
        content->setToolTip(e.content);
        m_table->setItem(i, 3, content);
    }
    const bool any = !entries.isEmpty();
    for (QPushButton* b : {m_copyBtn, m_recreateBtn, m_deleteBtn, m_clearBtn})
        b->setEnabled(any);
}

void QRHistoryTab::copySelected()
{
    const int i = selectedIndex();
    const auto& entries = QRHistoryStore::instance().entries();
    if (i >= 0 && i < entries.size())
        QGuiApplication::clipboard()->setText(entries[i].content);
}

void QRHistoryTab::recreateSelected()
{
    const int i = selectedIndex();
    const auto& entries = QRHistoryStore::instance().entries();
    if (i >= 0 && i < entries.size())
        emit recreateRequested(entries[i].content);
}

void QRHistoryTab::deleteSelected()
{
    const int i = selectedIndex();
    if (i >= 0)
        QRHistoryStore::instance().removeAt(i);
}

void QRHistoryTab::clearAll()
{
    if (QRHistoryStore::instance().entries().isEmpty())
        return;
    if (QMessageBox::question(this, "Xóa lịch sử", "Xóa toàn bộ lịch sử tạo/quét mã QR?",
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes)
        QRHistoryStore::instance().clear();
}
