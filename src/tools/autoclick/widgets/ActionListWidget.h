#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QPushButton>
#include "../model/Action.h"
#include <set>
#include <vector>

class ActionListWidget : public QWidget
{
    Q_OBJECT

public:
    explicit ActionListWidget(QWidget* parent = nullptr);

    void setActions(const std::vector<Action>& actions);
    int selectedActionIndex() const;
    void setSelectedActionIndex(int index);
    /// Đánh dấu (hoặc bỏ đánh dấu) MỘT dòng đang có thay đổi CHƯA LƯU bằng dấu "*" ở cột số thứ tự - gọi
    /// khi ActionEditorWidget phát hiện người dùng sửa một trường mà chưa bấm "Lưu hành động". Có thể có
    /// NHIỀU hàng cùng dirty một lúc (AutoClickWindow cache riêng từng hành động đang sửa dở khi đổi hàng
    /// xem, xem m_pendingEdits) - không thay thế dấu của các hàng khác.
    void setRowDirty(int row, bool dirty);
    /// Thay TOÀN BỘ tập hàng đang dirty bằng `rows` (xóa dấu các hàng không còn trong tập, thêm dấu cho
    /// hàng mới) - dùng khi AutoClickWindow đồng bộ lại cả cache (áp dụng/hủy toàn bộ, đổi chuỗi...).
    void setDirtyRows(const std::set<int>& rows);
    /// Đánh dấu hàng đang được ActionRunner thực thi bằng "▶" ở cột số thứ tự (-1 = không hàng nào).
    /// CHỈ đổi cách hiển thị, KHÔNG đổi hàng đang chọn: đổi hàng chọn sẽ nạp lại ActionEditorWidget và
    /// xóa mất các chỉnh sửa người dùng chưa bấm "Lưu hành động".
    void setRunningRow(int row);

signals:
    void actionSelectionChanged(int index);
    void addActionClicked();
    void cloneActionClicked(int index);
    void deleteActionClicked(int index);
    /// Người dùng kéo-thả hàng từ vị trí `from` sang `to` (đã là chỉ số SAU khi kéo - nơi nhận tự làm
    /// phép hoán đổi/di chuyển trong dữ liệu gốc rồi gọi lại setActions()).
    void actionMoved(int from, int to);

private slots:
    void onCustomContextMenuRequested(const QPoint& pos);

private:
    QTableWidget* m_table;
    QPushButton* m_addButton;
    void refreshIndexCell(int row);

    std::set<int> m_dirtyRows;
    int m_runningRow{-1};
};
