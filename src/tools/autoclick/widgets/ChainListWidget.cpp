#include "ChainListWidget.h"
#include "ui/widgets/ModernMenu.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QInputDialog>
#include <QMessageBox>
#include <QDropEvent>
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
    // Không dùng trình sửa tên mặc định của Qt (ô nhập trần không theo style ứng dụng, lại không đi
    // qua renameChainClicked/cập nhật đúng dữ liệu) - double-click mở hộp thoại Đổi tên thật thay vào đó.
    m_listWidget->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_listWidget->setToolTip("Nhấp đúp hoặc chuột phải để đổi tên - kéo-thả để đổi thứ tự");
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

void ChainListWidget::onCustomContextMenuRequested(const QPoint& pos)
{
    int row = m_listWidget->currentRow();
    if (row < 0) return;

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
