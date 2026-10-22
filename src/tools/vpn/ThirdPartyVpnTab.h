#pragma once

#include <QList>
#include <QWidget>

#include "model/ThirdPartyVpnProfile.h"

class QLabel;
class QPushButton;
class QTableWidget;

/// Tab "VPN đã cài sẵn": phát hiện các hồ sơ VPN đã cấu hình bởi phần mềm bên thứ ba (FortiClient,
/// GlobalProtect, OpenVPN, Cisco Secure Client) rồi cho MỞ chính phần mềm đó để kết nối. Chỉ đọc tên +
/// địa chỉ máy chủ, KHÔNG đụng mật khẩu, KHÔNG kết nối thẳng (giao thức SSL-VPN độc quyền). Xem
/// engine/ThirdPartyVpnScanner.h.
class ThirdPartyVpnTab : public QWidget
{
    Q_OBJECT

public:
    explicit ThirdPartyVpnTab(QWidget* parent = nullptr);

private slots:
    void onRefreshClicked();
    void onOpenClicked();
    void onRowSelectionChanged();

private:
    void buildUi();
    void reload();
    const ThirdPartyVpnProfile* selected() const;

    QTableWidget* m_table{nullptr}; // [Phần mềm][Tên hồ sơ][Máy chủ]
    QPushButton* m_refreshBtn{nullptr};
    QPushButton* m_openBtn{nullptr};
    QLabel* m_statusLabel{nullptr};

    QList<ThirdPartyVpnProfile> m_profiles;
};
