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
    // Yêu cầu hủy rồi chờ KHÔNG giới hạn: luồng tự kết thúc yt-dlp.exe và thoát trong vài trăm ms. Trước
    // đây chỉ wait(5000) trong khi yt-dlp được chờ tới 45 giây - quá 5 giây là hủy một QThread còn đang
    // chạy (hành vi KHÔNG XÁC ĐỊNH theo tài liệu Qt).
    if (m_infoWorker)
    {
        m_infoWorker->requestCancel();
        m_infoWorker->wait();
    }
    if (m_downloadWorker)
        m_downloadWorker->cancel();
}

void PlatformVideoTab::cancelActiveDownload()
{
    if (m_infoWorker && m_infoWorker->isRunning())
        m_infoWorker->requestCancel();
    if (m_downloadWorker)
        m_downloadWorker->cancel();
}

bool PlatformVideoTab::isDownloading() const
{
    return m_downloadWorker && m_downloadWorker->isRunning();
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
    connect(m_urlEdit, &QLineEdit::textChanged, this, &PlatformVideoTab::onUrlTextChanged);
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

    // Trước đây cách DUY NHẤT để dừng một lượt tải là đóng cả cửa sổ.
    m_cancelBtn = new QPushButton("✕ Hủy tải", this);
    m_cancelBtn->setStyleSheet(DownloaderUi::dangerButtonStyle());
    m_cancelBtn->setCursor(Qt::PointingHandCursor);
    m_cancelBtn->setVisible(false);
    connect(m_cancelBtn, &QPushButton::clicked, this, &PlatformVideoTab::onCancelDownloadClicked);
    root->addWidget(m_cancelBtn);

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
    const bool hasInfo = !m_infoUrl.isEmpty() && m_formatCombo->count() > 0;
    m_downloadBtn->setEnabled(!busy && hasInfo);
    m_formatCombo->setEnabled(!busy && hasInfo);
    m_fetchBtn->setEnabled(!busy && YtDlpController::isBundleAvailable());
    // Khóa cả ô URL: Enter trong ô này chạy "Lấy thông tin", mà lấy thông tin xong lại mở khóa nút Tải
    // ngay giữa lúc đang tải - bấm tiếp là giết lượt tải đang chạy.
    m_urlEdit->setEnabled(!busy);
    m_chooseFolderBtn->setEnabled(!busy);
    m_progressBar->setVisible(busy);
    m_cancelBtn->setVisible(busy);
    m_cancelBtn->setEnabled(busy);
}

void PlatformVideoTab::clearFetchedInfo()
{
    m_infoUrl.clear();
    m_currentInfo = VideoInfo();
    m_formatCombo->clear();
    m_formatCombo->setEnabled(false);
    m_downloadBtn->setEnabled(false);
}

void PlatformVideoTab::onUrlTextChanged(const QString& text)
{
    // Thông tin/định dạng đang hiển thị thuộc về m_infoUrl - ô nhập đổi sang địa chỉ khác thì chúng không
    // còn đúng nữa: bỏ đi, buộc lấy thông tin lại.
    if (m_infoUrl.isEmpty() || isDownloading() || text.trimmed() == m_infoUrl)
        return;
    clearFetchedInfo();
    m_titleLabel->setText("Địa chỉ đã thay đổi - bấm \"Lấy thông tin\" lại trước khi tải.");
}

void PlatformVideoTab::onFetchInfoClicked()
{
    if (isDownloading() || m_infoWorker->isRunning())
        return;
    const QString url = m_urlEdit->text().trimmed();
    if (url.isEmpty())
        return;
    if (!YtDlpController::isSupportedVideoUrl(url))
    {
        m_titleLabel->setText("⚠ Địa chỉ không hợp lệ - hãy dán URL video bắt đầu bằng http:// hoặc https://.");
        return;
    }
    clearFetchedInfo();
    m_pendingInfoUrl = url;
    setBusyFetching(true);
    m_titleLabel->setText("⏳ Đang lấy thông tin video (có thể mất vài giây)...");

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
    {
        if (!fmt.hasVideo && !fmt.hasAudio)
            continue; // ảnh xem trước (storyboard)... - không phải thứ để tải như video
        // Dữ liệu của mục = bộ chọn `-f` đầy đủ: định dạng chỉ có hình được ghép kèm âm thanh tốt nhất.
        const bool videoOnly = fmt.hasVideo && !fmt.hasAudio;
        m_formatCombo->addItem(fmt.displayLabel() + (videoOnly ? " + ghép âm thanh tốt nhất" : ""),
                               YtDlpDownloadWorkerInternal::formatSelectorFor(fmt));
    }
    if (m_formatCombo->count() == 0)
    {
        m_titleLabel->setText(m_titleLabel->text() + " - không tìm thấy định dạng nào khả dụng.");
        return;
    }

    // Mục đầu + mặc định: để yt-dlp tự chọn (video tốt nhất + âm thanh tốt nhất, tự ghép qua ffmpeg). Trước
    // đây mặc định là mục CUỐI danh sách - với YouTube thường là một định dạng CHỈ CÓ HÌNH, tải ra không tiếng.
    m_formatCombo->insertItem(0, "Tự động - chất lượng tốt nhất (video + âm thanh)", QString());
    m_formatCombo->setCurrentIndex(0);
    m_infoUrl = m_pendingInfoUrl;
    m_formatCombo->setEnabled(true);
    m_downloadBtn->setEnabled(true);
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
    // Không bao giờ thay/hủy một worker đang chạy; URL lấy từ m_infoUrl (đúng video đã lấy thông tin).
    if (isDownloading() || m_infoUrl.isEmpty() || m_formatCombo->currentIndex() < 0)
        return;
    const QString formatSelector = m_formatCombo->currentData().toString(); // rỗng = "Tự động"

    if (m_downloadWorker)
        m_downloadWorker->deleteLater(); // worker của lượt trước - đã chạy xong
    m_downloadWorker = new YtDlpDownloadWorker(this);
    connect(m_downloadWorker, &YtDlpDownloadWorker::progress, this, &PlatformVideoTab::onDownloadProgress);
    connect(m_downloadWorker, &YtDlpDownloadWorker::finished, this, &PlatformVideoTab::onDownloadFinished);

    setBusyDownloading(true);
    m_progressBar->setRange(0, 0);
    m_progressLabel->setText("⏳ Đang tải...");
    m_downloadWorker->start(m_infoUrl, formatSelector, m_saveFolder);
}

void PlatformVideoTab::onCancelDownloadClicked()
{
    m_cancelBtn->setEnabled(false);
    m_progressLabel->setText("⏳ Đang hủy...");
    if (m_downloadWorker)
        m_downloadWorker->cancel(); // kết quả báo qua onDownloadFinished() như bình thường
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
    if (sender() && sender() != m_downloadWorker)
        return; // tín hiệu muộn của một worker cũ đã bị thay
    const bool canceled = m_downloadWorker && m_downloadWorker->wasCanceled();
    setBusyDownloading(false);
    if (ok)
    {
        m_progressLabel->setText("✓ Đã tải xong, lưu tại: " + m_saveFolder);
    }
    else if (canceled)
    {
        // Chính người dùng bấm Hủy/đóng cửa sổ - không phải lỗi, không hiện hộp cảnh báo "Không tải được".
        m_progressLabel->setText("Đã hủy tải. Các tệp tải dở (.part/.ytdl) nếu còn trong thư mục lưu có thể xóa đi.");
    }
    else
    {
        m_progressLabel->setText("⚠ Tải thất bại: " + error);
        QMessageBox::warning(this, "Tải video", "Không tải được:\n" + error);
    }
}
