#pragma once

#include <QString>

class QApplication;

/// Giao diện SÁNG dùng chung cho cả ứng dụng: kiểu Fusion (vẽ giống nhau ở mọi máy, QSS tác động được tới thanh cuộn,
/// tab, ô tích, bảng... thay vì kiểu gốc Windows vuông vức và không có hiệu ứng), bảng màu sáng đồng bộ với các
/// tệp `*UiStyle.h` của từng công cụ, stylesheet toàn ứng dụng (bo góc, hover/nhấn/focus/vô hiệu cho nút, tab, ô nhập,
/// ô tích, bảng, thanh cuộn...) và con trỏ NGÓN TAY TRỎ cho mọi thứ bấm được.
///
/// Stylesheet của từng widget (các `*UiStyle.h`, MainWindow...) vẫn được ưu tiên hơn - theme này chỉ lấp phần chưa có
/// kiểu riêng nên không làm đổi những màn hình đã chỉnh tay. Nút nhấn mạnh/nguy hiểm dùng thuộc tính động:
/// `button->setProperty("variant", "primary")` hoặc `"danger"`.
namespace AppTheme
{
/// Gọi MỘT lần ngay sau khi dựng QApplication, trước khi tạo cửa sổ.
void apply(QApplication& app);

/// Stylesheet toàn ứng dụng (để kiểm tra/chụp ảnh giao diện).
QString styleSheet();
} // namespace AppTheme
