#pragma once

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include "../model/ActionChain.h"
#include <vector>

class ChainListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ChainListWidget(QWidget* parent = nullptr);

    void setChains(const std::vector<ActionChain>& chains);
    int selectedChainIndex() const;
    void setSelectedChainIndex(int index);

signals:
    void chainSelectionChanged(int index);
    void addChainClicked();
    void cloneChainClicked(int index);
    void deleteChainClicked(int index);
    void renameChainClicked(int index, const QString& newName);
    /// Bật/tắt cờ "ghim lên" của chuỗi tại `index` - xem comment `ActionChain::pinned`.
    void togglePinClicked(int index);
    /// Người dùng kéo-thả hàng từ vị trí `from` sang `to` (cùng khuôn `ActionListWidget::actionMoved`).
    void chainMoved(int from, int to);

private slots:
    void onCustomContextMenuRequested(const QPoint& pos);
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    void promptRename(int row);

    QListWidget* m_listWidget;
    QPushButton* m_addButton;
};
