#pragma once

#include <QWidget>

class DevicesTab;
class QCloseEvent;

/// Cửa sổ Android Phone Control: liệt kê/ghép đôi thiết bị, khởi chạy gương + điều khiển màn hình thời
/// gian thực (qua scrcpy.exe đóng gói kèm theo - xem AdbController.h/ScrcpyLauncher.h).
class AndroidControlWindow : public QWidget
{
    Q_OBJECT

public:
    explicit AndroidControlWindow(QWidget* parent = nullptr);
    ~AndroidControlWindow() override;

    /// Dừng mọi việc nền (phiên scrcpy, lệnh adb, daemon adb đóng gói kèm) - xem
    /// DevicesTab::stopBackgroundWork(). Dùng cho AndroidControlTool::stopBackgroundWorkForQuit().
    void stopBackgroundWork();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();

    DevicesTab* m_devicesTab{nullptr};
};
