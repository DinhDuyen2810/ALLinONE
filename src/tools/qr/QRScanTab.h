#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QWidget>

#include "QRCodec.h"
#include "QRPayload.h"

class QCamera;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QMediaCaptureSession;
class QPlainTextEdit;
class QPushButton;
class QRImageView;
class QTableWidget;
class QVideoFrame;
class QVideoSink;
class WlanController;

/// Tab quét mã QR: từ file ảnh, clipboard, vùng màn hình, kéo-thả hoặc camera.
class QRScanTab : public QWidget
{
    Q_OBJECT

public:
    explicit QRScanTab(QWidget* parent = nullptr);
    ~QRScanTab() override;

    /// Giải mã ảnh và hiển thị kết quả. Trả về số mã tìm được.
    int scanImage(const QImage& image, const QString& sourceName);

    QList<QRDecoded> results() const { return m_results; }
    QString statusText() const;

    void stopCamera();

signals:
    void recreateRequested(const QString& text); // "Tạo lại mã này"

public slots:
    void openImageFile();
    void pasteFromClipboard();
    void captureScreen();
    void toggleCamera(bool on);

private slots:
    void onResultSelected(int row);
    void copyContent();
    void openAction();
    void saveContent();
    void onVideoFrame(const QVideoFrame& frame);
    void refreshCameras();

private:
    void buildUi();
    void setStatus(const QString& text, bool error = false);
    void showResult(int index);
    void clearResults();
    bool cameraAvailable() const;
    /// Hiển thị một kết quả ĐÃ giải mã (không giải mã lại) cho ảnh tương ứng. Trả về số mã.
    /// `undecodedCount` > 0 khi quirc có NHÌN THẤY dạng mã QR nhưng không đọc được - dùng để phân biệt
    /// thông báo lỗi "không có gì giống mã QR" với "có thấy nhưng đọc hỏng" lúc `found` rỗng.
    int presentResults(const QImage& image, const QList<QRDecoded>& found, const QString& sourceName, int undecodedCount = 0);
    /// Chỉ giải phóng QCamera/phiên/sink, KHÔNG đụng trạng thái nút Camera (khác stopCamera()).
    void releaseCamera();
    /// Mở ConnectDialog (tái dùng nguyên vẹn của module WiFi Connection) với SSID/mật khẩu/bảo mật đã điền
    /// sẵn từ mã QR WiFi vừa quét được ở hàng `row` - người dùng vẫn tự bấm "Kết nối" ở đó, đúng MỘT mật
    /// khẩu lấy từ chính mã QR, không có cơ chế dò/thử nhiều mật khẩu nào (CLAUDE.md mục 3).
    void connectToScannedWifi(int row);

    QRImageView* m_view{nullptr};
    QLabel* m_resultListLabel{nullptr}; // "KẾT QUẢ" - chỉ hiện khi ảnh có HƠN 1 mã QR (cần chọn giữa các mã)
    QListWidget* m_resultList{nullptr};
    QLabel* m_typeLabel{nullptr};
    QTableWidget* m_fieldTable{nullptr};
    QPlainTextEdit* m_rawText{nullptr};
    QLabel* m_status{nullptr};
    QCheckBox* m_revealCheck{nullptr}; // chỉ hiện khi kết quả đang xem có trường nhạy cảm (mật khẩu WiFi)

    QPushButton* m_openBtn{nullptr};
    QPushButton* m_pasteBtn{nullptr};
    QPushButton* m_screenBtn{nullptr};
    QPushButton* m_cameraBtn{nullptr};
    QComboBox* m_cameraCombo{nullptr};
    QPushButton* m_copyBtn{nullptr};
    QPushButton* m_actionBtn{nullptr};
    QPushButton* m_saveBtn{nullptr};
    QPushButton* m_recreateBtn{nullptr};

    QList<QRDecoded> m_results;
    QList<ParsedPayload> m_parsed;

    // Camera
    QCamera* m_camera{nullptr};
    QMediaCaptureSession* m_session{nullptr};
    QVideoSink* m_sink{nullptr};
    QElapsedTimer m_lastDecode;
    bool m_decodingFrame{false};

    // Tạo/mở lười (lúc thật sự bấm "Kết nối WiFi" lần đầu) - đa số phiên quét QR không bao giờ cần tới.
    WlanController* m_wlan{nullptr};
};
