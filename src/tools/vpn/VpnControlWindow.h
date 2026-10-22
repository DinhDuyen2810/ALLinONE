#pragma once

#include <QWidget>

class QTabWidget;
class ConnectivityTab;
class VpnGateTab;
class VpnTab;
class WireGuardTab;

/// Cửa sổ VPN & Location: tab "VPN hệ thống" (IKEv2/L2TP/SSTP/PPTP tích hợp sẵn Windows, nhiều máy chủ/
/// quốc gia), tab "WireGuard" (tunnel nhập từ .conf, dùng WireGuard for Windows đã cài trên máy - xem
/// WireGuardTab.h), và hiển thị vị trí IP hiện tại (xem VpnTab.h để biết vì sao không giả lập GPS thiết bị).
class VpnControlWindow : public QWidget
{
    Q_OBJECT

public:
    explicit VpnControlWindow(QWidget* parent = nullptr);
    ~VpnControlWindow() override;

    /// Dừng NGAY phiên RAS (VpnConnector) đang kết nối/ngắt kết nối (nếu có), KHÔNG hỏi xác nhận - khác
    /// closeEvent() bên dưới (có hộp thoại hỏi, dành cho người dùng chủ động bấm X). Gọi từ
    /// VpnControlTool::stopBackgroundWorkForQuit() khi ứng dụng thoát qua qApp->quit() ở nơi khác -
    /// hỏi xác nhận giữa một luồng thoát không thể hủy là vô nghĩa, chỉ cần dừng êm tiến trình ngoài.
    void forceStopNow();

protected:
    /// Chặn đóng cửa sổ (hỏi xác nhận) nếu đang kết nối/ngắt kết nối VPN - tránh hủy QThread đang thực
    /// sự chạy (hành vi KHÔNG XÁC ĐỊNH theo tài liệu Qt), vì bắt tay VPN có thể mất tới 45 giây. Tab
    /// WireGuard chỉ có thao tác NGẮN (wireguard.exe/sc.exe, timeout 15s, không hủy giữa chừng được) nên
    /// ở đó chỉ CHẶN đóng cho tới khi xong, không hỏi hủy.
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();

    QTabWidget* m_tabs{nullptr};
    VpnGateTab* m_vpnGateTab{nullptr};
    ConnectivityTab* m_connectivityTab{nullptr};
    VpnTab* m_vpnTab{nullptr};
    WireGuardTab* m_wireGuardTab{nullptr};
};
