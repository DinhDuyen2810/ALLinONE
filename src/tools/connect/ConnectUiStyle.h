#pragma once

#include <QString>

/// Style dùng chung cho module Connect Together (đồng bộ Light Theme của toàn ứng dụng).
/// Cùng mẫu với src/tools/wifi/WifiUiStyle.h và src/tools/qr/QRUiStyle.h.
namespace ConnectUi
{
inline QString buttonStyle()
{
    return "QPushButton { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 7px 14px; font-size: 12px; font-weight: 500; }"
           "QPushButton:hover { background-color: #f3f4f6; color: #0969da; border-color: #0969da; }"
           "QPushButton:pressed { background-color: #ebecf0; }"
           "QPushButton:disabled { background-color: #eaeef2; color: #8c959f; border-color: #d0d7de; }";
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

inline QString groupStyle()
{
    return "QGroupBox { color: #0969da; font-weight: bold; border: 1px solid #d0d7de; border-radius: 12px; margin-top: 10px; padding-top: 14px; background-color: #ffffff; }"
           "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; font-size: 11px; }";
}

inline QString tableStyle()
{
    return "QTableWidget { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 10px; }"
           "QTableWidget::item { padding: 6px 8px; border-bottom: 1px solid #eaeef2; }"
           "QTableWidget::item:selected { background-color: #0969da; color: #ffffff; }"
           "QHeaderView::section { background-color: #f6f8fa; color: #57606a; font-weight: bold; border: none; padding: 6px; border-bottom: 1px solid #d0d7de; }";
}

inline QString bannerStyle(const QString& kind) // "idle" | "active" | "warn"
{
    if (kind == "active")
        return "background-color: rgba(9, 105, 218, 0.08); color: #0969da; border: 1px solid rgba(9, 105, 218, 0.3); border-radius: 10px; padding: 10px 14px; font-size: 12px; font-weight: bold;";
    if (kind == "warn")
        return "background-color: rgba(154, 103, 0, 0.08); color: #9a6700; border: 1px solid rgba(154, 103, 0, 0.3); border-radius: 10px; padding: 10px 14px; font-size: 12px;";
    return "background-color: rgba(31, 136, 61, 0.08); color: #1a7f37; border: 1px solid rgba(31, 136, 61, 0.3); border-radius: 10px; padding: 10px 14px; font-size: 12px;";
}
} // namespace ConnectUi
