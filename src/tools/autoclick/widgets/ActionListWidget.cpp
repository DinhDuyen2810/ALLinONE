#include "ActionListWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QMenu>

ActionListWidget::ActionListWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    auto* header = new QLabel("DANH SÁCH HÀNH ĐỘNG", this);
    QFont font = header->font();
    font.setBold(true);
    header->setFont(font);
    header->setStyleSheet("color: #0969da; font-size: 11px; font-weight: bold; letter-spacing: 1.5px; padding-left: 4px;");
    mainLayout->addWidget(header);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(6);
    m_table->setHorizontalHeaderLabels({"#", "Loại", "Mô tả", "Chờ trước", "Chờ sau", "Thời lượng"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(false);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->setWordWrap(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);

    m_table->setStyleSheet(
        "QTableWidget { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 12px; gridline-color: transparent; padding: 4px; }"
        "QTableWidget::item { padding: 8px 10px; color: #1f2328; border-bottom: 1px solid #eaeef2; font-size: 12px; }"
        "QTableWidget::item:selected { background-color: #0969da; color: #ffffff; font-weight: bold; border-radius: 4px; }"
        "QHeaderView::section { background-color: #f6f8fa; color: #57606a; font-weight: bold; border: none; padding: 8px; border-bottom: 1px solid #d0d7de; font-size: 11px; }"
    );
    mainLayout->addWidget(m_table);

    connect(m_table, &QTableWidget::currentCellChanged, this, [this](int currentRow, int /*col*/, int /*prevRow*/, int /*prevCol*/) {
        emit actionSelectionChanged(currentRow);
    });
    connect(m_table, &QTableWidget::customContextMenuRequested, this, &ActionListWidget::onCustomContextMenuRequested);

    // Toolbar buttons (2 rows to prevent horizontal squishing)
    auto* btnLayout = new QVBoxLayout();
    btnLayout->setSpacing(4);

    m_addButton = new QPushButton("+ Thêm hành động", this);
    m_cloneButton = new QPushButton("Nhân bản", this);
    m_insertBeforeBtn = new QPushButton("Trước", this);
    m_insertAfterBtn = new QPushButton("Sau", this);
    m_moveUpBtn = new QPushButton("▲ Lên", this);
    m_moveDownBtn = new QPushButton("▼ Xuống", this);
    m_deleteBtn = new QPushButton("Xóa", this);

    m_addButton->setCursor(Qt::PointingHandCursor);
    m_cloneButton->setCursor(Qt::PointingHandCursor);
    m_insertBeforeBtn->setCursor(Qt::PointingHandCursor);
    m_insertAfterBtn->setCursor(Qt::PointingHandCursor);
    m_moveUpBtn->setCursor(Qt::PointingHandCursor);
    m_moveDownBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);

    QString btnStyle =
        "QPushButton { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 10px; font-size: 11px; font-weight: 600; }"
        "QPushButton:hover { background-color: #f3f4f6; color: #0969da; border-color: #0969da; }"
        "QPushButton:pressed { background-color: #ebecf0; }";

    m_addButton->setStyleSheet(btnStyle);
    m_cloneButton->setStyleSheet(btnStyle);
    m_insertBeforeBtn->setStyleSheet(btnStyle);
    m_insertAfterBtn->setStyleSheet(btnStyle);
    m_moveUpBtn->setStyleSheet(btnStyle);
    m_moveDownBtn->setStyleSheet(btnStyle);
    m_deleteBtn->setStyleSheet(btnStyle);

    auto* row1 = new QHBoxLayout();
    row1->setSpacing(6);
    row1->addWidget(m_addButton);
    row1->addWidget(m_cloneButton);
    row1->addWidget(m_insertBeforeBtn);
    row1->addWidget(m_insertAfterBtn);

    auto* row2 = new QHBoxLayout();
    row2->setSpacing(6);
    row2->addWidget(m_moveUpBtn);
    row2->addWidget(m_moveDownBtn);
    row2->addWidget(m_deleteBtn);

    btnLayout->addLayout(row1);
    btnLayout->addLayout(row2);
    mainLayout->addLayout(btnLayout);

    connect(m_addButton, &QPushButton::clicked, this, &ActionListWidget::addActionClicked);

    connect(m_cloneButton, &QPushButton::clicked, this, [this]() {
        int row = selectedActionIndex();
        if (row >= 0) emit cloneActionClicked(row);
    });

    connect(m_insertBeforeBtn, &QPushButton::clicked, this, [this]() {
        int row = selectedActionIndex();
        if (row >= 0) emit insertActionClicked(row, true);
    });

    connect(m_insertAfterBtn, &QPushButton::clicked, this, [this]() {
        int row = selectedActionIndex();
        if (row >= 0) emit insertActionClicked(row, false);
    });

    connect(m_moveUpBtn, &QPushButton::clicked, this, [this]() {
        int row = selectedActionIndex();
        if (row > 0) emit moveActionClicked(row, -1);
    });

    connect(m_moveDownBtn, &QPushButton::clicked, this, [this]() {
        int row = selectedActionIndex();
        if (row >= 0 && row < m_table->rowCount() - 1) emit moveActionClicked(row, 1);
    });

    connect(m_deleteBtn, &QPushButton::clicked, this, [this]() {
        int row = selectedActionIndex();
        if (row >= 0) emit deleteActionClicked(row);
    });
}

void ActionListWidget::setActions(const std::vector<Action>& actions)
{
    int currentRow = m_table->currentRow();
    m_table->setRowCount(0);

    for (size_t i = 0; i < actions.size(); ++i)
    {
        const auto& act = actions[i];
        int row = m_table->rowCount();
        m_table->insertRow(row);

        auto* itemIdx = new QTableWidgetItem(QString("%1").arg(i + 1, 2, 10, QChar('0')));
        auto* itemType = new QTableWidgetItem(act.typeName());
        auto* itemDesc = new QTableWidgetItem(act.description());
        auto* itemBefore = new QTableWidgetItem(QString("%1 ms").arg(act.waitBefore.count()));
        auto* itemAfter = new QTableWidgetItem(QString("%1 ms").arg(act.waitAfter.count()));
        auto* itemDur = new QTableWidgetItem(act.duration.count() > 0 ? QString("%1 ms").arg(act.duration.count()) : "-");

        itemType->setToolTip(act.typeName());
        itemDesc->setToolTip(act.description());
        itemBefore->setToolTip(QString("Thời gian chờ trước khi thực hiện: %1 ms").arg(act.waitBefore.count()));
        itemAfter->setToolTip(QString("Thời gian chờ sau khi thực hiện: %1 ms").arg(act.waitAfter.count()));
        if (act.duration.count() > 0)
        {
            itemDur->setToolTip(QString("Thời lượng thao tác: %1 ms").arg(act.duration.count()));
        }

        itemIdx->setTextAlignment(Qt::AlignCenter);
        itemType->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        itemBefore->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        itemAfter->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        itemDur->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_table->setItem(row, 0, itemIdx);
        m_table->setItem(row, 1, itemType);
        m_table->setItem(row, 2, itemDesc);
        m_table->setItem(row, 3, itemBefore);
        m_table->setItem(row, 4, itemAfter);
        m_table->setItem(row, 5, itemDur);
    }

    if (currentRow >= 0 && currentRow < m_table->rowCount())
    {
        m_table->setCurrentCell(currentRow, 0);
    }
    else if (m_table->rowCount() > 0)
    {
        m_table->setCurrentCell(0, 0);
    }
}

int ActionListWidget::selectedActionIndex() const
{
    return m_table->currentRow();
}

void ActionListWidget::setSelectedActionIndex(int index)
{
    if (index >= 0 && index < m_table->rowCount())
    {
        m_table->setCurrentCell(index, 0);
    }
}

void ActionListWidget::onCustomContextMenuRequested(const QPoint& pos)
{
    int row = m_table->currentRow();
    if (row < 0) return;

    QMenu menu(this);
    QAction* editAct = menu.addAction("Sửa");
    QAction* cloneAct = menu.addAction("Nhân bản");
    menu.addSeparator();
    QAction* insBeforeAct = menu.addAction("Chèn trước");
    QAction* insAfterAct = menu.addAction("Chèn sau");
    menu.addSeparator();
    QAction* upAct = menu.addAction("Di chuyển lên");
    QAction* downAct = menu.addAction("Di chuyển xuống");
    menu.addSeparator();
    QAction* delAct = menu.addAction("Xóa");

    QAction* chosen = menu.exec(m_table->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == editAct) emit editActionClicked(row);
    else if (chosen == cloneAct) emit cloneActionClicked(row);
    else if (chosen == insBeforeAct) emit insertActionClicked(row, true);
    else if (chosen == insAfterAct) emit insertActionClicked(row, false);
    else if (chosen == upAct) { if (row > 0) emit moveActionClicked(row, -1); }
    else if (chosen == downAct) { if (row < m_table->rowCount() - 1) emit moveActionClicked(row, 1); }
    else if (chosen == delAct) emit deleteActionClicked(row);
}
