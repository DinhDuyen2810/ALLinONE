#pragma once

#include <QMessageBox>
#include <QString>

/// Style dùng chung cho module WiFi (đồng bộ Light Theme của toàn ứng dụng).
/// Giống hệt src/tools/qr/QRUiStyle.h - để riêng theo module cho độc lập, dễ thay đổi sau này.
namespace WifiUi
{
/// Hộp thoại cho thông báo có chứa SSID/tên hồ sơ. SSID là dữ liệu KHÔNG TIN CẬY (bất kỳ điểm phát nào
/// quanh đây cũng tự đặt được, tối đa 32 byte tùy ý): QMessageBox/QLabel mặc định tự đoán định dạng và
/// dựng chuỗi trông giống HTML thành rich text - một SSID kiểu <img src="\\máy-lạ\x"> có thể khiến Qt tự
/// mở đường dẫn mạng đó. Mọi nơi hiển thị SSID đều ép Qt::PlainText (hộp thoại qua hàm này, QLabel qua
/// setTextFormat).
inline QMessageBox::StandardButton plainMessage(QWidget* parent, QMessageBox::Icon icon, const QString& title,
                                                const QString& text,
                                                QMessageBox::StandardButtons buttons = QMessageBox::Ok,
                                                QMessageBox::StandardButton defaultButton = QMessageBox::NoButton)
{
    QMessageBox box(icon, title, text, buttons, parent);
    box.setTextFormat(Qt::PlainText);
    if (defaultButton != QMessageBox::NoButton)
        box.setDefaultButton(defaultButton);
    return static_cast<QMessageBox::StandardButton>(box.exec());
}

inline QString buttonStyle()
{
    return "QPushButton { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 7px 14px; font-size: 12px; font-weight: 500; }"
           "QPushButton:hover { background-color: #f3f4f6; color: #0969da; border-color: #0969da; }"
           "QPushButton:pressed { background-color: #ebecf0; }"
           "QPushButton:disabled { background-color: #eaeef2; color: #8c959f; border-color: #d0d7de; }"
           "QPushButton:checked { background-color: #ddf4ff; color: #0969da; border-color: #0969da; }";
}

inline QString primaryButtonStyle()
{
    return "QPushButton { background-color: #0969da; color: white; border: none; border-radius: 8px; padding: 8px 16px; font-size: 12px; font-weight: bold; }"
           "QPushButton:hover { background-color: #0854b0; }"
           "QPushButton:pressed { background-color: #053d82; }"
           "QPushButton:disabled { background-color: #eaeef2; color: #8c959f; }";
}

inline QString dangerButtonStyle()
{
    return "QPushButton { background-color: #ffffff; color: #cf222e; border: 1px solid #d0d7de; border-radius: 8px; padding: 7px 14px; font-size: 12px; font-weight: 600; }"
           "QPushButton:hover { background-color: #fff1f0; border-color: #cf222e; }"
           "QPushButton:pressed { background-color: #ffd7d5; }"
           "QPushButton:disabled { background-color: #eaeef2; color: #8c959f; border-color: #d0d7de; }";
}

inline QString inputStyle()
{
    return "QLineEdit, QComboBox, QSpinBox { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 8px; font-size: 12px; }"
           "QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border-color: #0969da; }"
           "QComboBox QAbstractItemView { background-color: #ffffff; color: #1f2328; selection-background-color: #0969da; selection-color: #ffffff; }";
}

inline QString cardStyle()
{
    return "background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 12px;";
}

inline QString groupStyle()
{
    return "QGroupBox { color: #0969da; font-weight: bold; border: 1px solid #d0d7de; border-radius: 12px; margin-top: 10px; padding-top: 14px; background-color: #ffffff; }"
           "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; font-size: 11px; }";
}

inline QString tableStyle()
{
    return "QTableWidget, QListWidget { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 10px; }"
           "QTableWidget::item, QListWidget::item { padding: 6px 8px; border-bottom: 1px solid #eaeef2; }"
           "QTableWidget::item:selected, QListWidget::item:selected { background-color: #0969da; color: #ffffff; }"
           "QHeaderView::section { background-color: #f6f8fa; color: #57606a; font-weight: bold; border: none; padding: 6px; border-bottom: 1px solid #d0d7de; }";
}

inline QString bannerStyle(bool good)
{
    return QString("background-color: %1; color: %2; border: 1px solid %3; border-radius: 10px; padding: 8px 14px; font-size: 12px;")
        .arg(good ? "rgba(31, 136, 61, 0.08)" : "#ffffff", good ? "#1a7f37" : "#57606a", good ? "rgba(31, 136, 61, 0.3)" : "#d0d7de");
}
} // namespace WifiUi
