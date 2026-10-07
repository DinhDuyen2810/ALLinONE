#pragma once

#include <QWidget>

#include "engine/PublicIpChecker.h"
#include "engine/VpnController.h"

class QLabel;
class QPushButton;
class QTableWidget;
class VpnConnector;

/// Tab chính VPN & Location: quản lý hồ sơ kết nối VPN hệ thống Windows (IKEv2/L2TP/SSTP/PPTP, tích
/// hợp sẵn - không cần cài phần mềm nào khác) theo nhiều máy chủ/quốc gia người dùng tự khai báo, và
/// hiển thị vị trí IP hiện tại (tự cập nhật theo kết nối VPN đang dùng).
///
/// KHÔNG giả lập GPS/cảm biến vị trí của Windows: đã xác nhận qua tài liệu chính thức của Microsoft
/// (Geolocator.DefaultGeoposition) rằng việc này cần một "capability" hạn chế chỉ cấp cho ứng dụng
/// đóng gói MSIX được Microsoft Store phê duyệt/cấp quyền đặc biệt - một ứng dụng Win32 thường như ứng
/// dụng này KHÔNG thể có được, kể cả khi chạy với quyền Administrator. Cách duy nhất khác là ghi thẳng
/// vào một khóa registry KHÔNG CHÍNH THỨC dưới dịch vụ hệ thống `lfsvc` (location service) - không có
/// tài liệu xác nhận định dạng, không kiểm chứng được là có tác dụng thật hay không, và rủi ro làm hỏng
/// dịch vụ định vị thật của máy nếu ghi sai định dạng. Vì vậy module này cung cấp vị trí dựa trên ĐỊA
/// CHỈ IP (những gì tuyệt đại đa số website/dịch vụ thực sự dùng để xác định "vị trí" của bạn - ngay cả
/// phần lớn ứng dụng VPN thương mại "đổi vị trí" cũng hoạt động theo đúng cách này, không phải giả lập
/// GPS phần cứng), đổi theo đúng quốc gia của kết nối VPN đang dùng.
class VpnTab : public QWidget
{
    Q_OBJECT

public:
    explicit VpnTab(QWidget* parent = nullptr);
    ~VpnTab() override;

private slots:
    void onRefreshClicked();
    void onAddClicked();
    void onRemoveClicked();
    void onConnectClicked();
    void onDisconnectClicked();
    void onRowSelectionChanged();
    void onConnectorFinished(bool success, QString message);
    void onCheckLocationClicked();
    void onIpResult(PublicIpInfo info);
    void onIpError(QString message);

private:
    void buildUi();
    void reloadConnections();
    const VpnConnectionStatus* selectedConnection() const;

    QTableWidget* m_table{nullptr}; // [Tên][Quốc gia][Giao thức][Trạng thái]
    QPushButton* m_refreshBtn{nullptr};
    QPushButton* m_addBtn{nullptr};
    QPushButton* m_removeBtn{nullptr};
    QPushButton* m_connectBtn{nullptr};
    QPushButton* m_disconnectBtn{nullptr};
    QLabel* m_statusLabel{nullptr};

    QLabel* m_locationIpLabel{nullptr};
    QLabel* m_locationPlaceLabel{nullptr};
    QPushButton* m_refreshLocationBtn{nullptr};

    QList<VpnConnectionStatus> m_connections;
    VpnConnector* m_connector{nullptr};
    PublicIpChecker* m_ipChecker{nullptr};
};
