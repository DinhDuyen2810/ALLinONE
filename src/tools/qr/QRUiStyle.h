#pragma once

#include <QString>

/// Style dùng chung cho module QR (đồng bộ Light Theme của toàn ứng dụng).
namespace QRUi
{
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

inline QString inputStyle()
{
    return "QLineEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 8px; font-size: 12px; placeholder-text-color: #8c959f; }"
           "QLineEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border-color: #0969da; }"
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

/// Tooltip cho nội dung KHÔNG TIN CẬY (nội dung mã QR quét được). QToolTip tự đoán định dạng: chuỗi trông
/// giống HTML sẽ được dựng như rich text - một mã QR chứa thẻ (vd <img src="\\máy-lạ\x">) có thể khiến
/// Qt tự nạp tài nguyên từ đường dẫn mạng. Ở đây ÉP rich text nhưng mọi ký tự đặc biệt đã được escape,
/// nên nội dung luôn hiện đúng nguyên văn (pre-wrap giữ xuống dòng).
inline QString plainToolTip(const QString& text)
{
    if (text.isEmpty())
        return QString();
    return "<p style=\"white-space: pre-wrap;\">" + text.toHtmlEscaped() + "</p>";
}

inline QString tableStyle()
{
    return "QTableWidget, QListWidget { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 10px; }"
           "QTableWidget::item, QListWidget::item { padding: 6px 8px; border-bottom: 1px solid #eaeef2; }"
           "QTableWidget::item:selected, QListWidget::item:selected { background-color: #0969da; color: #ffffff; }"
           "QHeaderView::section { background-color: #f6f8fa; color: #57606a; font-weight: bold; border: none; padding: 6px; border-bottom: 1px solid #d0d7de; }";
}
} // namespace QRUi
