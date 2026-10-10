#pragma once

#include <QLayout>
#include <QRect>
#include <QStyle>
#include <QVector>

/// Layout tự xuống dòng khi hết chỗ ngang, thay vì để Qt cắt mất widget con ngoài tầm nhìn (QHBoxLayout)
/// hay buộc người dùng cuộn ngang thủ công chỉ để xem hết một dãy thẻ/nút. Dùng cho các dãy phần tử có
/// SỐ LƯỢNG THAY ĐỔI THEO DỮ LIỆU THẬT (vd thẻ tổng quan từng ổ đĩa trong Disk Cleanup - máy có ổ đĩa ảo
/// Google Drive mount thêm thì số thẻ tăng theo) hoặc dãy nút cố định nhưng nhiều hơn mức vừa một hàng ở
/// kích thước cửa sổ thường dùng (vd 7 nút của WiFi NetworksTab). Phỏng theo mẫu "Flow Layout" chính thức
/// của Qt (Qt Examples/Widgets/Layouts/FlowLayout) - đã qua kiểm chứng rộng rãi.
class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget* parent, int margin = 0, int hSpacing = -1, int vSpacing = -1);
    explicit FlowLayout(int margin = 0, int hSpacing = -1, int vSpacing = -1);
    ~FlowLayout() override;

    void addItem(QLayoutItem* item) override;
    int horizontalSpacing() const;
    int verticalSpacing() const;
    Qt::Orientations expandingDirections() const override;
    bool hasHeightForWidth() const override;
    int heightForWidth(int width) const override;
    int count() const override;
    QLayoutItem* itemAt(int index) const override;
    QSize minimumSize() const override;
    void setGeometry(const QRect& rect) override;
    QSize sizeHint() const override;
    QLayoutItem* takeAt(int index) override;

private:
    int doLayout(const QRect& rect, bool testOnly) const;
    int smartSpacing(QStyle::PixelMetric pm) const;

    QList<QLayoutItem*> m_itemList;
    int m_hSpace;
    int m_vSpace;
};
