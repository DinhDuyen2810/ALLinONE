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
    void runChainClicked(int index);

private slots:
    void onCustomContextMenuRequested(const QPoint& pos);
    void onItemDoubleClicked(QListWidgetItem* item);

private:
    void promptRename(int row);

    QListWidget* m_listWidget;
    QPushButton* m_addButton;
    QPushButton* m_cloneButton;
    QPushButton* m_deleteButton;
};
