#include "DurationInput.h"
#include "ComboBoxStyle.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QSignalBlocker>

DurationInput::DurationInput(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_numberSpin = new QDoubleSpinBox(this);
    m_numberSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    // decimals/maximum CỐ ĐỊNH, không đổi theo đơn vị đang chọn (xem comment DurationInput.h) - đổi đơn
    // vị không bao giờ được viết lại số đang hiện, kể cả gián tiếp qua Qt tự kẹp/làm tròn khi setDecimals/
    // setMaximum đổi theo đơn vị.
    m_numberSpin->setDecimals(0);
    m_numberSpin->setMinimum(0.0);
    m_numberSpin->setMaximum(m_maxMs);
    m_numberSpin->setStyleSheet(
        "QDoubleSpinBox { background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 4px 8px; font-size: 12px; }"
        "QDoubleSpinBox:hover, QDoubleSpinBox:focus { border-color: #0969da; }"
    );
    layout->addWidget(m_numberSpin, 1);

    m_unitCombo = new QComboBox(this);
    m_unitCombo->addItem("ms", static_cast<int>(Unit::Milliseconds));
    m_unitCombo->addItem("giây", static_cast<int>(Unit::Seconds));
    m_unitCombo->addItem("phút", static_cast<int>(Unit::Minutes));
    m_unitCombo->addItem("giờ", static_cast<int>(Unit::Hours));
    m_unitCombo->setStyleSheet(
        "QComboBox { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 4px 8px; font-size: 12px; }"
        "QComboBox:hover { border-color: #0969da; }"
        + ComboBoxStyle::noArrow()
    );
    layout->addWidget(m_unitCombo);

    connect(m_numberSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &DurationInput::onNumberEdited);
    connect(m_unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DurationInput::onUnitChanged);
}

double DurationInput::unitFactorMs(Unit u)
{
    switch (u)
    {
        case Unit::Milliseconds: return 1.0;
        case Unit::Seconds:      return 1000.0;
        case Unit::Minutes:      return 60000.0;
        case Unit::Hours:        return 3600000.0;
    }
    return 1.0;
}

void DurationInput::onUnitChanged(int index)
{
    if (m_updatingDisplay) // setValueMs() đang tự đặt lại đơn vị về ms - không phải người dùng tự chọn
        return;
    // Theo yêu cầu người dùng (v1.19.8): đổi đơn vị KHÔNG được quy đổi/viết lại số đang hiện - số giữ
    // nguyên y hệt, chỉ đổi Ý NGHĨA của nó (vd "500" đang là 500ms, chọn "giây" thì vẫn hiện "500" nhưng
    // giờ là 500 giây). Mili-giây thật tính lại = số hiện tại × hệ số đơn vị mới.
    const Unit unit = static_cast<Unit>(index);
    const int newMs = qBound(0, static_cast<int>(qRound(m_numberSpin->value() * unitFactorMs(unit))), m_maxMs);
    if (newMs != m_valueMs)
    {
        m_valueMs = newMs;
        emit valueChanged(m_valueMs);
    }
}

void DurationInput::onNumberEdited(double displayValue)
{
    if (m_updatingDisplay)
        return;
    const Unit unit = static_cast<Unit>(m_unitCombo->currentIndex());
    const int newMs = qBound(0, static_cast<int>(qRound(displayValue * unitFactorMs(unit))), m_maxMs);
    if (newMs == m_valueMs)
        return;
    m_valueMs = newMs;
    emit valueChanged(m_valueMs);
}

void DurationInput::setValueMs(int ms)
{
    ms = qBound(0, ms, m_maxMs);
    const bool changed = (ms != m_valueMs);
    m_valueMs = ms;
    // Nạp giá trị từ code (vd ActionEditorWidget::setAction() nạp lại hành động khác) - LUÔN đưa đơn vị
    // hiển thị về "ms" + hiện đúng số mili-giây nguyên, đảm bảo round-trip setValueMs(x)/valueMs() tuyệt
    // đối chính xác bất kể đơn vị người dùng từng chọn trước đó. Đây là lúc TẢI dữ liệu, không phải người
    // dùng tự chọn đơn vị, nên không vi phạm quy tắc "đổi đơn vị không quy đổi số".
    m_updatingDisplay = true;
    m_unitCombo->setCurrentIndex(static_cast<int>(Unit::Milliseconds));
    m_numberSpin->setValue(ms);
    m_updatingDisplay = false;
    if (changed)
        emit valueChanged(m_valueMs);
}

void DurationInput::setMaximumMs(int maxMs)
{
    m_maxMs = maxMs;
    m_updatingDisplay = true;
    m_numberSpin->setMaximum(m_maxMs);
    m_updatingDisplay = false;
    if (m_valueMs > m_maxMs)
    {
        m_valueMs = m_maxMs;
        // Số đang hiện (ở đơn vị hiện tại) không nhất thiết còn khớp m_valueMs sau khi kẹp - không ép
        // hiện lại (đổi đơn vị/kẹp trần không được viết lại số đang gõ, xem comment lớp), chỉ kẹp giá trị
        // THẬT được lưu/emit.
        emit valueChanged(m_valueMs);
    }
}
