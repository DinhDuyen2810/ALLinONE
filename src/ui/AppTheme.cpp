#include "AppTheme.h"

#include "ui/widgets/ComboBoxStyle.h"

#include <QAbstractButton>
#include <QApplication>
#include <QComboBox>
#include <QEvent>
#include <QPalette>
#include <QStyleFactory>
#include <QTabBar>

namespace
{
/// Con trỏ ngón tay trỏ cho mọi thứ BẤM ĐƯỢC (nút, ô tích, nút radio, tab, combo) - gán khi widget được "polish" lần đầu
/// và cập nhật khi bật/tắt: nút bị vô hiệu thì trả về mũi tên (không còn gì để bấm).
class PointerCursorFilter : public QObject
{
public:
    explicit PointerCursorFilter(QObject* parent) : QObject(parent) {}

protected:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if (event->type() == QEvent::Polish || event->type() == QEvent::EnabledChange)
        {
            auto* w = qobject_cast<QWidget*>(obj);
            if (w && isClickable(w))
            {
                if (w->isEnabled())
                    w->setCursor(Qt::PointingHandCursor);
                else
                    w->unsetCursor();
            }
        }
        return QObject::eventFilter(obj, event);
    }

private:
    static bool isClickable(const QWidget* w)
    {
        return qobject_cast<const QAbstractButton*>(w) || qobject_cast<const QTabBar*>(w) || qobject_cast<const QComboBox*>(w);
    }
};

QPalette lightPalette()
{
    QPalette p;
    const QColor window(0xf6, 0xf8, 0xfa), text(0x1f, 0x23, 0x28), muted(0x8c, 0x95, 0x9f), primary(0x09, 0x69, 0xda);
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, Qt::white);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, primary);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, primary);
    p.setColor(QPalette::ToolTipBase, text);
    p.setColor(QPalette::ToolTipText, Qt::white);
    p.setColor(QPalette::PlaceholderText, muted);
    p.setColor(QPalette::Disabled, QPalette::WindowText, muted);
    p.setColor(QPalette::Disabled, QPalette::Text, muted);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, muted);
    return p;
}
} // namespace

QString AppTheme::styleSheet()
{
    // Bảng màu (đồng bộ các *UiStyle.h): chữ #1f2328, chữ phụ #57606a, mờ #8c959f, viền #d0d7de, nền #f6f8fa,
    // nhấn #0969da (đậm hơn khi rê/nhấn #0854b0/#053d82), nguy hiểm #cf222e, thành công #1f883d.
    return QStringLiteral(R"QSS(
/* ---- Hộp thoại, chú giải ---- */
QDialog, QMessageBox, QInputDialog { background-color: #f6f8fa; }
QMessageBox QLabel, QInputDialog QLabel { color: #1f2328; }
QToolTip { background-color: #1f2328; color: #ffffff; border: none; padding: 5px 9px; }

/* ---- Nút bấm: rê chuột đổi màu + viền xanh, nhấn tối hơn, focus có viền, vô hiệu mờ ---- */
QPushButton, QToolButton { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 14px; }
QToolButton { padding: 4px 8px; }
QPushButton:hover, QToolButton:hover { background-color: #f1f6fd; color: #0969da; border-color: #0969da; }
QPushButton:pressed, QToolButton:pressed { background-color: #dbe9fb; color: #0854b0; border-color: #0854b0; }
QPushButton:focus { border-color: #0969da; }
QPushButton:checked, QToolButton:checked { background-color: #ddf4ff; color: #0969da; border-color: #0969da; }
QPushButton:disabled, QToolButton:disabled { background-color: #eaeef2; color: #8c959f; border-color: #d0d7de; }
QPushButton:default, QPushButton[variant="primary"] { background-color: #0969da; color: #ffffff; border: 1px solid #0969da; font-weight: bold; }
QPushButton:default:hover, QPushButton[variant="primary"]:hover { background-color: #0854b0; color: #ffffff; border-color: #0854b0; }
QPushButton:default:pressed, QPushButton[variant="primary"]:pressed { background-color: #053d82; color: #ffffff; border-color: #053d82; }
QPushButton:default:disabled, QPushButton[variant="primary"]:disabled { background-color: #eaeef2; color: #8c959f; border-color: #d0d7de; }
QPushButton[variant="danger"] { background-color: #cf222e; color: #ffffff; border: 1px solid #cf222e; font-weight: bold; }
QPushButton[variant="danger"]:hover { background-color: #a40e26; color: #ffffff; border-color: #a40e26; }
QPushButton[variant="danger"]:pressed { background-color: #82071e; color: #ffffff; border-color: #82071e; }
QPushButton[variant="danger"]:disabled { background-color: #eaeef2; color: #8c959f; border-color: #d0d7de; }
QDialogButtonBox QPushButton { min-width: 76px; }

/* ---- Tab: dạng viên thuốc bo tròn, rê chuột sáng lên ---- */
QTabWidget::pane { border: none; }
QTabBar::tab { background: #eaeef2; color: #57606a; padding: 9px 22px; margin-right: 4px; border-top-left-radius: 10px; border-top-right-radius: 10px; }
QTabBar::tab:selected { background: #ffffff; color: #0969da; }
QTabBar::tab:hover:!selected { background: #dde3ea; color: #1f2328; }
QTabBar::tab:disabled { color: #8c959f; }

/* ---- Ô nhập: viền đậm lên khi rê, xanh khi focus ---- */
QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QDateTimeEdit, QComboBox {
    background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 5px 8px;
    selection-background-color: #0969da; selection-color: #ffffff; }
QLineEdit:hover, QTextEdit:hover, QPlainTextEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QDateTimeEdit:hover, QComboBox:hover { border-color: #8c959f; }
QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QDateTimeEdit:focus, QComboBox:focus { border-color: #0969da; }
QLineEdit:disabled, QTextEdit:disabled, QPlainTextEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QDateTimeEdit:disabled, QComboBox:disabled {
    background-color: #eaeef2; color: #8c959f; }
QComboBox QAbstractItemView { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 4px; outline: none;
    selection-background-color: #0969da; selection-color: #ffffff; }
/* Nút lên/xuống của ô số luôn ẩn trên toàn ứng dụng (yêu cầu người dùng, v1.19.8/v1.19.10) - nhập bằng bàn phím/cuộn chuột */
QSpinBox::up-button, QDoubleSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::down-button { width: 0px; border: none; }
QSpinBox::up-arrow, QDoubleSpinBox::up-arrow, QSpinBox::down-arrow, QDoubleSpinBox::down-arrow { image: none; width: 0px; height: 0px; }
)QSS")
    + ComboBoxStyle::noArrow() // mũi tên xổ xuống luôn ẩn trên toàn ứng dụng (yêu cầu người dùng, v1.19.8)
    + QStringLiteral(R"QSS(

/* ---- Ô tích / radio: bo tròn, rê chuột đổi viền, chọn thì đổ màu xanh có dấu ---- */
QCheckBox, QRadioButton { spacing: 8px; color: #1f2328; }
QCheckBox:hover, QRadioButton:hover { color: #0969da; }
QCheckBox:disabled, QRadioButton:disabled { color: #8c959f; }
QCheckBox::indicator, QAbstractItemView::indicator { width: 16px; height: 16px; border: 1.5px solid #8c959f; border-radius: 5px; background: #ffffff; }
QCheckBox::indicator:hover, QAbstractItemView::indicator:hover { border-color: #0969da; background: #eef4fc; }
QCheckBox::indicator:checked, QAbstractItemView::indicator:checked { background-color: #0969da; border-color: #0969da; image: url(:/ui/check.svg); }
QCheckBox::indicator:indeterminate, QAbstractItemView::indicator:indeterminate { background-color: #0969da; border-color: #0969da; image: url(:/ui/dash.svg); }
QCheckBox::indicator:disabled, QAbstractItemView::indicator:disabled { background: #eaeef2; border-color: #d0d7de; }
QRadioButton::indicator { width: 16px; height: 16px; border: 1.5px solid #8c959f; border-radius: 9px; background: #ffffff; }
QRadioButton::indicator:hover { border-color: #0969da; background: #eef4fc; }
QRadioButton::indicator:checked { background-color: #0969da; border-color: #0969da; image: url(:/ui/radio-dot.svg); }
QRadioButton::indicator:disabled { background: #eaeef2; border-color: #d0d7de; }

/* ---- Danh sách / bảng / cây: bo góc, dòng sáng lên khi rê, tiêu đề phẳng ---- */
QAbstractItemView { background-color: #ffffff; alternate-background-color: #f6f8fa; color: #1f2328; border: 1px solid #d0d7de; border-radius: 10px;
    selection-background-color: #0969da; selection-color: #ffffff; outline: none; }
QTableView, QTreeView { gridline-color: #eaeef2; }
QAbstractItemView::item:hover { background-color: #eef4fc; }
QAbstractItemView::item:selected { background-color: #0969da; color: #ffffff; }
QHeaderView::section { background-color: #f6f8fa; color: #57606a; font-weight: bold; border: none; border-bottom: 1px solid #d0d7de; padding: 6px 8px; }
QHeaderView::section:hover { background-color: #eaeef2; color: #1f2328; }
QHeaderView::section:first:horizontal { border-top-left-radius: 9px; }
QHeaderView::section:last:horizontal { border-top-right-radius: 9px; }
QTableCornerButton::section { background-color: #f6f8fa; border: none; }

/* ---- Thanh cuộn mảnh, bo tròn, đậm lên khi rê ---- */
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: #c3cad2; border-radius: 4px; min-height: 28px; }
QScrollBar::handle:horizontal { background: #c3cad2; border-radius: 4px; min-width: 28px; }
QScrollBar::handle:vertical:hover, QScrollBar::handle:horizontal:hover { background: #9aa4ae; }
QScrollBar::handle:vertical:pressed, QScrollBar::handle:horizontal:pressed { background: #6e7781; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0px; height: 0px; background: none; border: none; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

/* ---- Thanh tiến độ, thanh trượt, nhóm, menu ---- */
QProgressBar { background-color: #e6ebf1; border: none; border-radius: 7px; text-align: center; color: #1f2328; }
QProgressBar::chunk { background-color: #0969da; border-radius: 7px; }
QSlider::groove:horizontal { height: 6px; background: #d8dee4; border-radius: 3px; }
QSlider::sub-page:horizontal { background: #0969da; border-radius: 3px; }
QSlider::handle:horizontal { width: 16px; height: 16px; margin: -5px 0; background: #ffffff; border: 2px solid #0969da; border-radius: 9px; }
QSlider::handle:horizontal:hover { background: #eef4fc; }
QSlider::handle:horizontal:pressed { background: #dbe9fb; }
QGroupBox { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 12px; margin-top: 10px; padding-top: 14px; }
QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #0969da; font-weight: bold; }
QMenu { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; padding: 4px; }
QMenu::item { padding: 7px 28px 7px 14px; border-radius: 6px; }
QMenu::item:selected { background-color: #eef4fc; color: #0969da; }
QMenu::item:disabled { color: #8c959f; }
QMenu::separator { height: 1px; background: #eaeef2; margin: 5px 8px; }
QStatusBar { background-color: #ffffff; color: #57606a; }
)QSS");
}

void AppTheme::apply(QApplication& app)
{
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    app.setPalette(lightPalette());
    app.setStyleSheet(styleSheet());
    app.installEventFilter(new PointerCursorFilter(&app));
}
