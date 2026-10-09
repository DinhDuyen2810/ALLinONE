#pragma once

#include <QWidget>

class VpnTab;

/// Cửa sổ VPN & Location: quản lý kết nối VPN hệ thống Windows theo nhiều máy chủ/quốc gia, hiển thị
/// vị trí IP hiện tại (xem VpnTab.h để biết vì sao không giả lập GPS thiết bị).
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
    /// sự chạy (hành vi KHÔNG XÁC ĐỊNH theo tài liệu Qt), vì bắt tay VPN có thể mất tới 45 giây.
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();

    VpnTab* m_vpnTab{nullptr};
};
