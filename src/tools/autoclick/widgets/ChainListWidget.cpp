#include "ChainListWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>

ChainListWidget::ChainListWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    auto* header = new QLabel("ACTION CHAINS", this);
    QFont font = header->font();
    font.setBold(true);
    header->setFont(font);
    header->setStyleSheet("color: #0969da; font-size: 11px; font-weight: bold; letter-spacing: 1.5px; padding-left: 4px;");
    mainLayout->addWidget(header);

    m_listWidget = new QListWidget(this);
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_listWidget->setTextElideMode(Qt::ElideRight);
    m_listWidget->setStyleSheet(
        "QListWidget { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 12px; padding: 6px; }"
        "QListWidget::item { padding: 9px 12px; border-radius: 8px; margin-bottom: 3px; color: #1f2328; font-weight: 500; font-size: 12px; }"
        "QListWidget::item:hover { background-color: #f3f4f6; color: #1f2328; }"
        "QListWidget::item:selected { background-color: #0969da; color: #ffffff; font-weight: bold; }"
    );
    mainLayout->addWidget(m_listWidget);

    connect(m_listWidget, &QListWidget::currentRowChanged, this, &ChainListWidget::chainSelectionChanged);
    connect(m_listWidget, &QListWidget::customContextMenuRequested, this, &ChainListWidget::onCustomContextMenuRequested);

    // Button row
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(6);

    m_addButton = new QPushButton("+ Add", this);
    m_cloneButton = new QPushButton("Clone", this);
    m_deleteButton = new QPushButton("Delete", this);

    m_addButton->setCursor(Qt::PointingHandCursor);
    m_cloneButton->setCursor(Qt::PointingHandCursor);
    m_deleteButton->setCursor(Qt::PointingHandCursor);

    QString btnStyle =
        "QPushButton { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 10px; font-size: 11px; font-weight: 600; }"
        "QPushButton:hover { background-color: #f3f4f6; color: #0969da; border-color: #0969da; }"
        "QPushButton:pressed { background-color: #ebecf0; }";

    m_addButton->setStyleSheet(btnStyle);
    m_cloneButton->setStyleSheet(btnStyle);
    m_deleteButton->setStyleSheet(btnStyle);

    btnLayout->addWidget(m_addButton);
    btnLayout->addWidget(m_cloneButton);
    btnLayout->addWidget(m_deleteButton);
    mainLayout->addLayout(btnLayout);

    connect(m_addButton, &QPushButton::clicked, this, &ChainListWidget::addChainClicked);

    connect(m_cloneButton, &QPushButton::clicked, this, [this]() {
        int row = selectedChainIndex();
        if (row >= 0) emit cloneChainClicked(row);
    });

    connect(m_deleteButton, &QPushButton::clicked, this, [this]() {
        int row = selectedChainIndex();
        if (row >= 0) emit deleteChainClicked(row);
    });
}

void ChainListWidget::setChains(const std::vector<ActionChain>& chains)
{
    int currentRow = m_listWidget->currentRow();
    m_listWidget->clear();

    for (const auto& chain : chains)
    {
        QString title = QString::fromStdString(chain.name);
        QString sub = QString(" (%1 acts)").arg(chain.actions.size());
        auto* item = new QListWidgetItem(title + sub);
        item->setToolTip(title + sub);
        m_listWidget->addItem(item);
    }

    if (currentRow >= 0 && currentRow < m_listWidget->count())
    {
        m_listWidget->setCurrentRow(currentRow);
    }
    else if (m_listWidget->count() > 0)
    {
        m_listWidget->setCurrentRow(0);
    }
}

int ChainListWidget::selectedChainIndex() const
{
    return m_listWidget->currentRow();
}

void ChainListWidget::setSelectedChainIndex(int index)
{
    if (index >= 0 && index < m_listWidget->count())
    {
        m_listWidget->setCurrentRow(index);
    }
}

void ChainListWidget::onCustomContextMenuRequested(const QPoint& pos)
{
    int row = m_listWidget->currentRow();
    if (row < 0) return;

    QMenu menu(this);
    QAction* runAction = menu.addAction("Run Chain");
    QAction* cloneAction = menu.addAction("Clone");
    QAction* renameAction = menu.addAction("Rename");
    menu.addSeparator();
    QAction* deleteAction = menu.addAction("Delete");

    QAction* chosen = menu.exec(m_listWidget->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == runAction)
    {
        emit runChainClicked(row);
    }
    else if (chosen == cloneAction)
    {
        emit cloneChainClicked(row);
    }
    else if (chosen == renameAction)
    {
        bool ok = false;
        QString oldText = m_listWidget->currentItem()->text().split(" (").first();
        QString newName = QInputDialog::getText(this, "Rename Chain", "Tên chuỗi mới:", QLineEdit::Normal, oldText, &ok);
        if (ok && !newName.trimmed().isEmpty())
        {
            emit renameChainClicked(row, newName.trimmed());
        }
    }
    else if (chosen == deleteAction)
    {
        emit deleteChainClicked(row);
    }
}
