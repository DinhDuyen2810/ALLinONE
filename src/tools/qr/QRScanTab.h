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
    int presentResults(const QImage& image, const QList<QRDecoded>& found, const QString& sourceName);
    /// Chỉ giải phóng QCamera/phiên/sink, KHÔNG đụng trạng thái nút Camera (khác stopCamera()).
    void releaseCamera();

    QRImageView* m_view{nullptr};
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
};
