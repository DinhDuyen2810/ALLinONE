#pragma once

#include <QWidget>

#include "engine/ScrcpyLauncher.h"
#include "model/AndroidDeviceInfo.h"

class AdbDeviceLister;
class AdbPairer;
class QCheckBox;
class QComboBox;
class QHideEvent;
class QShowEvent;
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

    /// Dừng MỌI việc nền của tab: phiên scrcpy, timer tự làm mới, lệnh `adb devices` đang chạy dở, và
    /// daemon adb đóng gói kèm (xem AdbController::stopBundledAdbServer() - daemon đó sống sót sau khi
    /// ứng dụng thoát và khóa tệp adb.exe, cản trình cài đặt bản cập nhật). Gọi khi đóng cửa sổ Android
    /// Phone Control và khi ứng dụng thoát; mở lại cửa sổ (showEvent) thì mọi thứ tự chạy lại.
    void stopBackgroundWork();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onRefreshClicked();
    void onAutoRefreshTick();
    void onDevicesListed(const QList<AndroidDeviceInfo>& devices, const QString& error);
    void onPairClicked();
    void onPairFinished(bool ok, const QString& error);
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
    /// Chốt đường dẫn tệp ghi hình cho phiên sắp chạy (tên mặc định kèm dấu thời gian MỚI cho mỗi phiên;
    /// tệp người dùng tự chọn mà đã tồn tại thì hỏi trước khi ghi đè). Trả về false = người dùng hủy.
    bool prepareRecordPath(ScrcpyOptions& options);
    static QString defaultRecordPath();
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
    AdbDeviceLister* m_lister{nullptr};
    AdbPairer* m_pairer{nullptr};
    QTimer* m_autoRefreshTimer{nullptr};
    QList<AndroidDeviceInfo> m_devices;

    bool m_backgroundAllowed{false};    // true chỉ khi tab đang hiện - cấm gọi adb khi cửa sổ đã ẩn/đang thoát
    bool m_manualRefreshPending{false}; // người dùng vừa bấm "Làm mới" - lần liệt kê kế tiếp phải báo kết quả
    QString m_lastListStatus;           // dòng trạng thái của lần liệt kê trước, xem onDevicesListed()
    bool m_recordPathIsAuto{false};     // ô đường dẫn ghi hình đang là tên mặc định tự sinh
    bool m_recordOverwriteConfirmed{false}; // người dùng vừa đồng ý ghi đè trong hộp thoại "Chọn..."
};
