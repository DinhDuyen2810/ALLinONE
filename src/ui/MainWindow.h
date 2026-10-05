#pragma once

#include <QMainWindow>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include "../core/ToolManager.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void onToolSelected(int row);
    void onToolDoubleClicked(QListWidgetItem* item);
    void onOpenToolClicked();

private:
    void setupUi();
    void registerTools();

    QListWidget* m_toolListWidget;
    QLabel* m_toolIconLabel;
    QLabel* m_toolTitleLabel;
    QLabel* m_toolStatusLabel;
    QLabel* m_toolDescLabel;
    QPushButton* m_openToolButton;

    ITool* m_selectedTool{nullptr};
};
