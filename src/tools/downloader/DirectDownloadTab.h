#pragma once

#include <QHash>
#include <QWidget>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class FileDownloader;

/// Tab "Tải trực tiếp": dán một URL TRỰC TIẾP tới tệp (ảnh/video/âm thanh/tài liệu - vd link kết thúc
/// bằng .jpg/.mp4/.pdf..., hoặc bất kỳ URL HTTP(S) nào server trả về một tệp) để tải về, có hàng đợi
/// nhiều tệp, tạm dừng/tiếp tục (nếu server hỗ trợ Range), tốc độ/tiến độ thời gian thực. Cũng là nơi
/// hiển thị HÀNG ĐỢI CHUNG mà tab "Quét trang web" đẩy vào sau khi người dùng chọn các mục tìm thấy.
class DirectDownloadTab : public QWidget
{
    Q_OBJECT

public:
    /// downloader dùng CHUNG với PageScanTab (sở hữu bởi DownloaderWindow) - mọi mục tải, dù thêm từ
    /// tab nào, đều hiện trong MỘT hàng đợi duy nhất ở đây.
    explicit DirectDownloadTab(FileDownloader* downloader, QWidget* parent = nullptr);

public slots:
    /// Cho PageScanTab (và tính năng tự phát hiện clipboard) gọi để thêm thẳng vào hàng đợi.
    void enqueueUrl(const QString& url);

private slots:
    void onAddClicked();
    void onChooseFolderClicked();
    void onStartAllClicked();
    void onTableContextMenu(const QPoint& pos);
    void onItemUpdated(int id);
    void onClipboardChanged();

private:
    void buildUi();
    void addRowForId(int id);
    void refreshRow(int id);
    QString suggestedDestPath(const QString& url) const;

    FileDownloader* m_downloader{nullptr};

    QLineEdit* m_urlEdit{nullptr};
    QPushButton* m_addBtn{nullptr};
    QLabel* m_folderLabel{nullptr};
    QPushButton* m_chooseFolderBtn{nullptr};
    QCheckBox* m_clipboardCheck{nullptr};
    QPushButton* m_startAllBtn{nullptr};
    QTableWidget* m_table{nullptr};

    QString m_saveFolder;
    QHash<int, int> m_idToRow;
    QString m_lastClipboardText;
};
