#include "ActionListWidget.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QMenu>
#include <QDropEvent>
#include <QMouseEvent>
#include <QColor>
#include <functional>

namespace
{
/// QTableWidget con tự xử lý kéo-thả để đổi vị trí hàng. KHÔNG dùng cơ chế dropMimeData mặc định của
/// QTableWidget cho việc này - đó là mô hình theo TỪNG Ô (cell), không theo HÀNG hoàn chỉnh, nên kéo-thả
/// nhiều cột dễ làm xáo trộn/mất dữ liệu giữa các ô thay vì di chuyển trọn vẹn một hành động. Thay vào
/// đó: tự bắt sự kiện thả, chỉ dùng nó để biết "từ hàng nào, tới hàng nào", rồi để nơi sở hữu dữ liệu
/// thật (ActionListWidget -> AutoClickWindow) tự sắp xếp lại std::vector<Action> và vẽ lại toàn bộ bảng -
/// không bao giờ để Qt tự ghép dữ liệu ô theo cơ chế mặc định của nó.
class ReorderableTable : public QTableWidget
{
public:
    using QTableWidget::QTableWidget;

    std::function<void(int from, int to)> onRowMoved;

protected:
    void dropEvent(QDropEvent* event) override
    {
        const int fromRow = currentRow();
        const QPoint pos = event->position().toPoint();
        int toRow = rowAt(pos.y());
        if (toRow < 0)
            toRow = (pos.y() <= 0) ? 0 : (rowCount() - 1);

        event->setDropAction(Qt::IgnoreAction); // không để Qt tự ghép dữ liệu ô mặc định
        event->accept();

        if (fromRow >= 0 && toRow >= 0 && fromRow != toRow && onRowMoved)
            onRowMoved(fromRow, toRow);
    }
};
} // namespace

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

    auto* hint = new QLabel("Kéo-thả một hàng để đổi vị trí", this);
    hint->setStyleSheet("color: #8c959f; font-size: 10px; padding-left: 4px;");
    mainLayout->addWidget(hint);

    auto* reorderableTable = new ReorderableTable(this);
    m_table = reorderableTable;
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
    // Thiếu dòng này là nguyên nhân lỗi giao diện thật đã gặp: double-click vào ô mở trình soạn
    // thảo mặc định của Qt (QLineEdit trần, không theo style ứng dụng) đè lên nội dung ô - trông như
    // một khối viên thuốc/kẻ lạ chồng lên chữ. Sửa hành động phải qua panel bên phải, không sửa trực
    // tiếp trên bảng.
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->verticalHeader()->setVisible(false);
    m_table->setShowGrid(false);

    // Kéo-thả để đổi vị trí hành động thay cho nút Lên/Xuống trước đây.
    m_table->setDragEnabled(true);
    m_table->setAcceptDrops(true);
    m_table->setDropIndicatorShown(true);
    m_table->setDragDropMode(QAbstractItemView::InternalMove);
    m_table->setDefaultDropAction(Qt::MoveAction);
    reorderableTable->onRowMoved = [this](int from, int to) { emit actionMoved(from, to); };

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

    // Chỉ còn 3 nút theo đúng yêu cầu: Thêm, Nhân bản, Xóa - "Chèn trước/sau" và "Lên/Xuống" trước đây
    // được thay bằng kéo-thả trực tiếp trên danh sách, ít bước hơn và trực quan hơn.
    auto* btnLayout = new QHBoxLayout();
    btnLayout->setSpacing(6);

    m_addButton = new QPushButton("+ Thêm hành động", this);
    m_cloneButton = new QPushButton("Nhân bản", this);
    m_deleteBtn = new QPushButton("Xóa", this);

    m_addButton->setCursor(Qt::PointingHandCursor);
    m_cloneButton->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);

    QString btnStyle =
        "QPushButton { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 10px; font-size: 11px; font-weight: 600; }"
        "QPushButton:hover { background-color: #f3f4f6; color: #0969da; border-color: #0969da; }"
        "QPushButton:pressed { background-color: #ebecf0; }";

    m_addButton->setStyleSheet(btnStyle);
    m_cloneButton->setStyleSheet(btnStyle);
    m_deleteBtn->setStyleSheet(btnStyle);

    btnLayout->addWidget(m_addButton);
    btnLayout->addWidget(m_cloneButton);
    btnLayout->addWidget(m_deleteBtn);
    mainLayout->addLayout(btnLayout);

    connect(m_addButton, &QPushButton::clicked, this, &ActionListWidget::addActionClicked);

    connect(m_cloneButton, &QPushButton::clicked, this, [this]() {
        int row = selectedActionIndex();
        if (row >= 0) emit cloneActionClicked(row);
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

        const bool dirty = (static_cast<int>(i) == m_dirtyRow);
        QString idxText = QString("%1").arg(i + 1, 2, 10, QChar('0'));
        if (dirty)
            idxText += " *";

        auto* itemIdx = new QTableWidgetItem(idxText);
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
        if (dirty)
        {
            itemIdx->setToolTip("Có thay đổi CHƯA LƯU - bấm \"Lưu hành động (Apply)\" ở panel bên phải để lưu lại");
            itemIdx->setForeground(QColor("#9a6700"));
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

void ActionListWidget::setRowDirty(int row, bool dirty)
{
    const int newDirtyRow = dirty ? row : -1;
    if (newDirtyRow == m_dirtyRow)
        return;

    const int oldDirtyRow = m_dirtyRow;
    m_dirtyRow = newDirtyRow;

    // Chỉ cập nhật lại CỘT SỐ của (tối đa) 2 hàng liên quan thay vì vẽ lại cả bảng - rẻ hơn nhiều vì
    // onPositionCaptured/các trường trong ActionEditorWidget có thể gọi hàm này liên tục khi người dùng
    // đang gõ/chỉnh từng ký tự.
    auto refreshIndexCell = [this](int r) {
        if (r < 0 || r >= m_table->rowCount())
            return;
        auto* item = m_table->item(r, 0);
        if (!item)
            return;
        QString text = QString("%1").arg(r + 1, 2, 10, QChar('0'));
        if (r == m_dirtyRow)
        {
            text += " *";
            item->setToolTip("Có thay đổi CHƯA LƯU - bấm \"Lưu hành động (Apply)\" ở panel bên phải để lưu lại");
            item->setForeground(QColor("#9a6700"));
        }
        else
        {
            item->setToolTip(QString());
            item->setForeground(m_table->palette().text());
        }
        item->setText(text);
    };

    refreshIndexCell(oldDirtyRow);
    refreshIndexCell(newDirtyRow);
}

void ActionListWidget::onCustomContextMenuRequested(const QPoint& pos)
{
    int row = m_table->currentRow();
    if (row < 0) return;

    QMenu menu(this);
    QAction* editAct = menu.addAction("Sửa");
    QAction* cloneAct = menu.addAction("Nhân bản");
    menu.addSeparator();
    QAction* delAct = menu.addAction("Xóa");

    QAction* chosen = menu.exec(m_table->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == editAct) emit editActionClicked(row);
    else if (chosen == cloneAct) emit cloneActionClicked(row);
    else if (chosen == delAct) emit deleteActionClicked(row);
}
