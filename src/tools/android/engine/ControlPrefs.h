#pragma once

#include <QJsonObject>

/// Hai tùy chọn về MÀN HÌNH ĐIỆN THOẠI của phiên điều khiển, được nhớ giữa các lần mở ứng dụng (tệp
/// `android_control_prefs.json` trong thư mục dữ liệu người dùng qua AppPaths). Lõi thuần - không widget.
struct ControlPrefs
{
    bool stayAwake{true};      // --stay-awake: không để điện thoại tự khóa màn hình khi đang cắm USB
    bool turnScreenOff{false}; // --turn-screen-off: tắt màn hình vật lý lúc điều khiển (hình trên máy tính vẫn sáng)

    QJsonObject toJson() const;
    /// Thiếu khóa / sai kiểu (tệp bị sửa tay hoặc hỏng) -> giữ giá trị mặc định của khóa đó.
    static ControlPrefs fromJson(const QJsonObject& o);

    /// Đọc từ đĩa; chưa có tệp hoặc tệp hỏng -> mặc định.
    static ControlPrefs load();
    /// Ghi xuống đĩa (QSaveFile - không để lại tệp dở dang); trả false nếu không ghi được.
    bool save() const;
};
