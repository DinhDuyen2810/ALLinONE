#pragma once

#include <QString>

/// Style dùng chung cho module Android Phone Control (đồng bộ Light Theme chung của ứng dụng).
/// Cùng mẫu src/tools/diskcleanup/DiskUiStyle.h, src/tools/connect/ConnectUiStyle.h, src/tools/wifi/WifiUiStyle.h.
namespace AndroidUi
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
    return "QPushButton { background-color: #cf222e; color: white; border: none; border-radius: 8px; padding: 8px 16px; font-size: 12px; font-weight: bold; }"
           "QPushButton:hover { background-color: #a40e26; }"
           "QPushButton:pressed { background-color: #82071e; }"
           "QPushButton:disabled { background-color: #eaeef2; color: #8c959f; }";
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

inline QString stateColor(const QString& state)
{
    if (state == "device") return "#1a7f37";        // sẵn sàng
    if (state == "unauthorized") return "#9a6700";   // chờ cho phép trên điện thoại
    return "#cf222e";                                // offline / no permissions / khác
}

inline QString bannerStyle(const QString& kind) // "info" | "warn" | "danger"
{
    if (kind == "warn")
        return "background-color: rgba(154, 103, 0, 0.08); color: #9a6700; border: 1px solid rgba(154, 103, 0, 0.3); border-radius: 10px; padding: 10px 14px; font-size: 12px;";
    if (kind == "danger")
        return "background-color: rgba(207, 34, 46, 0.08); color: #cf222e; border: 1px solid rgba(207, 34, 46, 0.3); border-radius: 10px; padding: 10px 14px; font-size: 12px; font-weight: bold;";
    return "background-color: rgba(9, 105, 218, 0.08); color: #0969da; border: 1px solid rgba(9, 105, 218, 0.3); border-radius: 10px; padding: 10px 14px; font-size: 12px;";
}
} // namespace AndroidUi
