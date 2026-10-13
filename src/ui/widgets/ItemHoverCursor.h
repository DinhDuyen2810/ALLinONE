#pragma once

#include <QAbstractItemView>
#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QPoint>

/// Con trỏ NGÓN TAY TRỎ khi rê chuột lên MỘT MỤC của danh sách/bảng (QListWidget, QTableWidget...) - các mục
/// là thứ bấm được (chọn công cụ, mở...) nên cần cùng kiểu con trỏ như nút bấm/combo (yêu cầu người dùng).
/// Chỉ đổi khi chuột NẰM TRÊN một mục: vùng trống bên dưới danh sách giữ con trỏ mũi tên, vì ở đó không có gì
/// để bấm. Không dùng viewport->setCursor() cố định vì nó phủ cả vùng trống.
///
/// Dùng: `ItemHoverCursor::install(listWidget);` MỘT lần sau khi dựng danh sách. Đối tượng tự gắn làm con của
/// view nên bị hủy cùng view - không cần giữ con trỏ. Cần mouse tracking (hàm tự bật) để nhận MouseMove khi
/// chưa bấm nút nào.
class ItemHoverCursor : public QObject
{
public:
    static void install(QAbstractItemView* view)
    {
        if (view)
            new ItemHoverCursor(view);
    }

protected:
    bool eventFilter(QObject* obj, QEvent* event) override
    {
        if (obj == m_view->viewport())
        {
            switch (event->type())
            {
                case QEvent::MouseMove:
                    update(static_cast<QMouseEvent*>(event)->position().toPoint());
                    break;
                case QEvent::Leave:
                case QEvent::Hide:
                    m_view->viewport()->unsetCursor();
                    break;
                default:
                    break;
            }
        }
        return QObject::eventFilter(obj, event);
    }

private:
    explicit ItemHoverCursor(QAbstractItemView* view)
        : QObject(view)
        , m_view(view)
    {
        view->setMouseTracking(true);
        view->viewport()->setMouseTracking(true);
        view->viewport()->installEventFilter(this);
    }

    void update(const QPoint& pos)
    {
        QWidget* vp = m_view->viewport();
        if (m_view->indexAt(pos).isValid())
            vp->setCursor(Qt::PointingHandCursor);
        else
            vp->unsetCursor();
    }

    QAbstractItemView* m_view;
};
