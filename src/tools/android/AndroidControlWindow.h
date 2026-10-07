#pragma once

#include <QWidget>

class DevicesTab;

/// Cửa sổ Android Phone Control: liệt kê/ghép đôi thiết bị, khởi chạy gương + điều khiển màn hình thời
/// gian thực (qua scrcpy.exe đóng gói kèm theo - xem AdbController.h/ScrcpyLauncher.h).
class AndroidControlWindow : public QWidget
{
    Q_OBJECT

public:
    explicit AndroidControlWindow(QWidget* parent = nullptr);
    ~AndroidControlWindow() override;

private:
    void buildUi();

    DevicesTab* m_devicesTab{nullptr};
};
