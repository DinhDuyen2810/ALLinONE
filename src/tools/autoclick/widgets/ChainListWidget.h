#pragma once

#include <QList>
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
    /// Chỉ số MỌI chuỗi đang được chọn (Ctrl/Shift + bấm chọn nhiều), tăng dần, không trùng.
    /// selectedChainIndex() vẫn là chuỗi HIỆN TẠI (chuỗi bấm gần nhất - chuỗi đang hiển thị ở panel giữa/phải).
    QList<int> selectedChainIndices() const;
    /// Chọn đúng các chuỗi này (bỏ mọi lựa chọn cũ); chuỗi ĐẦU TIÊN thành chuỗi hiện tại.
    void setSelectedChainIndices(const QList<int>& indices);

signals:
    void chainSelectionChanged(int index);
    void addChainClicked();
    void cloneChainClicked(int index);
    void deleteChainClicked(int index);
    /// Menu chuột phải khi đang chọn TỪ 2 CHUỖI TRỞ LÊN (chỉ số tăng dần) - một chuỗi vẫn dùng hai tín hiệu trên.
    void cloneChainsClicked(const QList<int>& indices);
    void deleteChainsClicked(const QList<int>& indices);
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
    /// Các chuỗi mà menu chuột phải tại `pos` tác động tới - cùng quy tắc ActionListWidget::contextRows().
    QList<int> contextRows(const QPoint& pos);

    QListWidget* m_listWidget;
    QPushButton* m_addButton;
};
