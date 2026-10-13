#include "ChainListWidget.h"
#include "ui/widgets/ModernMenu.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QDropEvent>
#include <algorithm>
#include <functional>

namespace
{
/// Kéo-thả đổi vị trí chuỗi - CÙNG khuôn với `ReorderableTable` trong `ActionListWidget.cpp`: tự bắt sự
/// kiện thả, chỉ dùng nó để biết "từ hàng nào, tới hàng nào", để nơi sở hữu dữ liệu thật
/// (ChainListWidget -> AutoClickWindow) tự sắp xếp lại std::vector<ActionChain> và vẽ lại toàn bộ danh
/// sách - không để Qt tự ghép dữ liệu mục theo cơ chế mặc định.
class ReorderableListWidget : public QListWidget
{
public:
    using QListWidget::QListWidget;

    std::function<void(int from, int to)> onRowMoved;

protected:
    void dropEvent(QDropEvent* event) override
    {
        const int fromRow = currentRow();
        const QPoint pos = event->position().toPoint();
        int toRow = row(itemAt(pos));
        if (toRow < 0)
            toRow = (pos.y() <= 0) ? 0 : (count() - 1);

        event->setDropAction(Qt::IgnoreAction);
        event->accept();

        // Đang chọn NHIỀU chuỗi (Ctrl/Shift + bấm) thì kéo-thả KHÔNG đổi vị trí - xem ReorderableTable::dropEvent().
        if (selectedItems().size() > 1)
            return;

        if (fromRow >= 0 && toRow >= 0 && fromRow != toRow && onRowMoved)
            onRowMoved(fromRow, toRow);
    }
};
} // namespace

ChainListWidget::ChainListWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(8, 8, 8, 8);
    mainLayout->setSpacing(8);

    auto* header = new QLabel("CHUỖI HÀNH ĐỘNG", this);
    QFont font = header->font();
    font.setBold(true);
    header->setFont(font);
    header->setStyleSheet("color: #0969da; font-size: 11px; font-weight: bold; letter-spacing: 1.5px; padding-left: 4px;");
    mainLayout->addWidget(header);

    auto* reorderableList = new ReorderableListWidget(this);
    m_listWidget = reorderableList;
    m_listWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    // Ctrl + bấm = thêm/bớt MỘT chuỗi, Shift + bấm = chọn cả khoảng (yêu cầu người dùng: xóa/nhân bản nhiều chuỗi
    // một lượt). Chuỗi HIỆN TẠI (bấm gần nhất) vẫn là chuỗi đang hiển thị.
    m_listWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    // Không dùng trình sửa tên mặc định của Qt (ô nhập trần không theo style ứng dụng, lại không đi
    // qua renameChainClicked/cập nhật đúng dữ liệu) - double-click mở hộp thoại Đổi tên thật thay vào đó.
    m_listWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listWidget->setToolTip("Nhấp đúp hoặc chuột phải để đổi tên - kéo-thả để đổi thứ tự - giữ Ctrl/Shift rồi bấm "
                             "để chọn nhiều chuỗi (chuột phải: xóa/nhân bản)");
    m_listWidget->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_listWidget->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_listWidget->setTextElideMode(Qt::ElideRight);
    // Kéo-thả để đổi vị trí chuỗi, cùng cơ chế ActionListWidget đã dùng cho danh sách hành động.
    m_listWidget->setDragEnabled(true);
    m_listWidget->setAcceptDrops(true);
    m_listWidget->setDropIndicatorShown(true);
    m_listWidget->setDragDropMode(QAbstractItemView::InternalMove);
    m_listWidget->setDefaultDropAction(Qt::MoveAction);
    reorderableList->onRowMoved = [this](int from, int to) { emit chainMoved(from, to); };
    m_listWidget->setStyleSheet(
        "QListWidget { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 12px; padding: 6px; outline: none; }"
        "QListWidget::item { padding: 9px 12px; border-radius: 8px; margin-bottom: 3px; color: #1f2328; font-weight: 500; font-size: 12px; }"
        "QListWidget::item:hover { background-color: #f3f4f6; color: #1f2328; }"
        "QListWidget::item:selected { background-color: #0969da; color: #ffffff; font-weight: bold; }"
        "QListWidget::item:focus { outline: none; border: none; }"
    );
    mainLayout->addWidget(m_listWidget);

    connect(m_listWidget, &QListWidget::currentRowChanged, this, &ChainListWidget::chainSelectionChanged);
    connect(m_listWidget, &QListWidget::customContextMenuRequested, this, &ChainListWidget::onCustomContextMenuRequested);
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this, &ChainListWidget::onItemDoubleClicked);

    // Chỉ còn "+ Thêm" - Nhân bản/Xóa đã chuyển hẳn vào menu chuột phải (trùng chức năng, yêu cầu người
    // dùng v1.19.8).
    auto* btnLayout = new QVBoxLayout();
    btnLayout->setSpacing(6);

    m_addButton = new QPushButton("+ Thêm", this);
    m_addButton->setCursor(Qt::PointingHandCursor);
    m_addButton->setStyleSheet(
        "QPushButton { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 10px; font-size: 11px; font-weight: 600; }"
        "QPushButton:hover { background-color: #f3f4f6; color: #0969da; border-color: #0969da; }"
        "QPushButton:pressed { background-color: #ebecf0; }"
    );
    btnLayout->addWidget(m_addButton);
    mainLayout->addLayout(btnLayout);

    connect(m_addButton, &QPushButton::clicked, this, &ChainListWidget::addChainClicked);
}

void ChainListWidget::setChains(const std::vector<ActionChain>& chains)
{
    int currentRow = m_listWidget->currentRow();
    m_listWidget->clear();

    for (const auto& chain : chains)
    {
        QString title = QString::fromStdString(chain.name);
        QString sub = QString(" (%1 acts)").arg(chain.actions.size());
        QString display = chain.pinned ? (QString::fromUtf8("📌 ") + title + sub) : (title + sub);
        auto* item = new QListWidgetItem(display);
        item->setToolTip(title + sub);
        item->setData(Qt::UserRole, title);      // tên gốc cho hộp thoại Đổi tên - xem promptRename()
        item->setData(Qt::UserRole + 1, chain.pinned); // trạng thái ghim - đổi chữ menu chuột phải
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

QList<int> ChainListWidget::selectedChainIndices() const
{
    QList<int> rows;
    for (QListWidgetItem* item : m_listWidget->selectedItems())
        rows << m_listWidget->row(item);
    std::sort(rows.begin(), rows.end());
    rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
    return rows;
}

void ChainListWidget::setSelectedChainIndices(const QList<int>& indices)
{
    QList<int> valid;
    for (int i : indices)
        if (i >= 0 && i < m_listWidget->count() && !valid.contains(i))
            valid << i;
    if (valid.isEmpty())
        return;
    m_listWidget->clearSelection();
    for (int i : valid)
        m_listWidget->item(i)->setSelected(true);
    // Chuỗi hiện tại = chuỗi đầu tiên, KHÔNG đổi lựa chọn vừa đặt (NoUpdate).
    m_listWidget->selectionModel()->setCurrentIndex(m_listWidget->model()->index(valid.first(), 0),
                                                    QItemSelectionModel::NoUpdate);
}

QList<int> ChainListWidget::contextRows(const QPoint& pos)
{
    const QList<int> selected = selectedChainIndices();
    QListWidgetItem* at = m_listWidget->itemAt(pos);
    if (at)
    {
        const int row = m_listWidget->row(at);
        if (selected.contains(row))
            return selected; // bấm phải TRONG vùng đang chọn -> tác động cả vùng chọn
        setSelectedChainIndex(row); // ngoài vùng chọn -> chọn riêng chuỗi đó (bỏ vùng chọn cũ)
        return {row};
    }
    if (!selected.isEmpty())
        return selected;
    const int current = m_listWidget->currentRow();
    return current >= 0 ? QList<int>{current} : QList<int>{};
}

void ChainListWidget::onCustomContextMenuRequested(const QPoint& pos)
{
    const QList<int> rows = contextRows(pos);
    if (rows.isEmpty()) return;

    if (rows.size() > 1)
    {
        // Nhiều chuỗi: chỉ Nhân bản/Xóa (Đổi tên và Ghim chỉ có nghĩa với MỘT chuỗi).
        QMenu multiMenu(this);
        ModernMenu::style(&multiMenu);
        QAction* cloneMulti = ModernMenu::addAction(multiMenu, QString("Nhân bản %1 chuỗi").arg(rows.size()));
        multiMenu.addSeparator();
        QAction* deleteMulti = ModernMenu::addAction(multiMenu, QString("Xóa %1 chuỗi").arg(rows.size()));
        QAction* picked = multiMenu.exec(m_listWidget->mapToGlobal(pos));
        if (picked == cloneMulti)
            emit cloneChainsClicked(rows);
        else if (picked == deleteMulti)
            emit deleteChainsClicked(rows);
        return;
    }

    const int row = rows.first();
    const bool pinned = m_listWidget->item(row)->data(Qt::UserRole + 1).toBool();

    QMenu menu(this);
    ModernMenu::style(&menu);
    QAction* cloneAction = ModernMenu::addAction(menu, "Nhân bản");
    QAction* renameAction = ModernMenu::addAction(menu, "Đổi tên");
    QAction* pinAction = ModernMenu::addAction(menu, pinned ? "Bỏ ghim" : "Ghim lên");
    menu.addSeparator();
    QAction* deleteAction = ModernMenu::addAction(menu, "Xóa");

    QAction* chosen = menu.exec(m_listWidget->mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == cloneAction)
    {
        emit cloneChainClicked(row);
    }
    else if (chosen == renameAction)
    {
        promptRename(row);
    }
    else if (chosen == pinAction)
    {
        emit togglePinClicked(row);
    }
    else if (chosen == deleteAction)
    {
        emit deleteChainClicked(row);
    }
}

void ChainListWidget::onItemDoubleClicked(QListWidgetItem* item)
{
    const int row = m_listWidget->row(item);
    if (row >= 0)
        promptRename(row);
}

void ChainListWidget::promptRename(int row)
{
    auto* item = m_listWidget->item(row);
    if (!item)
        return;

    bool ok = false;
    // Lấy tên gốc đã lưu kèm mục, KHÔNG tách ngược từ chữ hiển thị "<tên> (N acts)": tách tại " (" đầu
    // tiên cắt cụt mọi tên có sẵn ngoặc đơn - vd chuỗi nhân bản "Đăng nhập (Copy)" hiện ra ô đổi tên chỉ
    // còn "Đăng nhập".
    QString oldText = item->data(Qt::UserRole).toString();
    QString newName = QInputDialog::getText(this, "Đổi tên chuỗi", "Tên chuỗi mới:", QLineEdit::Normal, oldText, &ok);
    if (ok && !newName.trimmed().isEmpty())
    {
        emit renameChainClicked(row, newName.trimmed());
    }
}
