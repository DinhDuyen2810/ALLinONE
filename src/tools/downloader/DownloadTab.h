#pragma once

#include "model/MediaLink.h"
#include "model/ResolvedMedia.h"
#include "model/VideoInfo.h"

#include <QList>
#include <QStringList>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPoint;
class QPushButton;
class QTableWidget;
class DownloadQueue;
class DownloaderSettings;
class LinkResolver;
class PageMediaScanner;
class YtDlpInfoWorker;

/// Tab "Tải xuống" - HÀNG ĐỢI TẢI HỢP NHẤT, gộp ba tab cũ (Tải trực tiếp, Quét trang web, Video nền tảng). Quy trình:
///  1. Dán một hoặc nhiều link (mỗi dòng một link) rồi bấm MỘT nút "Quét": mỗi link được quét lần lượt - HTML tĩnh trước, rồi
///     quét sâu bằng bộ phân giải đầy đủ (tệp trực tiếp, Google Drive/Docs, yt-dlp, mã nguồn, trình duyệt ẩn) khi cần - và mọi
///     ảnh/video/âm thanh/tài liệu tìm được vào hàng đợi dưới dạng dòng OK (CHƯA tải gì). Link không ra tệp nào thành dòng ERR
///     kèm lý do. Một link có thể bung thành nhiều dòng (hồ sơ/danh sách phát/nhiều ảnh). Nút thành "Dừng quét" khi đang chạy.
///  2. Chọn các dòng (ô tick hoặc bôi đen) rồi "Tải các mục đã chọn" - chỉ dòng OK được tải.
/// Video nền tảng (yt-dlp): "Chất lượng video" áp dụng chung; bấm phải vào dòng video -> "Chọn chất lượng khác..." để chọn
/// riêng một định dạng cụ thể của video đó. Mỗi dòng có cột ↗ mở link của nó trên trình duyệt.
class DownloadTab : public QWidget
{
    Q_OBJECT

public:
    DownloadTab(DownloadQueue* queue, DownloaderSettings* settings, QWidget* parent = nullptr);
    ~DownloadTab() override;

    /// Dừng NGAY lượt quét trang đang chạy (yt-dlp/trình duyệt ẩn) và lượt lấy danh sách chất lượng (yt-dlp) - gọi khi đóng
    /// cửa sổ/thoát ứng dụng, nếu không các tiến trình ngoài này mồ côi chạy ngầm (xem core/WinProcessTree.h). Tiến trình do
    /// HÀNG ĐỢI sinh ra do DownloadQueue::stopProcesses() lo.
    void cancelBackgroundWork();

public slots:
    /// Quét các link trong đoạn văn bản, đưa mọi ảnh/video/tài liệu tìm được vào hàng đợi (link không ra gì -> dòng ERR).
    /// `forceDeep` = luôn quét sâu kể cả khi quét thường đã thấy tệp.
    void scanText(const QString& text, bool forceDeep = false);

private slots:
    void onScanClicked();
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
    void onTableContextMenu(const QPoint& pos);
    void onSelectionChanged();
    void onClipboardChanged();
    void onSettingsChanged();
    void onQualityChanged();
    void onScanResult(QList<MediaLink> links);
    void onScanError(QString message);
    void onDeepProgress(QString message);
    void onDeepFinished(ResolveResult result);
    void onFormatsFetched(bool ok, VideoInfo info, QString error);

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void buildUi();
    void updateBundleBanner();
    int rowOfId(int id) const;
    int idOfRow(int row) const;
    void fillRow(int row, int id);
    void refreshSummary();
    /// Mục tác động: các dòng được TICK; không có dòng nào tick thì các dòng đang bôi đen.
    QList<int> targetIds() const;
    void openLinkOfId(int id);
    void setMessage(const QString& text, bool isError = false);

    // Quét trang: từng trang một, mỗi trang chạy quét tĩnh rồi (khi cần) quét sâu.
    void startNextScan();
    void startDeepScan();
    void finishScanPage();
    void endScan();
    void stopScan();
    /// Đưa các mục tìm được vào hàng đợi; trả số mục MỚI (không tính mục đã có sẵn).
    int addScanned(const QList<ResolvedMedia>& items);
    void updateScanStatus();

    // Chọn chất lượng riêng cho một video yt-dlp.
    void pickFormatFor(int id);

    DownloadQueue* m_queue{nullptr};
    DownloaderSettings* m_settings{nullptr};

    QLabel* m_bundleBanner{nullptr};
    QPlainTextEdit* m_urlEdit{nullptr};
    QPushButton* m_scanBtn{nullptr};
    QLabel* m_folderLabel{nullptr};
    QComboBox* m_qualityCombo{nullptr};
    QCheckBox* m_clipboardCheck{nullptr};
    QCheckBox* m_deepCheck{nullptr};
    QLabel* m_summaryLabel{nullptr};
    QLabel* m_messageLabel{nullptr};
    QPushButton* m_downloadBtn{nullptr};
    QTableWidget* m_table{nullptr};

    QString m_lastClipboardText;
    bool m_updatingTable{false};

    // Trạng thái quét trang.
    PageMediaScanner* m_scanner{nullptr}; ///< Dựng mới cho từng trang, hủy ngay khi xong/dừng (không có API hủy riêng)
    LinkResolver* m_resolver{nullptr};
    QStringList m_scanQueue;              ///< Các trang còn chờ quét
    QString m_scanUrl;                    ///< Trang đang quét (Referer cho các tệp quét tĩnh)
    bool m_scanBusy{false};
    bool m_scanStopped{false};
    bool m_scanForceDeep{false};
    bool m_scanDeepRunning{false};
    int m_scanAddedTotal{0};              ///< Mục MỚI đã đưa vào hàng đợi trong lượt quét này
    int m_scanFoundTotal{0};              ///< Mục tìm thấy (kể cả mục đã có sẵn trong hàng đợi)
    int m_scanPageFound{0};               ///< Mục tìm thấy ở trang đang quét
    int m_scanPagesDone{0};
    int m_scanPagesTotal{0};
    QString m_scanDeepProgress;
    QString m_scanStaticError;
    QString m_scanDeepError;
    QString m_scanDeepWarning;
    QStringList m_scanNotes;              ///< Cảnh báo/lỗi từng trang, hiện ở cuối lượt quét

    // Chọn chất lượng riêng.
    YtDlpInfoWorker* m_infoWorker{nullptr};
    int m_formatItemId{-1};
};
