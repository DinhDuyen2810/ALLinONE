#include "PlatformVideoTab.h"

#include "DownloaderUiStyle.h"
#include "engine/YtDlpController.h"
#include "engine/YtDlpDownloadWorker.h"
#include "engine/YtDlpInfoWorker.h"

#include <QComboBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

PlatformVideoTab::PlatformVideoTab(QWidget* parent)
    : QWidget(parent)
{
    m_saveFolder = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    buildUi();
    updateBundleBanner();

    m_infoWorker = new YtDlpInfoWorker(this);
    connect(m_infoWorker, &YtDlpInfoWorker::infoFetched, this, &PlatformVideoTab::onInfoFetched);
}

PlatformVideoTab::~PlatformVideoTab()
{
    if (m_infoWorker && m_infoWorker->isRunning())
        m_infoWorker->wait(5000);
}

void PlatformVideoTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    m_bundleBanner = new QLabel(this);
    m_bundleBanner->setWordWrap(true);
    m_bundleBanner->setVisible(false);
    root->addWidget(m_bundleBanner);

    auto* introLabel = new QLabel(
        "Tải video từ YouTube/Facebook/TikTok và hơn 1000 trang khác (dựa trên yt-dlp, mã nguồn mở) - "
        "dán URL video (không phải URL playlist/kênh).",
        this);
    introLabel->setWordWrap(true);
    introLabel->setStyleSheet(DownloaderUi::bannerStyle("info"));
    root->addWidget(introLabel);

    m_disclaimerLabel = new QLabel(
        "⚠ Bạn tự chịu trách nhiệm tuân thủ Điều khoản dịch vụ của trang nguồn và luật bản quyền hiện "
        "hành ở nơi bạn sống khi tải về - chỉ nên dùng cho mục đích cá nhân/hợp pháp.",
        this);
    m_disclaimerLabel->setWordWrap(true);
    m_disclaimerLabel->setStyleSheet(DownloaderUi::bannerStyle("warn"));
    root->addWidget(m_disclaimerLabel);

    auto* urlRow = new QHBoxLayout();
    m_urlEdit = new QLineEdit(this);
    m_urlEdit->setStyleSheet(DownloaderUi::inputStyle());
    m_urlEdit->setPlaceholderText("https://www.youtube.com/watch?v=...");
    connect(m_urlEdit, &QLineEdit::returnPressed, this, &PlatformVideoTab::onFetchInfoClicked);
    urlRow->addWidget(m_urlEdit, 1);
    m_fetchBtn = new QPushButton("Lấy thông tin", this);
    m_fetchBtn->setStyleSheet(DownloaderUi::primaryButtonStyle());
    m_fetchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_fetchBtn, &QPushButton::clicked, this, &PlatformVideoTab::onFetchInfoClicked);
    urlRow->addWidget(m_fetchBtn);
    root->addLayout(urlRow);

    m_titleLabel = new QLabel(this);
    m_titleLabel->setWordWrap(true);
    m_titleLabel->setStyleSheet("color: #1f2328; font-size: 13px; font-weight: bold;");
    root->addWidget(m_titleLabel);

    auto* formatRow = new QHBoxLayout();
    auto* formatCaption = new QLabel("Chất lượng:", this);
    formatCaption->setStyleSheet("color: #57606a; font-size: 12px;");
    formatRow->addWidget(formatCaption);
    m_formatCombo = new QComboBox(this);
    m_formatCombo->setStyleSheet(DownloaderUi::inputStyle());
    m_formatCombo->setEnabled(false);
    formatRow->addWidget(m_formatCombo, 1);
    root->addLayout(formatRow);

    auto* folderRow = new QHBoxLayout();
    auto* folderCaption = new QLabel("Thư mục lưu:", this);
    folderCaption->setStyleSheet("color: #57606a; font-size: 12px;");
    folderRow->addWidget(folderCaption);
    m_folderLabel = new QLabel(m_saveFolder, this);
    m_folderLabel->setStyleSheet("color: #1f2328; font-size: 12px;");
    m_folderLabel->setWordWrap(true); // đường dẫn thư mục có thể rất dài - tránh bị cắt cụt không thấy hết
    m_folderLabel->setToolTip(m_saveFolder);
    folderRow->addWidget(m_folderLabel, 1);
    m_chooseFolderBtn = new QPushButton("Đổi...", this);
    m_chooseFolderBtn->setStyleSheet(DownloaderUi::buttonStyle());
    m_chooseFolderBtn->setCursor(Qt::PointingHandCursor);
    connect(m_chooseFolderBtn, &QPushButton::clicked, this, &PlatformVideoTab::onChooseFolderClicked);
    folderRow->addWidget(m_chooseFolderBtn);
    root->addLayout(folderRow);

    m_downloadBtn = new QPushButton("⬇ Tải xuống", this);
    m_downloadBtn->setStyleSheet(DownloaderUi::primaryButtonStyle());
    m_downloadBtn->setCursor(Qt::PointingHandCursor);
    m_downloadBtn->setEnabled(false);
    connect(m_downloadBtn, &QPushButton::clicked, this, &PlatformVideoTab::onDownloadClicked);
    root->addWidget(m_downloadBtn);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setVisible(false);
    root->addWidget(m_progressBar);

    m_progressLabel = new QLabel(this);
    m_progressLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    m_progressLabel->setWordWrap(true);
    root->addWidget(m_progressLabel);

    root->addStretch();
}

void PlatformVideoTab::updateBundleBanner()
{
    QString missing;
    const bool available = YtDlpController::isBundleAvailable(&missing);
    m_bundleBanner->setVisible(!available);
    if (!available)
    {
        m_bundleBanner->setText(
            QString("⚠ Thiếu %1 trong thư mục \"yt-dlp\" cạnh file chạy - tính năng tải video nền tảng "
                   "chưa dùng được. Xem THIRD_PARTY.md để biết cách tải, rồi chạy lại build_app.bat.")
                .arg(missing));
        m_bundleBanner->setStyleSheet(DownloaderUi::bannerStyle("danger"));
        m_fetchBtn->setEnabled(false);
    }
}

void PlatformVideoTab::setBusyFetching(bool busy)
{
    m_fetchBtn->setEnabled(!busy && YtDlpController::isBundleAvailable());
    m_urlEdit->setEnabled(!busy);
}

void PlatformVideoTab::setBusyDownloading(bool busy)
{
    m_downloadBtn->setEnabled(!busy);
    m_formatCombo->setEnabled(!busy);
    m_fetchBtn->setEnabled(!busy && YtDlpController::isBundleAvailable());
    m_progressBar->setVisible(busy);
}

void PlatformVideoTab::onFetchInfoClicked()
{
    const QString url = m_urlEdit->text().trimmed();
    if (url.isEmpty())
        return;
    setBusyFetching(true);
    m_titleLabel->setText("⏳ Đang lấy thông tin video (có thể mất vài giây)...");
    m_formatCombo->clear();
    m_formatCombo->setEnabled(false);
    m_downloadBtn->setEnabled(false);

    m_infoWorker->setUrl(url);
    m_infoWorker->start();
}

void PlatformVideoTab::onInfoFetched(bool ok, VideoInfo info, QString error)
{
    setBusyFetching(false);

    if (!ok)
    {
        m_titleLabel->setText("⚠ " + error);
        return;
    }

    m_currentInfo = info;
    m_titleLabel->setText(QString("%1  (%2)").arg(info.title, info.durationLabel()));

    m_formatCombo->clear();
    for (const auto& fmt : info.formats)
        m_formatCombo->addItem(fmt.displayLabel(), fmt.formatId);
    // Mặc định chọn mục CUỐI (yt-dlp trả formats theo thứ tự xấu nhất -> tốt nhất) - chất lượng tốt nhất trước.
    if (m_formatCombo->count() > 0)
        m_formatCombo->setCurrentIndex(m_formatCombo->count() - 1);
    m_formatCombo->setEnabled(m_formatCombo->count() > 0);
    m_downloadBtn->setEnabled(m_formatCombo->count() > 0);

    if (info.formats.isEmpty())
        m_titleLabel->setText(m_titleLabel->text() + " - không tìm thấy định dạng nào khả dụng.");
}

void PlatformVideoTab::onChooseFolderClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "Chọn thư mục lưu", m_saveFolder);
    if (dir.isEmpty())
        return;
    m_saveFolder = dir;
    m_folderLabel->setText(m_saveFolder);
    m_folderLabel->setToolTip(m_saveFolder);
}

void PlatformVideoTab::onDownloadClicked()
{
    const QString url = m_urlEdit->text().trimmed();
    const QString formatId = m_formatCombo->currentData().toString();
    if (url.isEmpty() || formatId.isEmpty())
        return;

    setBusyDownloading(true);
    m_progressBar->setRange(0, 0);
    m_progressLabel->setText("⏳ Đang tải...");

    delete m_downloadWorker;
    m_downloadWorker = new YtDlpDownloadWorker(this);
    connect(m_downloadWorker, &YtDlpDownloadWorker::progress, this, &PlatformVideoTab::onDownloadProgress);
    connect(m_downloadWorker, &YtDlpDownloadWorker::finished, this, &PlatformVideoTab::onDownloadFinished);
    m_downloadWorker->start(url, formatId, m_saveFolder);
}

void PlatformVideoTab::onDownloadProgress(qint64 downloaded, qint64 total, qint64 speed, qint64 eta)
{
    if (total > 0)
    {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(static_cast<int>(100.0 * downloaded / total));
        m_progressLabel->setText(QString("%1 / %2 - %3 - còn lại %4")
                                      .arg(DownloaderUi::formatBytes(downloaded), DownloaderUi::formatBytes(total),
                                           DownloaderUi::formatSpeed(speed), DownloaderUi::formatEta(eta)));
    }
    else
    {
        m_progressBar->setRange(0, 0);
        m_progressLabel->setText(QString("%1 đã tải - %2")
                                      .arg(DownloaderUi::formatBytes(downloaded), DownloaderUi::formatSpeed(speed)));
    }
}

void PlatformVideoTab::onDownloadFinished(bool ok, QString error)
{
    setBusyDownloading(false);
    if (ok)
    {
        m_progressLabel->setText("✓ Đã tải xong, lưu tại: " + m_saveFolder);
    }
    else
    {
        m_progressLabel->setText("⚠ Tải thất bại: " + error);
        QMessageBox::warning(this, "Tải video", "Không tải được:\n" + error);
    }
}
