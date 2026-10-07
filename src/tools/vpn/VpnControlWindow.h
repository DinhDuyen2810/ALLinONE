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

private:
    void buildUi();

    VpnTab* m_vpnTab{nullptr};
};
