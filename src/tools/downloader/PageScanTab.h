#pragma once

#include "model/MediaLink.h"
#include "model/ResolvedMedia.h"

#include <QList>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QTableWidget;
class DownloadQueue;
class DownloaderSettings;
class LinkResolver;
class PageMediaScanner;

/// Tab "Quét trang web": dán URL một TRANG, một HỒ SƠ (Facebook/TikTok/Instagram/YouTube...) hoặc một BÀI ĐĂNG - liệt kê mọi
/// ảnh/video/âm thanh/tài liệu tìm được để tick chọn rồi "Thêm vào hàng đợi" hoặc "Tải ngay".
///  1. Quét HTML TĨNH (nhanh): ảnh/video/tài liệu có sẵn trong mã nguồn.
///  2. Quét SÂU bằng bộ phân giải đầy đủ (LinkResolver) khi chưa thấy video/tài liệu nào, hoặc khi là hồ sơ/bài đăng: yt-dlp,
///     chuỗi trong script, khung nhúng, trình duyệt ẩn (trang chặn chuột phải/sao chép link vẫn đọc được) và - với hồ sơ -
///     QUÉT TỪNG BÀI ĐĂNG để lấy ảnh/video của mỗi bài. Có nút "Dừng".
/// Mỗi dòng có nút 🌐 mở link trên trình duyệt. Tải xuống đi qua hàng đợi chung của tab "Tải trực tiếp".
class PageScanTab : public QWidget
{
    Q_OBJECT

public:
    PageScanTab(DownloadQueue* queue, DownloaderSettings* settings, QWidget* parent = nullptr);
    ~PageScanTab() override;

    /// Dừng lượt quét sâu đang chạy (yt-dlp/trình duyệt ẩn) - gọi khi đóng cửa sổ.
    void cancelScan();

private slots:
    void onScanClicked();
    void onDeepClicked();
    void onScanResult(QList<MediaLink> links);
    void onScanError(QString message);
    void onDeepProgress(QString message);
    void onDeepFinished(ResolveResult result);
    void onSelectAllClicked();
    void onSelectNoneClicked();
    void onAddSelectedClicked();
    void onDownloadNowClicked();

private:
    void buildUi();
    void refreshFolderLabel();
    void beginScan(bool forceDeep);
    void startDeep();
    void endScan();
    void appendRows(const QList<ResolvedMedia>& rows);
    void updateStatus(const QString& extra = QString());
    /// Thêm các dòng đang tick vào hàng đợi; trả danh sách ID (có thể rỗng). `includeExisting`: mục đã có sẵn trong hàng
    /// đợi cũng được trả về (dùng cho "Tải ngay").
    QList<int> addCheckedToQueue(bool includeExisting);

    DownloadQueue* m_queue{nullptr};
    DownloaderSettings* m_settings{nullptr};
    PageMediaScanner* m_scanner{nullptr};
    LinkResolver* m_resolver{nullptr};

    QLineEdit* m_urlEdit{nullptr};
    QPushButton* m_scanBtn{nullptr};
    QPushButton* m_deepBtn{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_folderLabel{nullptr};
    QTableWidget* m_table{nullptr};
    QPushButton* m_selectAllBtn{nullptr};
    QPushButton* m_selectNoneBtn{nullptr};
    QPushButton* m_addSelectedBtn{nullptr};
    QPushButton* m_downloadNowBtn{nullptr};

    QList<ResolvedMedia> m_rows; ///< Song song với các dòng của bảng
    QString m_pageUrl;           ///< Trang đang quét (Referer cho các tệp quét tĩnh)
    bool m_busy{false};
    bool m_forceDeep{false};
    bool m_deepRunning{false};
    bool m_stopped{false};
    int m_staticCount{0};
    QString m_staticError;
    QString m_deepProgress;
    QString m_deepError;
    QString m_deepWarning;
};
