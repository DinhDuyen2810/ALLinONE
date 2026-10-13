#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPoint;
class QPushButton;
class QTableWidget;
class DownloadQueue;
class DownloaderSettings;

/// Tab "Tải trực tiếp" - HÀNG ĐỢI TẢI HỢP NHẤT. Quy trình:
///  1. Dán một hoặc nhiều link (mỗi dòng một link) -> "+ Thêm vào hàng đợi": link vào hàng đợi, CHƯA tải gì.
///  2. Mỗi link được kiểm tra tự động và đánh dấu OK (ra được tệp/video để tải) hoặc ERR (kèm lý do). Một link có thể
///     bung thành nhiều dòng (hồ sơ/danh sách phát/nhiều ảnh trong bài đăng).
///  3. Chọn các dòng (ô tick hoặc bôi đen) rồi "Tải các mục đã chọn" - chỉ dòng OK được tải.
/// Mỗi dòng có nút 🌐 mở link của nó trên trình duyệt. Hàng đợi này cũng hiện mọi mục do tab "Quét trang web" đưa vào.
class DirectDownloadTab : public QWidget
{
    Q_OBJECT

public:
    DirectDownloadTab(DownloadQueue* queue, DownloaderSettings* settings, QWidget* parent = nullptr);

public slots:
    /// Thêm các link trong đoạn văn bản vào hàng đợi (CHƯA tải). Trả về qua thanh trạng thái số link đã thêm.
    void enqueueText(const QString& text);

private slots:
    void onAddClicked();
    void onItemAdded(int id);
    void onItemUpdated(int id);
    void onItemRemoved(int id);
    void onMessage(const QString& text);
    void onSelectAll();
    void onSelectNone();
    void onSelectOk();
    void onDownloadSelected();
    void onPauseSelected();
    void onCancelSelected();
    void onRemoveSelected();
    void onRemoveFinished();
    void onChooseFolderClicked();
    void onOpenFolderClicked();
    void onTableContextMenu(const QPoint& pos);
    void onClipboardChanged();
    void onSettingsChanged();

private:
    void buildUi();
    int rowOfId(int id) const;
    int idOfRow(int row) const;
    void fillRow(int row, int id);
    void refreshSummary();
    /// Mục tác động: các dòng được TICK; không có dòng nào tick thì các dòng đang bôi đen.
    QList<int> targetIds() const;
    void openLinkOfId(int id);

    DownloadQueue* m_queue{nullptr};
    DownloaderSettings* m_settings{nullptr};

    QPlainTextEdit* m_urlEdit{nullptr};
    QPushButton* m_addBtn{nullptr};
    QLabel* m_folderLabel{nullptr};
    QPushButton* m_chooseFolderBtn{nullptr};
    QPushButton* m_openFolderBtn{nullptr};
    QCheckBox* m_clipboardCheck{nullptr};
    QLabel* m_summaryLabel{nullptr};
    QLabel* m_messageLabel{nullptr};
    QPushButton* m_downloadBtn{nullptr};
    QTableWidget* m_table{nullptr};

    QString m_lastClipboardText;
    bool m_updatingTable{false};
};
