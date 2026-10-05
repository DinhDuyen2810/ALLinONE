#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include "../model/Action.h"
#include <vector>

class ActionListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ActionListWidget(QWidget* parent = nullptr);

    void setActions(const std::vector<Action>& actions);
    int selectedActionIndex() const;
    void setSelectedActionIndex(int index);

signals:
    void actionSelectionChanged(int index);
    void addActionClicked();
    void editActionClicked(int index);
    void cloneActionClicked(int index);
    void deleteActionClicked(int index);
    void insertActionClicked(int index, bool before);
    void moveActionClicked(int index, int direction); // -1 up, +1 down

private slots:
    void onCustomContextMenuRequested(const QPoint& pos);

private:
    QTableWidget* m_table;
    QPushButton* m_addButton;
    QPushButton* m_cloneButton;
    QPushButton* m_insertBeforeBtn;
    QPushButton* m_insertAfterBtn;
    QPushButton* m_moveUpBtn;
    QPushButton* m_moveDownBtn;
    QPushButton* m_deleteBtn;
};
