#pragma once

#include "model/MediaLink.h"

#include <QList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class DownloadQueue;
class DownloaderSettings;
class PageMediaScanner;

/// Tab "Quét trang web": dán URL một TRANG web (không phải link tệp trực tiếp) - tải mã nguồn HTML trang đó và tìm các
/// liên kết ảnh/video/âm thanh/tài liệu có trong trang, hiện danh sách có thể tick chọn rồi "Thêm vào hàng đợi" (hàng đợi
/// chung của tab "Tải trực tiếp" - KHÔNG tải ngay). Mỗi dòng có nút 🌐 mở link trên trình duyệt. Quét ở đây là phân tích
/// HTML TĨNH (xem PageMediaScanner.h); trang động/chặn chuột phải thì dùng "Quét sâu" - đưa chính trang vào hàng đợi để bộ
/// phân giải (yt-dlp + quét sâu + trình duyệt ẩn) xử lý.
class PageScanTab : public QWidget
{
    Q_OBJECT

public:
    PageScanTab(DownloadQueue* queue, DownloaderSettings* settings, QWidget* parent = nullptr);

private slots:
    void onScanClicked();
    void onScanResult(QList<MediaLink> links);
    void onScanError(QString message);
    void onSelectAllClicked();
    void onSelectNoneClicked();
    void onAddSelectedClicked();
    void onDeepScanClicked();

private:
    void buildUi();
    void refreshFolderLabel();

    DownloadQueue* m_queue{nullptr};
    DownloaderSettings* m_settings{nullptr};
    PageMediaScanner* m_scanner{nullptr};

    QLineEdit* m_urlEdit{nullptr};
    QPushButton* m_scanBtn{nullptr};
    QPushButton* m_deepBtn{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_folderLabel{nullptr};
    QTableWidget* m_table{nullptr};
    QPushButton* m_selectAllBtn{nullptr};
    QPushButton* m_selectNoneBtn{nullptr};
    QPushButton* m_addSelectedBtn{nullptr};

    QList<MediaLink> m_links;
    QString m_pageUrl; ///< Trang đã quét - gửi làm Referer khi tải các tệp tìm được
};
