#pragma once

#include <QWidget>

#include <functional>

#include "model/WireGuardTunnel.h"

class QLabel;
class QPushButton;
class QTableWidget;
class QThread;

/// Tab "WireGuard": nhập tunnel từ tệp .conf (vd xuất từ ProtonVPN/Windscribe/Mullvad bản free, hoặc VPN
/// tự dựng/cơ quan), bật/tắt/gỡ - dùng CHÍNH WireGuard for Windows đã cài trên máy (không đóng gói driver
/// kernel nào - xem WireGuardController.h). Cùng khuôn mẫu với VpnTab: mỗi lúc chỉ một việc nền, banner
/// nhắc chạy lại với quyền Administrator khi cần.
class WireGuardTab : public QWidget
{
    Q_OBJECT

public:
    explicit WireGuardTab(QWidget* parent = nullptr);
    ~WireGuardTab() override;

    bool isBusy() const { return m_backgroundThread != nullptr; }

private slots:
    void onRefreshClicked();
    void onImportClicked();
    void onStartClicked();
    void onStopClicked();
    void onRemoveClicked();
    void onRowSelectionChanged();
    void onRelaunchElevatedClicked();

private:
    void buildUi();
    void updateElevationBanner();
    void updateInstalledBanner();
    void reloadTunnels();
    void populateTable(const QList<WireGuardTunnel>& tunnels, const QString& error);
    void runInBackground(const QString& statusText, std::function<void()> work, std::function<void()> done);
    void updateButtons();
    const WireGuardTunnel* selectedTunnel() const;

    QLabel* m_elevationBanner{nullptr};
    QPushButton* m_relaunchBtn{nullptr};
    QLabel* m_installBanner{nullptr};

    QTableWidget* m_table{nullptr}; // [Tên][Quốc gia][Máy chủ][Trạng thái]
    QPushButton* m_refreshBtn{nullptr};
    QPushButton* m_importBtn{nullptr};
    QPushButton* m_startBtn{nullptr};
    QPushButton* m_stopBtn{nullptr};
    QPushButton* m_removeBtn{nullptr};
    QLabel* m_statusLabel{nullptr};

    QList<WireGuardTunnel> m_tunnels;
    QThread* m_backgroundThread{nullptr};
};
