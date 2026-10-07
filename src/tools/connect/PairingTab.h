#pragma once

#include <QWidget>

class ConnectSessionController;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

/// Tab "Ghép đôi": sinh mã để máy khác nhập, hoặc tự nhập mã do máy khác hiển thị.
class PairingTab : public QWidget
{
    Q_OBJECT

public:
    explicit PairingTab(ConnectSessionController* controller, QWidget* parent = nullptr);

private slots:
    void onGenerateClicked();
    void onCancelClicked();
    void onPairClicked();
    void onDiscoveredSelectionChanged();
    void onCountdownTick();

    void onCodeGenerated(QString displayCode, int expirySeconds);
    void onSessionClosed();
    void onPairingSucceeded(QString peerId, QString machineName);
    void onPairingFailed(QString reason);
    void onDiscoveredChanged();

private:
    void buildUi();
    void reloadDiscoveredTable();

    ConnectSessionController* m_controller;

    QPushButton* m_generateBtn{nullptr};
    QPushButton* m_cancelBtn{nullptr};
    QLabel* m_codeLabel{nullptr};
    QLabel* m_countdownLabel{nullptr};
    QTimer* m_countdownTimer{nullptr};
    int m_secondsLeft{0};

    QTableWidget* m_discoveredTable{nullptr};
    QLineEdit* m_addressEdit{nullptr};
    QSpinBox* m_portSpin{nullptr};
    QLineEdit* m_codeEdit{nullptr};
    QPushButton* m_pairBtn{nullptr};
    QLabel* m_pairStatusLabel{nullptr};
};
