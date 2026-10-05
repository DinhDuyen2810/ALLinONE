#pragma once

#include <QWidget>

#include "engine/WlanController.h"

class QLabel;
class QPushButton;
class QTableWidget;
class QTimer;

/// Tab "Mạng xung quanh": quét, hiện danh sách, kết nối/ngắt/quên mạng.
class NetworksTab : public QWidget
{
    Q_OBJECT

public:
    explicit NetworksTab(WlanController* controller, QWidget* parent = nullptr);

    void setAdapter(const QString& guid);
    void refreshNow();     // đọc lại danh sách + trạng thái hiện tại ngay (không yêu cầu quét mới)
    void startActivePolling(); // bật polling khi tab đang hiển thị
    void stopActivePolling();

signals:
    void profilesMayHaveChanged(); // sau khi kết nối thành công (tạo hồ sơ mới) hoặc "quên mạng"

private slots:
    void onScanClicked();
    void onConnectClicked();
    void onDisconnectClicked();
    void onForgetClicked();
    void onHiddenClicked();
    void onLoveClicked();
    void onSelectionChanged();
    void onPollTick();

private:
    void buildUi();
    void reloadNetworks();
    void reloadCurrentConnectionBanner();
    int selectedRow() const;
    WifiNetwork selectedNetwork() const;

    WlanController* m_controller;
    QString m_adapterGuid;

    QLabel* m_banner{nullptr};
    QTableWidget* m_table{nullptr};
    QPushButton* m_scanBtn{nullptr};
    QPushButton* m_connectBtn{nullptr};
    QPushButton* m_disconnectBtn{nullptr};
    QPushButton* m_forgetBtn{nullptr};
    QPushButton* m_hiddenBtn{nullptr};
    QPushButton* m_loveBtn{nullptr};
    QLabel* m_statusLabel{nullptr};

    QList<WifiNetwork> m_networks;
    QTimer* m_scanSettleTimer{nullptr}; // chờ driver hoàn tất quét rồi đọc lại danh sách
    QTimer* m_pollTimer{nullptr};       // làm mới trạng thái kết nối hiện tại định kỳ khi tab đang mở
    bool m_scanning{false};
};
