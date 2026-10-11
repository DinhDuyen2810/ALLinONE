#pragma once

#include <QWidget>

class QDoubleSpinBox;
class QComboBox;

/// Ô nhập thời gian dùng chung: 1 số (không nút mũi tên tăng/giảm) + 1 combo chọn đơn vị
/// (ms/giây/phút/giờ) - trước đây Auto Click dùng QSpinBox hiển thị thẳng mili-giây thô kèm nút mũi tên,
/// khó đọc với khoảng thời gian dài (vd "45000 ms" thay vì "45 giây") và nút mũi tên trông không hiện đại
/// so với phần còn lại ứng dụng (yêu cầu người dùng, v1.19.7). Đặt trong `src/ui/widgets/` để dùng lại
/// được cho module khác sau này, cùng chỗ với `FlowLayout`.
///
/// Đổi đơn vị trong combo KHÔNG BAO GIỜ quy đổi/viết lại số đang hiện trong ô nhập (yêu cầu người dùng,
/// v1.19.8 - trước đó đổi đơn vị từng tính lại số hiển thị theo mili-giây đã lưu, vd "500" ms thành "0.5"
/// khi chọn "giây", bị phản hồi là quy đổi không mong muốn). Số trong ô nhập giữ nguyên tuyệt đối; chỉ Ý
/// NGHĨA của nó đổi theo đơn vị mới (mili-giây thật = số hiện tại × hệ số đơn vị). `setValueMs()` (nạp từ
/// code, vd nạp lại một hành động khác) là ngoại lệ DUY NHẤT: luôn đưa đơn vị về "ms" + hiện đúng số
/// mili-giây, vì đó là lúc TẢI dữ liệu từ model chứ không phải người dùng tự chọn đơn vị.
class DurationInput : public QWidget
{
    Q_OBJECT

public:
    explicit DurationInput(QWidget* parent = nullptr);

    int valueMs() const { return m_valueMs; }
    void setValueMs(int ms);

    /// Trần số mili-giây thật (giá trị emit/lưu luôn bị kẹp trong [0, maxMs]) - mặc định 3.600.000ms (1
    /// giờ). Cố định, không đổi theo đơn vị đang chọn (xem comment lớp): đổi theo đơn vị sẽ lại khiến
    /// Qt tự kẹp/làm tròn số đang gõ mỗi khi chọn đơn vị khác - đúng kiểu quy đổi ẩn mà yêu cầu người
    /// dùng muốn loại bỏ.
    void setMaximumMs(int maxMs);

signals:
    void valueChanged(int ms);

private:
    enum class Unit
    {
        Milliseconds = 0,
        Seconds = 1,
        Minutes = 2,
        Hours = 3,
    };

    static double unitFactorMs(Unit u); // số mili-giây trên 1 đơn vị
    void onNumberEdited(double displayValue);
    void onUnitChanged(int index);

    QDoubleSpinBox* m_numberSpin;
    QComboBox* m_unitCombo;
    int m_valueMs{0};
    int m_maxMs{3600000};
    bool m_updatingDisplay{false}; // chặn đệ quy khi setValueMs() tự set lại số/đơn vị hiển thị
};
