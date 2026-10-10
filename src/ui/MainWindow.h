#pragma once

#include <QMainWindow>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include "../core/ToolManager.h"
#include "../core/update/UpdateChecker.h"

class UpdateInstaller;

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
    void onUpdateAvailable(UpdateInfo info);
    void onUpdateUpToDate();
    void onUpdateCheckFailed(QString error);
    void onCheckUpdateClicked();

private:
    void setupUi();
    void registerTools();

    QListWidget* m_toolListWidget;
    QLabel* m_toolIconLabel;
    QLabel* m_toolTitleLabel;
    QLabel* m_toolStatusLabel;
    QLabel* m_toolDescLabel;
    QPushButton* m_openToolButton;
    QPushButton* m_checkUpdateButton;

    ITool* m_selectedTool{nullptr};

    // Tự kiểm tra cập nhật một lần mỗi khi mở ứng dụng (không lặp định kỳ - theo đúng yêu cầu người
    // dùng) - xem core/update/UpdateChecker.h/UpdateInstaller.h. Cùng một UpdateChecker/signal cho cả lần
    // tự động lúc mở app LẪN nút "Kiểm tra cập nhật" thủ công - lần tự động im lặng khi không có gì mới/
    // lỗi mạng (không phải thao tác người dùng tự yêu cầu), lần thủ công PHẢI phản hồi rõ ràng dù kết quả
    // là gì - m_manualUpdateCheckPending phân biệt hai trường hợp đó trong slot upToDate()/checkFailed().
    UpdateChecker* m_updateChecker{nullptr};
    UpdateInstaller* m_updateInstaller{nullptr};
    bool m_manualUpdateCheckPending{false};
};
