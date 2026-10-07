#pragma once

#include "model/MediaLink.h"

#include <QList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class FileDownloader;
class PageMediaScanner;

/// Tab "Quét trang web": dán URL một TRANG web (không phải link tệp trực tiếp) - tải mã nguồn HTML
/// trang đó và tìm các liên kết ảnh/video/âm thanh/tài liệu có trong trang, hiện danh sách có thể tick
/// chọn để tải về (dùng chung FileDownloader/hàng đợi với tab "Tải trực tiếp"). Xem PageMediaScanner.h
/// để biết giới hạn thật (không chạy JavaScript - có thể bỏ sót nội dung tải động/lazy-load).
class PageScanTab : public QWidget
{
    Q_OBJECT

public:
    explicit PageScanTab(FileDownloader* downloader, QWidget* parent = nullptr);

private slots:
    void onScanClicked();
    void onScanResult(QList<MediaLink> links);
    void onScanError(QString message);
    void onSelectAllClicked();
    void onSelectNoneClicked();
    void onChooseFolderClicked();
    void onDownloadSelectedClicked();

private:
    void buildUi();
    QString suggestedDestPath(const MediaLink& link) const;

    FileDownloader* m_downloader{nullptr};
    PageMediaScanner* m_scanner{nullptr};

    QLineEdit* m_urlEdit{nullptr};
    QPushButton* m_scanBtn{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_folderLabel{nullptr};
    QPushButton* m_chooseFolderBtn{nullptr};
    QTableWidget* m_table{nullptr};
    QPushButton* m_selectAllBtn{nullptr};
    QPushButton* m_selectNoneBtn{nullptr};
    QPushButton* m_downloadSelectedBtn{nullptr};

    QList<MediaLink> m_links;
    QString m_saveFolder;
};
