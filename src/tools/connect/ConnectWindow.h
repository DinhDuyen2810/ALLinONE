#pragma once

#include <QWidget>

class ConnectSessionController;
class PairingTab;
class PeersTab;
class QLabel;
class QPlainTextEdit;
class QTabWidget;

/// Cửa sổ Connect Together: ghép đôi, quản lý máy, trạng thái chia sẻ điều khiển.
class ConnectWindow : public QWidget
{
    Q_OBJECT

public:
    explicit ConnectWindow(QWidget* parent = nullptr);
    ~ConnectWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void appendLog(const QString& text);
    void updateRoleBanner();

    ConnectSessionController* m_controller{nullptr};

    QLabel* m_identityLabel{nullptr};
    QLabel* m_roleBanner{nullptr};
    QTabWidget* m_tabs{nullptr};
    PairingTab* m_pairingTab{nullptr};
    PeersTab* m_peersTab{nullptr};
    QPlainTextEdit* m_logView{nullptr};
};
