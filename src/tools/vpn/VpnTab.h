#pragma once

#include <QWidget>

#include <functional>

#include "engine/Ipv6GuardSession.h"
#include "engine/PublicIpChecker.h"
#include "engine/PublicIpv6Checker.h"
#include "engine/VpnController.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QThread;
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

    bool isBusy() const;
    /// Hủy giữa chừng (an toàn - gác máy phiên RAS đang quay số, VPN chỉ có 2 trạng thái "đã nối"/"chưa nối", không
    /// có trạng thái dở dang nguy hiểm) rồi CHỜ tới khi luồng thoát hẳn - gọi trước khi đóng cửa sổ (sắp
    /// HỦY đối tượng VpnConnector - bắt buộc phải chờ, hủy QThread đang chạy là hành vi KHÔNG XÁC ĐỊNH).
    void cancelAndWait(int waitMs = 3000);

    /// Yêu cầu hủy, KHÔNG CHỜ (không chặn luồng giao diện) - gọi từ VpnControlWindow::forceStopNow() khi
    /// ứng dụng thoát qua qApp->quit() ở NƠI KHÁC (xem ToolManager::stopAllBackgroundWorkForQuit()).
    /// KHÁC cancelAndWait(): ở đây KHÔNG có rủi ro "hủy QThread đang chạy" vì không hủy đối tượng gì cả -
    /// tiến trình chỉ sắp thoát hẳn (hệ điều hành thu hồi an toàn MỌI thứ, kể cả thread đang chạy, cùng
    /// lúc khi tiến trình kết thúc) - gọi cancelAndWait() ở đây sẽ SAI: có thể chặn cả ứng dụng (đang cố
    /// thoát NGAY cho một hành động khác, vd chạy lại với quyền Admin) tới 45 giây chờ phiên RAS vô ích.
    void requestCancelNoWait();

signals:
    /// Người dùng vừa thêm một hồ sơ chế độ GlobalProtect/FortiClient (kết nối trực tiếp, lưu trong ứng dụng) - cửa sổ chuyển
    /// sang tab "VPN đã cài sẵn" và chọn hồ sơ đó. `protocol` là DirectProtocol dạng số nguyên.
    void directProfileAdded(QString name, int protocol);

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
    void onIpv6Result(PublicIpv6Info info);
    void onGuardReady(bool ok, QString message);
    void onGuardStateChanged(Ipv6GuardSession::State state, QString text);

private:
    void buildUi();
    void reloadConnections();
    void populateConnections(const QList<VpnConnectionStatus>& connections, const QString& error);
    /// Chạy một lệnh PowerShell (liệt kê/thêm/xóa hồ sơ) trên luồng nền rồi gọi 'done' trên luồng giao
    /// diện - các nút bị khóa trong lúc chờ. Mỗi lúc chỉ một việc nền.
    void runInBackground(const QString& statusText, std::function<void()> work, std::function<void()> done);
    /// Bật/tắt các nút theo dòng đang chọn + trạng thái bận (đang kết nối/ngắt kết nối hoặc có việc nền).
    void updateButtons();
    const VpnConnectionStatus* selectedConnection() const;
    /// Hủy lần kết nối đang chờ chặn IPv6 (chưa quay số): xóa trắng mật khẩu đã nạp, gỡ việc chặn.
    void abortGuardWait();

    QTableWidget* m_table{nullptr}; // [Tên][Quốc gia][Giao thức][Trạng thái]
    QPushButton* m_refreshBtn{nullptr};
    QPushButton* m_addBtn{nullptr};
    QPushButton* m_removeBtn{nullptr};
    QPushButton* m_connectBtn{nullptr};
    QPushButton* m_disconnectBtn{nullptr};
    QLabel* m_statusLabel{nullptr};

    QLabel* m_locationIpLabel{nullptr};
    QLabel* m_locationPlaceLabel{nullptr};
    QLabel* m_locationIpv6Label{nullptr};
    QPushButton* m_refreshLocationBtn{nullptr};
    QCheckBox* m_ipv6GuardCheck{nullptr};
    QLabel* m_ipv6GuardLabel{nullptr};

    QList<VpnConnectionStatus> m_connections;
    VpnConnector* m_connector{nullptr};
    QThread* m_backgroundThread{nullptr};
    // Tên đăng nhập vừa nhập cho lần kết nối đang chạy - lưu lại (KHÔNG lưu mật khẩu) nếu kết nối thành công.
    QString m_pendingConnectionName;
    QString m_pendingUsername;
    PublicIpChecker* m_ipChecker{nullptr};
    PublicIpv6Checker* m_ipv6Checker{nullptr};
    /// Chặn rò rỉ IPv6 cho kết nối "toàn bộ lưu lượng qua VPN" (không chia tuyến) - xem Ipv6GuardSession.h.
    Ipv6GuardSession* m_ipv6Guard{nullptr};
    bool m_waitingGuard{false}; ///< đã nạp tài khoản vào VpnConnector, chờ chặn IPv6 xong mới quay số
    QString m_guardedConnection; ///< tên kết nối đang được chặn IPv6 (rỗng = không có)
    QString m_disconnectingName; ///< kết nối đang được ngắt (để biết lần ngắt nào kết thúc phiên được chặn)
};
