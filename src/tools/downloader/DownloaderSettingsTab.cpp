#include "DownloaderSettingsTab.h"

#include "DownloaderSettings.h"
#include "DownloaderUiStyle.h"
#include "engine/HeadlessBrowser.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QAbstractSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

DownloaderSettingsTab::DownloaderSettingsTab(DownloaderSettings* settings, QWidget* parent)
    : QWidget(parent)
    , m_settings(settings)
{
    buildUi();
    connect(m_settings, &DownloaderSettings::changed, this, &DownloaderSettingsTab::refreshFromSettings);
    refreshFromSettings();
}

void DownloaderSettingsTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* intro = new QLabel("Mọi thay đổi được lưu ngay và áp dụng cho tất cả các tab của Downloader.", this);
    intro->setWordWrap(true);
    intro->setStyleSheet(DownloaderUi::bannerStyle("info"));
    root->addWidget(intro);

    // ---- Thư mục tải xuống ----
    auto* folderGroup = new QGroupBox("Thư mục tải xuống", this);
    folderGroup->setStyleSheet(DownloaderUi::groupStyle());
    auto* fl = new QHBoxLayout(folderGroup);
    m_folderLabel = new QLabel(folderGroup);
    m_folderLabel->setStyleSheet("color: #1f2328; font-size: 12px; background: transparent;");
    m_folderLabel->setWordWrap(true);
    fl->addWidget(m_folderLabel, 1);
    auto makeBtn = [&](const QString& text, auto slot) {
        auto* b = new QPushButton(text, folderGroup);
        b->setStyleSheet(DownloaderUi::buttonStyle());
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, slot);
        fl->addWidget(b);
    };
    makeBtn("Chọn...", &DownloaderSettingsTab::onChooseFolder);
    makeBtn("Mở thư mục", &DownloaderSettingsTab::onOpenFolder);
    makeBtn("Mặc định", &DownloaderSettingsTab::onResetFolder);
    root->addWidget(folderGroup);

    // ---- Tải xuống ----
    auto* dlGroup = new QGroupBox("Tải xuống", this);
    dlGroup->setStyleSheet(DownloaderUi::groupStyle() + DownloaderUi::inputStyle());
    auto* dl = new QVBoxLayout(dlGroup);
    auto* row1 = new QHBoxLayout();
    auto* l1 = new QLabel("Số tệp tải cùng lúc:", dlGroup);
    l1->setStyleSheet("background: transparent; color: #1f2328;");
    m_concurrentSpin = new QSpinBox(dlGroup);
    m_concurrentSpin->setRange(1, 8);
    m_concurrentSpin->setButtonSymbols(QAbstractSpinBox::NoButtons); // bỏ mũi tên lên/xuống - gõ số trực tiếp
    m_concurrentSpin->setFixedWidth(70);
    connect(m_concurrentSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (!m_refreshing) m_settings->setMaxConcurrent(v);
    });
    row1->addWidget(l1);
    row1->addWidget(m_concurrentSpin);
    row1->addStretch();
    dl->addLayout(row1);

    auto* row2 = new QHBoxLayout();
    auto* l2 = new QLabel("Số mục tối đa khi tải cả hồ sơ/kênh/danh sách phát:", dlGroup);
    l2->setStyleSheet("background: transparent; color: #1f2328;");
    m_maxItemsSpin = new QSpinBox(dlGroup);
    m_maxItemsSpin->setRange(1, 1000);
    m_maxItemsSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_maxItemsSpin->setFixedWidth(70);
    connect(m_maxItemsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) {
        if (!m_refreshing) m_settings->setProfileMaxItems(v);
    });
    row2->addWidget(l2);
    row2->addWidget(m_maxItemsSpin);
    row2->addStretch();
    dl->addLayout(row2);

    m_browserCheck = new QCheckBox("Dùng trình duyệt ẩn (Edge/Chrome) để quét sâu", dlGroup);
    m_browserCheck->setToolTip("Khi các cách khác không tìm thấy tệp: dựng trang bằng Edge/Chrome không giao diện để đọc ảnh/video "
                               "do JavaScript tạo ra; hồ sơ thì quét từng bài đăng. Tài liệu Google Drive mà chủ tệp tắt \"tải xuống\" "
                               "cũng cần mục này: trang xem được mở trong trình duyệt ẩn, chụp từng trang rồi ghép thành PDF (không khuyến khích dùng cho "
                               "mục đích vi phạm pháp luật hoặc bản quyền - chỉ dùng cho tài liệu bạn có quyền sử dụng).");
    m_browserCheck->setStyleSheet("background: transparent; color: #1f2328;");
    connect(m_browserCheck, &QCheckBox::toggled, this, [this](bool on) {
        if (!m_refreshing) m_settings->setUseBrowserFallback(on);
    });
    dl->addWidget(m_browserCheck);
    m_browserLabel = new QLabel(dlGroup);
    m_browserLabel->setWordWrap(true);
    m_browserLabel->setStyleSheet("color: #57606a; font-size: 11px; background: transparent;");
    dl->addWidget(m_browserLabel);
    root->addWidget(dlGroup);

    // ---- Cookie ----
    auto* ckGroup = new QGroupBox("Đăng nhập (cookie) cho yt-dlp", this);
    ckGroup->setStyleSheet(DownloaderUi::groupStyle() + DownloaderUi::inputStyle());
    auto* ck = new QVBoxLayout(ckGroup);
    auto* ckNote = new QLabel(
        "Instagram, một số trang Facebook và nội dung riêng tư cần đăng nhập mới xem được. yt-dlp có thể dùng cookie đăng "
        "nhập từ trình duyệt của bạn. Firefox ổn định nhất; Chrome/Edge bản mới mã hóa cookie nên có thể không đọc được. "
        "Tệp cookies.txt (xuất từ trình duyệt của CHÍNH bạn bằng tiện ích \"Get cookies.txt\") còn dùng được để tải tệp bạn có quyền "
        "(vd Google Drive bạn là chủ sở hữu/người chỉnh sửa). Cookie chỉ được đọc cục bộ và chỉ gửi tới đúng trang tương ứng.",
        ckGroup);
    ckNote->setWordWrap(true);
    ckNote->setStyleSheet("color: #57606a; font-size: 11px; background: transparent;");
    ck->addWidget(ckNote);
    m_cookieNone = new QRadioButton("Không dùng cookie", ckGroup);
    m_cookieBrowser = new QRadioButton("Lấy cookie từ trình duyệt:", ckGroup);
    m_cookieFile = new QRadioButton("Dùng tệp cookies.txt:", ckGroup);
    for (QRadioButton* r : {m_cookieNone, m_cookieBrowser, m_cookieFile})
    {
        r->setStyleSheet("background: transparent; color: #1f2328;");
        connect(r, &QRadioButton::toggled, this, &DownloaderSettingsTab::onCookieControlsChanged);
    }
    ck->addWidget(m_cookieNone);
    auto* browserRow = new QHBoxLayout();
    browserRow->addWidget(m_cookieBrowser);
    m_cookieBrowserCombo = new QComboBox(ckGroup);
    m_cookieBrowserCombo->addItems(DownloaderSettings::supportedCookieBrowsers());
    connect(m_cookieBrowserCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &DownloaderSettingsTab::onCookieControlsChanged);
    browserRow->addWidget(m_cookieBrowserCombo);
    browserRow->addStretch();
    ck->addLayout(browserRow);
    auto* fileRow = new QHBoxLayout();
    fileRow->addWidget(m_cookieFile);
    m_cookieFileEdit = new QLineEdit(ckGroup);
    m_cookieFileEdit->setPlaceholderText("C:\\duong-dan\\cookies.txt");
    connect(m_cookieFileEdit, &QLineEdit::editingFinished, this, &DownloaderSettingsTab::onCookieControlsChanged);
    fileRow->addWidget(m_cookieFileEdit, 1);
    m_cookieFileBtn = new QPushButton("Chọn...", ckGroup);
    m_cookieFileBtn->setStyleSheet(DownloaderUi::buttonStyle());
    m_cookieFileBtn->setCursor(Qt::PointingHandCursor);
    connect(m_cookieFileBtn, &QPushButton::clicked, this, &DownloaderSettingsTab::onChooseCookieFile);
    fileRow->addWidget(m_cookieFileBtn);
    ck->addLayout(fileRow);
    root->addWidget(ckGroup);

    root->addStretch();
}

void DownloaderSettingsTab::refreshFromSettings()
{
    m_refreshing = true;
    m_folderLabel->setText(QDir::toNativeSeparators(m_settings->downloadFolder()));
    m_folderLabel->setToolTip(QDir::toNativeSeparators(m_settings->downloadFolder()));
    m_concurrentSpin->setValue(m_settings->maxConcurrent());
    m_maxItemsSpin->setValue(m_settings->profileMaxItems());
    m_browserCheck->setChecked(m_settings->useBrowserFallback());
    const QString browser = HeadlessBrowser::findBrowserExecutable();
    m_browserLabel->setText(browser.isEmpty() ? "⚠ Không tìm thấy Microsoft Edge hoặc Google Chrome trên máy - tính năng này không dùng được."
                                              : "Trình duyệt sẽ dùng: " + browser);
    switch (m_settings->cookieMode())
    {
        case DownloaderSettings::CookieMode::None: m_cookieNone->setChecked(true); break;
        case DownloaderSettings::CookieMode::Browser: m_cookieBrowser->setChecked(true); break;
        case DownloaderSettings::CookieMode::File: m_cookieFile->setChecked(true); break;
    }
    m_cookieBrowserCombo->setCurrentText(m_settings->cookieBrowser());
    m_cookieFileEdit->setText(m_settings->cookieFile());
    m_cookieBrowserCombo->setEnabled(m_cookieBrowser->isChecked());
    m_cookieFileEdit->setEnabled(m_cookieFile->isChecked());
    m_cookieFileBtn->setEnabled(m_cookieFile->isChecked());
    m_refreshing = false;
}

void DownloaderSettingsTab::onChooseFolder()
{
    const QString dir = QFileDialog::getExistingDirectory(this, "Chọn thư mục tải xuống", m_settings->downloadFolder());
    if (!dir.isEmpty())
        m_settings->setDownloadFolder(dir);
}

void DownloaderSettingsTab::onOpenFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_settings->downloadFolder()));
}

void DownloaderSettingsTab::onResetFolder()
{
    m_settings->setDownloadFolder(DownloaderSettings::defaultDownloadFolder());
}

void DownloaderSettingsTab::onChooseCookieFile()
{
    const QString f = QFileDialog::getOpenFileName(this, "Chọn tệp cookies.txt", QString(), "Tệp cookie (*.txt);;Tất cả (*)");
    if (f.isEmpty())
        return;
    m_cookieFileEdit->setText(f);
    onCookieControlsChanged();
}

void DownloaderSettingsTab::onCookieControlsChanged()
{
    if (m_refreshing)
        return;
    DownloaderSettings::CookieMode mode = DownloaderSettings::CookieMode::None;
    if (m_cookieBrowser->isChecked())
        mode = DownloaderSettings::CookieMode::Browser;
    else if (m_cookieFile->isChecked())
        mode = DownloaderSettings::CookieMode::File;
    m_settings->setCookies(mode, m_cookieBrowserCombo->currentText(), m_cookieFileEdit->text());
    // Cập nhật trạng thái bật/tắt ô nhập theo lựa chọn (setCookies không phát changed nếu không có gì đổi).
    m_cookieBrowserCombo->setEnabled(m_cookieBrowser->isChecked());
    m_cookieFileEdit->setEnabled(m_cookieFile->isChecked());
    m_cookieFileBtn->setEnabled(m_cookieFile->isChecked());
}
