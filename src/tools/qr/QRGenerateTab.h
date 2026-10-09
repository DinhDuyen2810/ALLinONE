#pragma once

#include <QColor>
#include <QImage>
#include <QWidget>

#include "QRCodec.h"
#include "QRPayload.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTimer;

/// Tab tạo mã QR: chọn loại nội dung, tùy chỉnh giao diện, xem trước trực tiếp, lưu/sao chép.
class QRGenerateTab : public QWidget
{
    Q_OBJECT

public:
    explicit QRGenerateTab(QWidget* parent = nullptr);

    /// Đặt nội dung dạng văn bản thô (dùng cho "Tạo lại mã").
    void setRawText(const QString& text);

    // ---- Dùng cho kiểm thử / truy xuất ----
    QString currentContent() const;           // nội dung sẽ được mã hóa ("" nếu chưa hợp lệ)
    QImage currentImage() const { return m_image; }
    QRMatrix currentMatrix() const { return m_matrix; }
    bool saveTo(const QString& path, QString* error = nullptr);
    bool copyToClipboard();

public slots:
    void refresh();

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTypeChanged(int index);
    void scheduleRefresh();
    void onPickForeground();
    void onPickBackground();
    void onSwapColors();
    void onPickLogo();
    void onClearLogo();
    void onSave();
    void onCopy();

private:
    void buildUi();
    QWidget* makeTextPage();
    QWidget* makeUrlPage();
    QWidget* makeWifiPage();
    QWidget* makeEmailPage();
    QWidget* makeSmsPage();
    QWidget* makePhonePage();
    QWidget* makeGeoPage();
    QWidget* makeVCardPage();

    QString buildContent(QString* problem) const;
    QRContentType currentType() const;
    QRStyle currentStyle() const;
    void updateColorButtons();
    void setPreviewMessage(const QString& msg);
    void updatePreviewPixmap();

    QTimer* m_timer{nullptr};
    QComboBox* m_typeCombo{nullptr};
    QStackedWidget* m_pages{nullptr};

    // Text / URL
    QPlainTextEdit* m_textEdit{nullptr};
    QLineEdit* m_urlEdit{nullptr};
    // WiFi
    QLineEdit* m_wifiSsid{nullptr};
    QLineEdit* m_wifiPass{nullptr};
    QComboBox* m_wifiSec{nullptr};
    QCheckBox* m_wifiHidden{nullptr};
    // Email
    QLineEdit* m_mailTo{nullptr};
    QLineEdit* m_mailSubject{nullptr};
    QPlainTextEdit* m_mailBody{nullptr};
    // SMS
    QLineEdit* m_smsNumber{nullptr};
    QPlainTextEdit* m_smsMessage{nullptr};
    // Phone
    QLineEdit* m_phoneNumber{nullptr};
    // Geo
    QDoubleSpinBox* m_geoLat{nullptr};
    QDoubleSpinBox* m_geoLon{nullptr};
    // vCard
    QLineEdit* m_vcFirst{nullptr};
    QLineEdit* m_vcLast{nullptr};
    QLineEdit* m_vcOrg{nullptr};
    QLineEdit* m_vcTitle{nullptr};
    QLineEdit* m_vcPhone{nullptr};
    QLineEdit* m_vcEmail{nullptr};
    QLineEdit* m_vcUrl{nullptr};
    QLineEdit* m_vcAddress{nullptr};

    // Options
    QComboBox* m_eccCombo{nullptr};
    QSpinBox* m_sizeSpin{nullptr};
    QSpinBox* m_quietSpin{nullptr};
    QPushButton* m_fgButton{nullptr};
    QPushButton* m_bgButton{nullptr};
    QPushButton* m_swapButton{nullptr};
    QCheckBox* m_roundedCheck{nullptr};
    QPushButton* m_logoButton{nullptr};
    QPushButton* m_logoClearButton{nullptr};
    QSlider* m_logoSlider{nullptr};
    QLabel* m_logoLabel{nullptr};

    // Preview
    QLabel* m_preview{nullptr};
    QLabel* m_info{nullptr};
    QLabel* m_warning{nullptr};
    QPushButton* m_saveButton{nullptr};
    QPushButton* m_copyButton{nullptr};

    QColor m_fg{Qt::black};
    QColor m_bg{Qt::white};
    QImage m_logo;

    // "Tạo lại mã": nội dung NGUYÊN VĂN khi ô văn bản không hiển thị/trả lại đúng được nó (xem setRawText);
    // null khi không dùng. m_rawTextShown là thứ ô văn bản đang trả về cho bản nguyên văn đó.
    QString m_rawText;
    QString m_rawTextShown;

    QRMatrix m_matrix;
    QImage m_image;
    QString m_content;
};
