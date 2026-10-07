#pragma once

#include <QHash>
#include <QWidget>

class ConnectSessionController;
class QLabel;
class QPushButton;
class QTableWidget;

/// Tab "Máy đã ghép đôi": xếp vị trí màn hình, bật/tắt tự kết nối, quên máy.
class PeersTab : public QWidget
{
    Q_OBJECT

public:
    explicit PeersTab(ConnectSessionController* controller, QWidget* parent = nullptr);

    void reload();

private slots:
    void onForgetClicked();
    void onPeerConnectionChanged(QString peerId, bool connected);

private:
    void buildUi();

    ConnectSessionController* m_controller;
    QTableWidget* m_table{nullptr};
    QPushButton* m_forgetBtn{nullptr};
    QLabel* m_hintLabel{nullptr};

    QHash<QString, bool> m_connectionStatus;
};
