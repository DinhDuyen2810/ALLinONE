#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPoint;
class QTableWidget;
class DownloadHistoryStore;

/// Tab "Lịch sử" của Downloader: mọi tệp/video đã tải xong, thất bại hoặc bị hủy (mới nhất đứng đầu), có ô lọc, mở
/// tệp/thư mục/link nguồn, xóa từng mục hoặc xóa toàn bộ. Dữ liệu ở DownloadHistoryStore (lưu bền).
class DownloadHistoryTab : public QWidget
{
    Q_OBJECT

public:
    explicit DownloadHistoryTab(DownloadHistoryStore* store, QWidget* parent = nullptr);

private slots:
    void rebuild();
    void onFilterChanged();
    void onOpenFile();
    void onOpenFolder();
    void onOpenSource();
    void onRemoveSelected();
    void onClearAll();
    void onContextMenu(const QPoint& pos);

private:
    void buildUi();
    /// Chỉ số trong store của dòng đang chọn (-1 nếu không có).
    int currentEntryIndex() const;

    DownloadHistoryStore* m_store{nullptr};
    QLineEdit* m_filterEdit{nullptr};
    QLabel* m_countLabel{nullptr};
    QTableWidget* m_table{nullptr};
};
