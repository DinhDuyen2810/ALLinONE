#include "DurationInput.h"

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
    m_numberSpin->setMinimum(0.0);
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
    );
    layout->addWidget(m_unitCombo);

    rebuildDisplay();

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

void DurationInput::rebuildDisplay()
{
    m_updatingDisplay = true;
    const Unit unit = static_cast<Unit>(m_unitCombo->currentIndex());
    const double factor = unitFactorMs(unit);
    // ms: số nguyên, giữ đúng độ chính xác cũ. giây: tới 1ms (3 chữ số thập phân). phút/giờ: 2 chữ số -
    // sai số làm tròn khi gõ phần lẻ không chia hết cho ms là chấp nhận được, không phải lỗi.
    const int decimals = (unit == Unit::Milliseconds) ? 0 : (unit == Unit::Seconds) ? 3 : 2;
    m_numberSpin->setDecimals(decimals);
    m_numberSpin->setMaximum(m_maxMs / factor);
    m_numberSpin->setValue(m_valueMs / factor);
    m_updatingDisplay = false;
}

void DurationInput::onUnitChanged(int /*index*/)
{
    // Chỉ đổi CÁCH HIỂN THỊ số theo đơn vị mới - giá trị mili-giây thật không đổi, không phát valueChanged.
    rebuildDisplay();
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
    {
        // Luôn đưa đơn vị hiển thị về "ms" khi set bằng code - đảm bảo round-trip setValueMs(x)/valueMs()
        // tuyệt đối chính xác, không phụ thuộc đơn vị người dùng từng chọn trước đó (vd lúc
        // ActionEditorWidget::setAction() nạp lại giá trị của một hành động khác trong danh sách).
        const QSignalBlocker blocker(m_unitCombo);
        m_unitCombo->setCurrentIndex(static_cast<int>(Unit::Milliseconds));
    }
    rebuildDisplay();
    if (changed)
        emit valueChanged(m_valueMs);
}

void DurationInput::setMaximumMs(int maxMs)
{
    m_maxMs = maxMs;
    if (m_valueMs > m_maxMs)
    {
        m_valueMs = m_maxMs;
        rebuildDisplay();
        emit valueChanged(m_valueMs);
        return;
    }
    rebuildDisplay();
}
