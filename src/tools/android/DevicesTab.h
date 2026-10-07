#pragma once

#include <QWidget>

#include "engine/ScrcpyLauncher.h"
#include "model/AndroidDeviceInfo.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTimer;

/// Tab chính Android Phone Control: danh sách thiết bị adb thấy (USB + đã kết nối không dây), ghép đôi
/// không dây, và khởi chạy phiên gương + điều khiển màn hình thời gian thực (qua scrcpy.exe - xem
/// ScrcpyLauncher.h). Tự động làm mới danh sách thiết bị mỗi vài giây (giống NetworksTab của WiFi) để
/// phát hiện cắm/rút USB hoặc đổi trạng thái cấp quyền mà không cần người dùng tự bấm Làm mới.
class DevicesTab : public QWidget
{
    Q_OBJECT

public:
    explicit DevicesTab(QWidget* parent = nullptr);
    ~DevicesTab() override;

    /// Dừng phiên gương/điều khiển scrcpy đang chạy (nếu có) - một điểm vào DUY NHẤT dùng chung cho nút
    /// "Dừng", destructor, VÀ AndroidControlWindow::closeEvent (đóng cửa sổ Android Phone Control chỉ ẨN
    /// nó đi chứ không hủy đối tượng - không tự gọi hàm này từ closeEvent thì phiên scrcpy treo lại ngầm
    /// vô thời hạn dù đã "đóng" cửa sổ, xem AndroidControlWindow.cpp).
    void stopActiveSession();

private slots:
    void onRefreshClicked();
    void onAutoRefreshTick();
    void onPairClicked();
    void onSwitchToWirelessClicked();
    void onControlClicked();
    void onStopClicked();
    void onRecordCheckToggled(bool checked);
    void onBrowseRecordPath();
    void onRowSelectionChanged();
    void onScrcpyStarted();
    void onScrcpyFinished(int exitCode);
    void onScrcpyError(QString message);

private:
    void buildUi();
    void reloadDevices();
    void updateBundleBanner();
    ScrcpyOptions collectOptions() const;
    const AndroidDeviceInfo* selectedDevice() const;

    QLabel* m_bundleBanner{nullptr};
    QTableWidget* m_table{nullptr}; // [Serial][Trạng thái][Model][Kiểu kết nối]
    QPushButton* m_refreshBtn{nullptr};
    QPushButton* m_pairBtn{nullptr};
    QPushButton* m_switchWirelessBtn{nullptr};
    QLabel* m_statusLabel{nullptr};

    // Tùy chọn điều khiển (độ linh hoạt như remote thật - xem scrcpy --help cho ý nghĩa từng cờ)
    QComboBox* m_maxSizeCombo{nullptr};
    QSpinBox* m_bitRateSpin{nullptr};
    QComboBox* m_maxFpsCombo{nullptr};
    QCheckBox* m_stayAwakeCheck{nullptr};
    QCheckBox* m_turnScreenOffCheck{nullptr};
    QCheckBox* m_alwaysOnTopCheck{nullptr};
    QCheckBox* m_fullscreenCheck{nullptr};
    QCheckBox* m_noAudioCheck{nullptr};
    QCheckBox* m_recordCheck{nullptr};
    QLineEdit* m_recordPathEdit{nullptr};
    QPushButton* m_recordBrowseBtn{nullptr};

    QPushButton* m_controlBtn{nullptr};
    QPushButton* m_stopBtn{nullptr};

    ScrcpyLauncher* m_launcher{nullptr};
    QTimer* m_autoRefreshTimer{nullptr};
    QList<AndroidDeviceInfo> m_devices;
};
