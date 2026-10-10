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
/// Giá trị THẬT luôn lưu nội bộ theo mili-giây nguyên (`m_valueMs`) - đổi đơn vị trong combo chỉ đổi CÁCH
/// HIỂN THỊ số, không đổi giá trị thật và KHÔNG phát valueChanged (chưa có gì thực sự thay đổi). Chỉ khi
/// người dùng gõ số mới mới tính lại mili-giây và phát signal.
class DurationInput : public QWidget
{
    Q_OBJECT

public:
    explicit DurationInput(QWidget* parent = nullptr);

    int valueMs() const { return m_valueMs; }
    void setValueMs(int ms);

    /// Áp cho MỌI đơn vị (trần hiển thị = max / hệ số đơn vị hiện tại) - mặc định 3.600.000ms (1 giờ),
    /// giữ đúng trần cũ của QSpinBox trước đây, không lặng lẽ nới rộng.
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
    void rebuildDisplay();              // hiện lại số theo đơn vị đang chọn - KHÔNG phát valueChanged
    void onNumberEdited(double displayValue);
    void onUnitChanged(int index);

    QDoubleSpinBox* m_numberSpin;
    QComboBox* m_unitCombo;
    int m_valueMs{0};
    int m_maxMs{3600000};
    bool m_updatingDisplay{false}; // chặn đệ quy khi tự set lại hiển thị (rebuildDisplay gọi setValue())
};
