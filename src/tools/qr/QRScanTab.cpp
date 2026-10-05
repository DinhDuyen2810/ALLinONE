#include "QRScanTab.h"

#include "QRHistoryStore.h"
#include "QRImageView.h"
#include "QRUiStyle.h"
#include "ScreenSnipOverlay.h"

#include <QApplication>
#include <QCamera>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoFrame>
#include <QVideoSink>

QRScanTab::QRScanTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    refreshCameras();
    connect(new QMediaDevices(this), &QMediaDevices::videoInputsChanged, this, &QRScanTab::refreshCameras);

    auto* paste = new QShortcut(QKeySequence::Paste, this);
    paste->setContext(Qt::WidgetWithChildrenShortcut);
    connect(paste, &QShortcut::activated, this, &QRScanTab::pasteFromClipboard);
}

QRScanTab::~QRScanTab()
{
    stopCamera();
}

void QRScanTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    // ---------- Thanh nút nguồn ----------
    auto* bar = new QHBoxLayout();
    bar->setSpacing(8);
    m_openBtn = new QPushButton("📁 Mở ảnh...", this);
    m_pasteBtn = new QPushButton("📋 Dán ảnh (Ctrl+V)", this);
    m_screenBtn = new QPushButton("🖥 Chụp vùng màn hình", this);
    m_cameraBtn = new QPushButton("📷 Camera", this);
    m_cameraBtn->setCheckable(true);
    m_cameraCombo = new QComboBox(this);
    m_cameraCombo->setMinimumWidth(160);
    m_cameraCombo->setStyleSheet(QRUi::inputStyle());
    for (QPushButton* b : {m_openBtn, m_pasteBtn, m_screenBtn, m_cameraBtn})
    {
        b->setStyleSheet(QRUi::buttonStyle());
        b->setCursor(Qt::PointingHandCursor);
        bar->addWidget(b);
    }
    bar->addWidget(m_cameraCombo);
    bar->addStretch();
    root->addLayout(bar);

    connect(m_openBtn, &QPushButton::clicked, this, &QRScanTab::openImageFile);
    connect(m_pasteBtn, &QPushButton::clicked, this, &QRScanTab::pasteFromClipboard);
    connect(m_screenBtn, &QPushButton::clicked, this, &QRScanTab::captureScreen);
    connect(m_cameraBtn, &QPushButton::toggled, this, &QRScanTab::toggleCamera);

    // ---------- Thân: ảnh | kết quả ----------
    auto* body = new QHBoxLayout();
    body->setSpacing(12);

    m_view = new QRImageView(this);
    connect(m_view, &QRImageView::imageDropped, this, [this](const QImage& img, const QString& name) {
        scanImage(img, name);
    });
    body->addWidget(m_view, 5);

    auto* right = new QWidget(this);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->setSpacing(8);

    auto* listLabel = new QLabel("KẾT QUẢ", right);
    listLabel->setStyleSheet("color: #0969da; font-size: 11px; font-weight: bold; letter-spacing: 1.5px;");
    rl->addWidget(listLabel);

    m_resultList = new QListWidget(right);
    m_resultList->setStyleSheet(QRUi::tableStyle());
    m_resultList->setMaximumHeight(96);
    m_resultList->setTextElideMode(Qt::ElideRight);
    rl->addWidget(m_resultList);
    connect(m_resultList, &QListWidget::currentRowChanged, this, &QRScanTab::onResultSelected);

    m_typeLabel = new QLabel(right);
    m_typeLabel->setStyleSheet("color: #1a7f37; font-weight: bold; font-size: 13px;");
    rl->addWidget(m_typeLabel);

    m_fieldTable = new QTableWidget(0, 2, right);
    m_fieldTable->setHorizontalHeaderLabels({"Trường", "Giá trị"});
    m_fieldTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_fieldTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_fieldTable->verticalHeader()->setVisible(false);
    m_fieldTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fieldTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fieldTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_fieldTable->setWordWrap(true);
    m_fieldTable->setStyleSheet(QRUi::tableStyle());
    m_fieldTable->setMaximumHeight(150);
    rl->addWidget(m_fieldTable);

    auto* rawLabel = new QLabel("NỘI DUNG GỐC", right);
    rawLabel->setStyleSheet("color: #0969da; font-size: 11px; font-weight: bold; letter-spacing: 1.5px;");
    rl->addWidget(rawLabel);

    m_rawText = new QPlainTextEdit(right);
    m_rawText->setReadOnly(true);
    m_rawText->setStyleSheet(QRUi::inputStyle());
    rl->addWidget(m_rawText, 1);

    auto* row1 = new QHBoxLayout();
    m_copyBtn = new QPushButton("📋 Sao chép", right);
    m_actionBtn = new QPushButton("Mở", right);
    row1->addWidget(m_copyBtn);
    row1->addWidget(m_actionBtn);
    rl->addLayout(row1);
    auto* row2 = new QHBoxLayout();
    m_saveBtn = new QPushButton("💾 Lưu .txt", right);
    m_recreateBtn = new QPushButton("🔁 Tạo lại mã", right);
    row2->addWidget(m_saveBtn);
    row2->addWidget(m_recreateBtn);
    rl->addLayout(row2);
    for (QPushButton* b : {m_copyBtn, m_actionBtn, m_saveBtn, m_recreateBtn})
    {
        b->setStyleSheet(QRUi::buttonStyle());
        b->setCursor(Qt::PointingHandCursor);
        b->setEnabled(false);
    }
    connect(m_copyBtn, &QPushButton::clicked, this, &QRScanTab::copyContent);
    connect(m_actionBtn, &QPushButton::clicked, this, &QRScanTab::openAction);
    connect(m_saveBtn, &QPushButton::clicked, this, &QRScanTab::saveContent);
    connect(m_recreateBtn, &QPushButton::clicked, this, [this] {
        const int row = m_resultList->currentRow();
        if (row >= 0 && row < m_results.size())
            emit recreateRequested(m_results[row].text);
    });

    body->addWidget(right, 4);
    root->addLayout(body, 1);

    m_status = new QLabel(this);
    m_status->setStyleSheet("background-color: #ffffff; color: #57606a; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 10px; font-size: 12px;");
    root->addWidget(m_status);
    setStatus("Chọn một nguồn ảnh để quét mã QR.");
}

void QRScanTab::setStatus(const QString& text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(QString("background-color: #ffffff; color: %1; border: 1px solid %2; border-radius: 8px; padding: 6px 10px; font-size: 12px;")
                                .arg(error ? "#cf222e" : "#57606a", error ? "#cf222e" : "#d0d7de"));
}

QString QRScanTab::statusText() const
{
    return m_status->text();
}

// ---------------------------------------------------------------- Kết quả

void QRScanTab::clearResults()
{
    m_results.clear();
    m_parsed.clear();
    m_resultList->blockSignals(true);
    m_resultList->clear();
    m_resultList->blockSignals(false);
    m_fieldTable->setRowCount(0);
    m_rawText->clear();
    m_typeLabel->clear();
    for (QPushButton* b : {m_copyBtn, m_actionBtn, m_saveBtn, m_recreateBtn})
        b->setEnabled(false);
    m_actionBtn->setText("Mở");
}

int QRScanTab::scanImage(const QImage& image, const QString& sourceName)
{
    if (image.isNull())
    {
        setStatus("Ảnh không hợp lệ.", true);
        return 0;
    }

    QApplication::setOverrideCursor(Qt::WaitCursor);
    const QList<QRDecoded> found = QRCodec::decode(image);
    QApplication::restoreOverrideCursor();

    clearResults();
    m_view->setImage(image);

    if (found.isEmpty())
    {
        setStatus(QString("Không tìm thấy mã QR trong: %1. Thử ảnh rõ nét hơn hoặc cắt sát vùng mã.").arg(sourceName), true);
        return 0;
    }

    m_results = found;
    QList<QPolygon> polys;
    for (const QRDecoded& d : found)
    {
        polys << d.corners;
        m_parsed << QRPayload::parse(d.text);
    }
    m_view->setHighlights(polys);

    for (int i = 0; i < found.size(); ++i)
    {
        QString snippet = found[i].text.simplified();
        if (snippet.size() > 60)
            snippet = snippet.left(60) + "…";
        m_resultList->addItem(QString("%1. [%2] %3").arg(i + 1).arg(m_parsed[i].typeName, snippet));
        QRHistoryStore::instance().add("scan", m_parsed[i].typeName, found[i].text);
    }
    m_resultList->setCurrentRow(0);

    setStatus(QString("✓ Tìm thấy %1 mã QR trong: %2").arg(found.size()).arg(sourceName));
    return found.size();
}

void QRScanTab::onResultSelected(int row)
{
    showResult(row);
}

void QRScanTab::showResult(int index)
{
    if (index < 0 || index >= m_results.size())
        return;

    const QRDecoded& d = m_results[index];
    const ParsedPayload& p = m_parsed[index];

    m_typeLabel->setText(QString("%1  •  QR v%2  •  %3  •  sửa lỗi %4")
                             .arg(p.typeName).arg(d.version).arg(d.format, QRCodec::eccName(d.ecc)));

    m_fieldTable->setRowCount(p.fields.size());
    for (int i = 0; i < p.fields.size(); ++i)
    {
        m_fieldTable->setItem(i, 0, new QTableWidgetItem(p.fields[i].first));
        m_fieldTable->setItem(i, 1, new QTableWidgetItem(p.fields[i].second));
    }
    m_fieldTable->resizeRowsToContents();

    m_rawText->setPlainText(d.text);

    m_copyBtn->setEnabled(true);
    m_saveBtn->setEnabled(true);
    m_recreateBtn->setEnabled(true);
    m_actionBtn->setEnabled(!p.actionUrl.isEmpty());
    m_actionBtn->setText(p.actionLabel.isEmpty() ? "Mở" : p.actionLabel);
}

void QRScanTab::copyContent()
{
    const int row = m_resultList->currentRow();
    if (row < 0 || row >= m_results.size())
        return;
    QGuiApplication::clipboard()->setText(m_results[row].text);
    setStatus("✓ Đã sao chép nội dung vào clipboard.");
}

void QRScanTab::openAction()
{
    const int row = m_resultList->currentRow();
    if (row < 0 || row >= m_parsed.size() || m_parsed[row].actionUrl.isEmpty())
        return;

    const QString url = m_parsed[row].actionUrl;
    // Cảnh báo trước khi mở liên kết lạ (mã QR có thể dẫn tới trang độc hại)
    if (QMessageBox::question(this, "Mở liên kết",
                              "Chỉ mở nếu bạn tin tưởng nguồn mã QR này.\n\n" + url + "\n\nTiếp tục?",
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    if (!QDesktopServices::openUrl(QUrl::fromUserInput(url)))
        setStatus("Không thể mở liên kết này.", true);
}

void QRScanTab::saveContent()
{
    const int row = m_resultList->currentRow();
    if (row < 0 || row >= m_results.size())
        return;

    const QString path = QFileDialog::getSaveFileName(this, "Lưu nội dung", "qr_content.txt", "Văn bản (*.txt)");
    if (path.isEmpty())
        return;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        QMessageBox::critical(this, "Lỗi", "Không thể ghi file: " + f.errorString());
        return;
    }
    f.write(m_results[row].text.toUtf8());
    setStatus("✓ Đã lưu: " + path);
}

// ---------------------------------------------------------------- Nguồn ảnh

void QRScanTab::openImageFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, "Chọn ảnh chứa mã QR", QString(),
        "Ảnh (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.ico *.svg);;Tất cả (*.*)");
    if (path.isEmpty())
        return;

    stopCamera();
    const QImage img(path);
    if (img.isNull())
    {
        setStatus("Không đọc được file ảnh: " + path, true);
        return;
    }
    scanImage(img, QFileInfo(path).fileName());
}

void QRScanTab::pasteFromClipboard()
{
    const QMimeData* md = QGuiApplication::clipboard()->mimeData();
    QImage img;
    QString name = "clipboard";
    if (md && md->hasImage())
        img = qvariant_cast<QImage>(md->imageData());
    else if (md && md->hasUrls())
    {
        for (const QUrl& u : md->urls())
            if (u.isLocalFile() && !(img = QImage(u.toLocalFile())).isNull())
            {
                name = u.fileName();
                break;
            }
    }

    if (img.isNull())
    {
        setStatus("Clipboard không có ảnh. Hãy sao chép một ảnh (hoặc chụp màn hình bằng Win+Shift+S) rồi thử lại.", true);
        return;
    }
    stopCamera();
    scanImage(img, name);
}

void QRScanTab::captureScreen()
{
    stopCamera();
    QWidget* top = window();
    top->hide();

    QTimer::singleShot(250, this, [this, top] {
        auto* overlay = new ScreenSnipOverlay();
        connect(overlay, &ScreenSnipOverlay::captured, this, [this, top](const QImage& img) {
            top->showNormal();
            top->raise();
            top->activateWindow();
            scanImage(img, "vùng màn hình");
        });
        connect(overlay, &ScreenSnipOverlay::cancelled, this, [this, top] {
            top->showNormal();
            top->raise();
            top->activateWindow();
            setStatus("Đã hủy chụp màn hình.");
        });
        overlay->show();
        overlay->raise();
        overlay->activateWindow();
        overlay->setFocus();
    });
}

// ---------------------------------------------------------------- Camera

bool QRScanTab::cameraAvailable() const
{
    return !QMediaDevices::videoInputs().isEmpty();
}

void QRScanTab::refreshCameras()
{
    const QString previous = m_cameraCombo->currentData().toByteArray();
    m_cameraCombo->clear();
    const auto devices = QMediaDevices::videoInputs();
    for (const QCameraDevice& d : devices)
        m_cameraCombo->addItem(d.description(), d.id());

    const int idx = m_cameraCombo->findData(previous);
    if (idx >= 0)
        m_cameraCombo->setCurrentIndex(idx);

    const bool any = !devices.isEmpty();
    m_cameraBtn->setEnabled(any);
    m_cameraCombo->setEnabled(any);
    m_cameraBtn->setToolTip(any ? "Quét bằng camera/webcam" : "Không tìm thấy camera nào");
    if (!any && m_cameraBtn->isChecked())
        m_cameraBtn->setChecked(false);
}

void QRScanTab::toggleCamera(bool on)
{
    if (!on)
    {
        stopCamera();
        return;
    }

    if (!cameraAvailable())
    {
        m_cameraBtn->setChecked(false);
        setStatus("Không tìm thấy camera nào.", true);
        return;
    }

    stopCamera();
    clearResults();

    QCameraDevice chosen;
    for (const QCameraDevice& d : QMediaDevices::videoInputs())
        if (d.id() == m_cameraCombo->currentData().toByteArray())
            chosen = d;
    if (chosen.isNull())
        chosen = QMediaDevices::defaultVideoInput();

    m_camera = new QCamera(chosen, this);
    m_session = new QMediaCaptureSession(this);
    m_sink = new QVideoSink(this);
    m_session->setCamera(m_camera);
    m_session->setVideoSink(m_sink);

    connect(m_sink, &QVideoSink::videoFrameChanged, this, &QRScanTab::onVideoFrame);
    connect(m_camera, &QCamera::errorOccurred, this, [this](QCamera::Error, const QString& msg) {
        setStatus("Lỗi camera: " + msg, true);
        m_cameraBtn->setChecked(false);
    });

    m_view->setHighlights({});
    m_lastDecode.invalidate();
    m_camera->start();
    setStatus("Đang quét bằng camera... đưa mã QR vào khung hình.");
}

void QRScanTab::stopCamera()
{
    if (m_cameraBtn && m_cameraBtn->isChecked())
    {
        QSignalBlocker b(m_cameraBtn);
        m_cameraBtn->setChecked(false);
    }
    if (!m_camera)
        return;

    m_camera->stop();
    if (m_sink)
        disconnect(m_sink, nullptr, this, nullptr);
    m_session->setCamera(nullptr);
    m_session->setVideoSink(nullptr);
    m_camera->deleteLater();
    m_session->deleteLater();
    m_sink->deleteLater();
    m_camera = nullptr;
    m_session = nullptr;
    m_sink = nullptr;
}

void QRScanTab::onVideoFrame(const QVideoFrame& frame)
{
    if (!m_camera || !frame.isValid() || m_decodingFrame)
        return;

    QImage img = frame.toImage();
    if (img.isNull())
        return;

    // Luôn cập nhật khung xem trước
    m_view->setImage(img);

    // Giải mã tối đa ~3 lần/giây để không làm nghẽn giao diện
    if (m_lastDecode.isValid() && m_lastDecode.elapsed() < 330)
        return;
    m_lastDecode.restart();

    m_decodingFrame = true;
    QImage work = img;
    if (std::max(work.width(), work.height()) > 960)
        work = work.scaled(960, 960, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QList<QRDecoded> found = QRCodec::decode(work);
    m_decodingFrame = false;

    if (!found.isEmpty())
    {
        stopCamera();
        scanImage(img, "camera");
    }
}
