#pragma once

#include <QWidget>

#include <functional>

#include "engine/PublicIpChecker.h"
#include "model/VpnGateServer.h"

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class QThread;
class VpnConnector;
class VpnGateClient;

/// Tab "Máy chủ công cộng (VPN Gate)": tải danh sách relay tình nguyện công khai của VPN Gate (dự án ĐH
/// Tsukuba, Nhật), lọc theo quốc gia, rồi KẾT NỐI để đổi IP ra quốc gia đó - đúng trải nghiệm "chọn quốc
/// gia rồi bấm kết nối" của các ứng dụng VPN thương mại, nhưng KHÔNG cần ứng dụng tự dựng máy chủ.
///
/// Kết nối bằng L2TP/IPsec qua RAS có sẵn của Windows (tài khoản công khai của VPN Gate: user/mật khẩu/PSK
/// đều là "vpn"). Ứng dụng tạo một hồ sơ VPN TẠM tên "OneForAll-VPNGate" trỏ tới IP máy chủ đã chọn, kết
/// nối, rồi khi ngắt sẽ tự xóa hồ sơ tạm đó. KHÔNG phải máy chủ nào cũng bật L2TP - nếu kết nối thất bại,
/// người dùng thử máy chủ khác (giao diện có nêu rõ).
class VpnGateTab : public QWidget
{
    Q_OBJECT

public:
    explicit VpnGateTab(QWidget* parent = nullptr);
    ~VpnGateTab() override;

    bool isBusy() const;
    /// Hủy giữa chừng (an toàn - gác máy phiên RAS đang quay số) rồi CHỜ luồng thoát hẳn - gọi trước khi
    /// đóng cửa sổ (sắp hủy đối tượng VpnConnector; hủy QThread đang chạy là hành vi KHÔNG XÁC ĐỊNH).
    void cancelAndWait(int waitMs = 3000);
    /// Yêu cầu hủy, KHÔNG CHỜ - gọi từ VpnControlWindow::forceStopNow() lúc ứng dụng thoát ở nơi khác.
    void requestCancelNoWait();

signals:
    /// Người dùng muốn kiểm tra kết nối tới máy chủ đang chọn - VpnControlWindow chuyển sang tab "Kiểm tra
    /// kết nối" và điền sẵn địa chỉ này (cổng 443 - chỉ báo máy chủ còn sống, xem ConnectivityTab).
    void checkConnectivityRequested(QString host, quint16 port);

private slots:
    void onFetchClicked();
    void onServersFetched(QList<VpnGateServer> servers, QString error);
    void onCountryFilterChanged();
    void onRowSelectionChanged();
    void onConnectClicked();
    void onDisconnectClicked();
    void onCheckConnectivityClicked();
    void onNatFixClicked();
    void onConnectorFinished(bool success, QString message);
    void onCheckLocationClicked();
    void onIpResult(PublicIpInfo info);
    void onIpError(QString message);

private:
    void buildUi();
    void rebuildCountryFilter();
    void applyFilter();
    void updateButtons();
    const VpnGateServer* selectedServer() const;
    /// Chạy một lệnh nền (tạo/xóa hồ sơ VPN tạm qua PowerShell) rồi gọi 'done' trên luồng giao diện.
    void runInBackground(const QString& statusText, std::function<void()> work, std::function<void()> done);
    void removeTempProfileInBackground();

    enum class Phase { Idle, Connecting, Connected, Disconnecting };
    Phase m_phase{Phase::Idle};

    QTableWidget* m_table{nullptr}; // [Quốc gia][Ping][Tốc độ][Phiên][IP]
    QComboBox* m_countryFilter{nullptr};
    QPushButton* m_fetchBtn{nullptr};
    QPushButton* m_connectBtn{nullptr};
    QPushButton* m_disconnectBtn{nullptr};
    QPushButton* m_checkBtn{nullptr};
    QPushButton* m_natFixBtn{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_countLabel{nullptr};

    QLabel* m_locationIpLabel{nullptr};
    QLabel* m_locationPlaceLabel{nullptr};
    QPushButton* m_refreshLocationBtn{nullptr};

    QList<VpnGateServer> m_allServers; // toàn bộ danh sách tải về (đã sắp theo điểm giảm dần)
    QList<VpnGateServer> m_shown;      // các dòng đang hiển thị (sau khi lọc quốc gia)
    QString m_connectedCountry;

    VpnGateClient* m_client{nullptr};
    VpnConnector* m_connector{nullptr};
    PublicIpChecker* m_ipChecker{nullptr};
    QThread* m_backgroundThread{nullptr};
};
