#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;

/// Tab "Cài đặt" của công cụ WiFi.
/// Cho phép cấu hình mật khẩu mặc định và khoảng mã hệ 36 (From/To).
/// Lưu cấu hình qua WifiSettings (QSettings).
/// Không tự thực hiện kết nối.
class SettingsTab : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsTab(QWidget* parent = nullptr);

private slots:
    void onSaveClicked();
    void onTogglePasswordVisibility();

private:
    void buildUi();

    // Mật khẩu mặc định
    QLineEdit* m_passwordEdit{nullptr};
    QPushButton* m_toggleVisBtn{nullptr};

    // Khoảng mã hệ 36
    QLineEdit* m_fromEdit{nullptr};
    QLineEdit* m_toEdit{nullptr};

    // Nút lưu và thông báo
    QPushButton* m_saveBtn{nullptr};
    QLabel* m_statusLabel{nullptr};
};
