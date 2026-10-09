#include "DevicesTab.h"

#include "AndroidUiStyle.h"
#include "WirelessPairDialog.h"
#include "engine/AdbController.h"
#include "engine/AdbDeviceLister.h"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QComboBox>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTimer>
#include <QVBoxLayout>

namespace
{
/// Hộp thông báo với nội dung ép về VĂN BẢN THUẦN. Nội dung ở tab này thường chứa chuỗi do adb/thiết bị
/// trả về (thông báo lỗi của adb, số serial, tên model) - QMessageBox mặc định TỰ ĐOÁN định dạng, gặp
/// chuỗi trông giống HTML sẽ hiển thị nó như rich text (thẻ <a>/<img>... từ dữ liệu không tin cậy).
void showPlainMessage(QWidget* parent, QMessageBox::Icon icon, const QString& title, const QString& text)
{
    QMessageBox box(icon, title, text, QMessageBox::Ok, parent);
    box.setTextFormat(Qt::PlainText);
    box.exec();
}
} // namespace

DevicesTab::DevicesTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();

    m_launcher = new ScrcpyLauncher(this);
    connect(m_launcher, &ScrcpyLauncher::started, this, &DevicesTab::onScrcpyStarted);
    connect(m_launcher, &ScrcpyLauncher::finished, this, &DevicesTab::onScrcpyFinished);
    connect(m_launcher, &ScrcpyLauncher::errorOccurred, this, &DevicesTab::onScrcpyError);

    // Liệt kê thiết bị BẤT ĐỒNG BỘ - xem AdbDeviceLister.h vì sao không gọi AdbController::listDevices()
    // (đồng bộ) trên luồng giao diện nữa.
    m_lister = new AdbDeviceLister(this);
    connect(m_lister, &AdbDeviceLister::listed, this, &DevicesTab::onDevicesListed);

    updateBundleBanner();

    // Tự làm mới danh sách thiết bị định kỳ (giống NetworksTab của WiFi) - phát hiện cắm/rút USB hoặc
    // đổi trạng thái cấp quyền ("unauthorized" -> "device" sau khi bấm Cho phép trên điện thoại) mà
    // không cần người dùng tự bấm Làm mới liên tục. Timer CHỈ chạy khi tab đang hiện (showEvent/
    // hideEvent): trước đây nó chạy từ constructor và không bao giờ dừng, nên đóng cửa sổ (chỉ ẩn) rồi
    // ứng dụng vẫn gọi adb.exe mỗi 3 giây cho tới khi thoát hẳn - và chính lần gọi đó dựng lại daemon adb.
    m_autoRefreshTimer = new QTimer(this);
    m_autoRefreshTimer->setObjectName("autoRefreshTimer");
    m_autoRefreshTimer->setInterval(3000);
    connect(m_autoRefreshTimer, &QTimer::timeout, this, &DevicesTab::onAutoRefreshTick);

    // Lưới an toàn cuối cùng: nếu người dùng thoát HẲN One for ALL theo đường khác (vd menu Thoát gọi
    // thẳng qApp->quit() - xem MalwareScanTab.cpp/PartitionTab.cpp) MÀ KHÔNG đóng cửa sổ Android Phone
    // Control trước, closeEvent của cửa sổ đó sẽ không kịp chạy để tự dừng phiên scrcpy. aboutToQuit luôn
    // phát ra đúng một lần trước khi vòng lặp sự kiện kết thúc, bất kể thoát theo đường nào.
    connect(qApp, &QApplication::aboutToQuit, this, &DevicesTab::stopBackgroundWork);
}

DevicesTab::~DevicesTab()
{
    stopBackgroundWork();
}

void DevicesTab::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    m_backgroundAllowed = true;
    m_autoRefreshTimer->start();
    reloadDevices(); // không chờ hết 3 giây đầu mới có danh sách
}

void DevicesTab::hideEvent(QHideEvent* event)
{
    // Ẩn/thu nhỏ/đóng cửa sổ: thôi gọi adb định kỳ (phiên scrcpy đang chạy thì KHÔNG dừng ở đây - thu nhỏ
    // cửa sổ quản lý trong lúc đang điều khiển điện thoại là việc bình thường; đóng hẳn thì
    // AndroidControlWindow::closeEvent gọi stopBackgroundWork()).
    m_backgroundAllowed = false;
    m_autoRefreshTimer->stop();
    QWidget::hideEvent(event);
}

void DevicesTab::stopBackgroundWork()
{
    // Chặn TRƯỚC: stopActiveSession() bên dưới làm onScrcpyFinished() chạy, mà hàm đó gọi reloadDevices() -
    // không chặn thì nó khởi chạy lại adb (và daemon) ngay lúc ta đang dọn.
    m_backgroundAllowed = false;
    if (m_autoRefreshTimer)
        m_autoRefreshTimer->stop();

    stopActiveSession();
    if (m_lister)
        m_lister->cancel();
    AdbController::stopBundledAdbServer();
}

void DevicesTab::stopActiveSession()
{
    if (m_launcher && m_launcher->isRunning())
        m_launcher->stop();
}

void DevicesTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    m_bundleBanner = new QLabel(this);
    m_bundleBanner->setWordWrap(true);
    m_bundleBanner->setVisible(false);
    root->addWidget(m_bundleBanner);

    auto* hint = new QLabel(
        "Trên điện thoại: bật Cài đặt → Tùy chọn nhà phát triển → Gỡ lỗi USB (cắm cáp rồi bấm \"Cho "
        "phép\" khi điện thoại hỏi), hoặc dùng \"Ghép đôi không dây\" bên dưới nếu không có cáp.",
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(AndroidUi::bannerStyle("info"));
    root->addWidget(hint);

    auto* topRow = new QHBoxLayout();
    m_refreshBtn = new QPushButton("🔄 Làm mới", this);
    m_refreshBtn->setStyleSheet(AndroidUi::buttonStyle());
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &DevicesTab::onRefreshClicked);
    topRow->addWidget(m_refreshBtn);

    m_pairBtn = new QPushButton("🔗 Ghép đôi không dây...", this);
    m_pairBtn->setStyleSheet(AndroidUi::buttonStyle());
    m_pairBtn->setCursor(Qt::PointingHandCursor);
    connect(m_pairBtn, &QPushButton::clicked, this, &DevicesTab::onPairClicked);
    topRow->addWidget(m_pairBtn);

    m_switchWirelessBtn = new QPushButton("📶 Chuyển thiết bị đã chọn sang không dây", this);
    m_switchWirelessBtn->setStyleSheet(AndroidUi::buttonStyle());
    m_switchWirelessBtn->setCursor(Qt::PointingHandCursor);
    m_switchWirelessBtn->setEnabled(false);
    connect(m_switchWirelessBtn, &QPushButton::clicked, this, &DevicesTab::onSwitchToWirelessClicked);
    topRow->addWidget(m_switchWirelessBtn);
    topRow->addStretch();
    root->addLayout(topRow);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({"Serial", "Trạng thái", "Model", "Kiểu kết nối"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setStyleSheet(AndroidUi::tableStyle());
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &DevicesTab::onRowSelectionChanged);
    root->addWidget(m_table, 1);

    // ---- Tùy chọn điều khiển: độ linh hoạt kiểu remote thật - xem scrcpy --help ----
    auto* optsGroup = new QGroupBox("Tùy chọn điều khiển", this);
    optsGroup->setStyleSheet(AndroidUi::groupStyle());
    auto* optsLayout = new QFormLayout(optsGroup);
    optsLayout->setContentsMargins(12, 12, 12, 12);
    optsLayout->setSpacing(8);

    m_maxSizeCombo = new QComboBox(this);
    m_maxSizeCombo->setStyleSheet(AndroidUi::inputStyle());
    m_maxSizeCombo->addItem("Gốc (không giới hạn)", 0);
    m_maxSizeCombo->addItem("1920 px", 1920);
    m_maxSizeCombo->addItem("1600 px", 1600);
    m_maxSizeCombo->addItem("1280 px", 1280);
    m_maxSizeCombo->addItem("1024 px", 1024);
    m_maxSizeCombo->addItem("800 px", 800);
    optsLayout->addRow("Độ phân giải tối đa:", m_maxSizeCombo);

    m_bitRateSpin = new QSpinBox(this);
    m_bitRateSpin->setStyleSheet(AndroidUi::inputStyle());
    m_bitRateSpin->setRange(1, 64);
    m_bitRateSpin->setValue(8);
    m_bitRateSpin->setSuffix(" Mbps");
    optsLayout->addRow("Chất lượng hình (bitrate):", m_bitRateSpin);

    m_maxFpsCombo = new QComboBox(this);
    m_maxFpsCombo->setStyleSheet(AndroidUi::inputStyle());
    m_maxFpsCombo->addItem("Không giới hạn", 0);
    m_maxFpsCombo->addItem("60 fps", 60);
    m_maxFpsCombo->addItem("30 fps", 30);
    m_maxFpsCombo->addItem("15 fps", 15);
    optsLayout->addRow("Khung hình tối đa:", m_maxFpsCombo);

    m_stayAwakeCheck = new QCheckBox("Giữ màn hình điện thoại sáng khi đang cắm USB", this);
    m_stayAwakeCheck->setChecked(true);
    optsLayout->addRow(m_stayAwakeCheck);

    m_turnScreenOffCheck = new QCheckBox("Tắt màn hình điện thoại lúc điều khiển (riêng tư + tiết kiệm pin)", this);
    optsLayout->addRow(m_turnScreenOffCheck);

    m_alwaysOnTopCheck = new QCheckBox("Luôn nổi lên trên các cửa sổ khác", this);
    optsLayout->addRow(m_alwaysOnTopCheck);

    m_fullscreenCheck = new QCheckBox("Mở toàn màn hình", this);
    optsLayout->addRow(m_fullscreenCheck);

    m_noAudioCheck = new QCheckBox("Tắt chuyển âm thanh điện thoại sang máy tính", this);
    optsLayout->addRow(m_noAudioCheck);

    m_recordCheck = new QCheckBox("Ghi lại phiên điều khiển ra file .mp4", this);
    connect(m_recordCheck, &QCheckBox::toggled, this, &DevicesTab::onRecordCheckToggled);
    optsLayout->addRow(m_recordCheck);

    auto* recordRow = new QWidget(this);
    auto* recordRowLayout = new QHBoxLayout(recordRow);
    recordRowLayout->setContentsMargins(0, 0, 0, 0);
    m_recordPathEdit = new QLineEdit(this);
    m_recordPathEdit->setStyleSheet(AndroidUi::inputStyle());
    m_recordPathEdit->setReadOnly(true);
    m_recordPathEdit->setPlaceholderText("Chưa chọn nơi lưu...");
    m_recordPathEdit->setEnabled(false);
    m_recordBrowseBtn = new QPushButton("Chọn...", this);
    m_recordBrowseBtn->setStyleSheet(AndroidUi::buttonStyle());
    m_recordBrowseBtn->setEnabled(false);
    connect(m_recordBrowseBtn, &QPushButton::clicked, this, &DevicesTab::onBrowseRecordPath);
    recordRowLayout->addWidget(m_recordPathEdit, 1);
    recordRowLayout->addWidget(m_recordBrowseBtn);
    optsLayout->addRow(recordRow);

    root->addWidget(optsGroup);

    m_statusLabel = new QLabel("Chọn một thiết bị rồi bấm \"Điều khiển\".", this);
    // Văn bản thuần: nhãn này hiện cả thông báo lỗi của adb và số serial thiết bị (dữ liệu bên ngoài) -
    // QLabel mặc định tự đoán và hiển thị rich text nếu chuỗi trông giống HTML.
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_statusLabel);

    auto* bottomRow = new QHBoxLayout();
    m_controlBtn = new QPushButton("▶ Điều khiển", this);
    m_controlBtn->setStyleSheet(AndroidUi::primaryButtonStyle());
    m_controlBtn->setCursor(Qt::PointingHandCursor);
    m_controlBtn->setEnabled(false);
    connect(m_controlBtn, &QPushButton::clicked, this, &DevicesTab::onControlClicked);
    bottomRow->addWidget(m_controlBtn);

    m_stopBtn = new QPushButton("⏹ Dừng điều khiển", this);
    m_stopBtn->setStyleSheet(AndroidUi::dangerButtonStyle());
    m_stopBtn->setCursor(Qt::PointingHandCursor);
    m_stopBtn->setVisible(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &DevicesTab::onStopClicked);
    bottomRow->addWidget(m_stopBtn);

    bottomRow->addStretch();
    root->addLayout(bottomRow);
}

void DevicesTab::updateBundleBanner()
{
    QString missing;
    const bool available = AdbController::isBundleAvailable(&missing);
    m_bundleBanner->setVisible(!available);
    if (!available)
    {
        m_bundleBanner->setText(
            QString("⚠ Thiếu %1 trong thư mục \"scrcpy\" cạnh file chạy - tính năng điều khiển Android "
                   "chưa dùng được. Chạy lại build_app.bat để đóng gói đầy đủ.")
                .arg(missing));
        m_bundleBanner->setStyleSheet(AndroidUi::bannerStyle("danger"));
        m_refreshBtn->setEnabled(false);
        m_pairBtn->setEnabled(false);
    }
}

void DevicesTab::onRefreshClicked()
{
    m_manualRefreshPending = true;
    reloadDevices();
}

void DevicesTab::onAutoRefreshTick()
{
    reloadDevices();
}

void DevicesTab::reloadDevices()
{
    // m_backgroundAllowed: không gọi adb khi tab đã ẩn/đang dọn để thoát (xem stopBackgroundWork()).
    if (!m_backgroundAllowed || !AdbController::isBundleAvailable())
        return;

    // Bất đồng bộ; nếu lần liệt kê trước chưa xong thì refresh() không làm gì (không chồng lệnh) - kết
    // quả của lần đang chạy sẽ tới onDevicesListed().
    m_lister->refresh();
}

void DevicesTab::onDevicesListed(const QList<AndroidDeviceInfo>& devices, const QString& error)
{
    // Ghi nhớ serial đang chọn để giữ lại lựa chọn sau khi làm mới (nếu thiết bị vẫn còn trong danh sách).
    QString previouslySelected;
    if (const auto* sel = selectedDevice())
        previouslySelected = sel->serial;

    m_devices = devices;

    m_table->setRowCount(m_devices.size());
    int restoreRow = -1;
    for (int i = 0; i < m_devices.size(); ++i)
    {
        const auto& d = m_devices[i];
        if (d.serial == previouslySelected)
            restoreRow = i;

        auto setItem = [&](int col, const QString& text, const QString& color = QString()) {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            if (!color.isEmpty())
                item->setForeground(QColor(color));
            m_table->setItem(i, col, item);
        };
        setItem(0, d.serial);
        setItem(1, d.isReady() ? "Sẵn sàng" : (d.isUnauthorized() ? "Chờ cho phép trên máy" : d.state),
                AndroidUi::stateColor(d.state));
        setItem(2, d.model.isEmpty() ? "—" : d.model);
        setItem(3, d.isWireless() ? "📶 Không dây" : "🔌 USB");
    }
    // Thiết bị đang chọn đã biến mất thì BỎ chọn. Trước đây không làm gì trong trường hợp này: bảng vẫn giữ
    // lựa chọn theo SỐ HÀNG, nên rút thiết bị A đang chọn ở hàng đầu thì lựa chọn âm thầm chuyển sang thiết
    // bị B vừa dồn lên hàng đó - bấm "Điều khiển" ngay lúc danh sách tự làm mới là điều khiển nhầm máy.
    if (restoreRow >= 0)
        m_table->selectRow(restoreRow);
    else
        m_table->clearSelection();

    // Trước đây dòng trạng thái bị ghi đè "Tìm thấy N thiết bị." ở MỖI lần tự làm mới (3 giây/lần), xóa
    // mất mọi thông báo quan trọng hơn vừa hiện ("✓ Ghép đôi thành công...", "⚠ Phiên điều khiển kết
    // thúc với mã lỗi..."). Nay chỉ cập nhật khi người dùng tự bấm "Làm mới" hoặc khi kết quả liệt kê
    // THAY ĐỔI so với lần trước, và không bao giờ đè lên trạng thái của phiên điều khiển đang chạy.
    const QString listStatus =
        error.isEmpty() ? QString("Tìm thấy %1 thiết bị.").arg(m_devices.size()) : ("⚠ " + error);
    const bool changed = (listStatus != m_lastListStatus);
    m_lastListStatus = listStatus;
    if ((m_manualRefreshPending || changed) && !m_launcher->isRunning())
        m_statusLabel->setText(listStatus);
    m_manualRefreshPending = false;

    onRowSelectionChanged();
}

const AndroidDeviceInfo* DevicesTab::selectedDevice() const
{
    const auto rows = m_table->selectionModel() ? m_table->selectionModel()->selectedRows() : QModelIndexList();
    if (rows.size() != 1)
        return nullptr;
    const int row = rows.first().row();
    if (row < 0 || row >= m_devices.size())
        return nullptr;
    return &m_devices[row];
}

void DevicesTab::onRowSelectionChanged()
{
    const auto* dev = selectedDevice();
    const bool hasReadyDevice = dev && dev->isReady();
    m_controlBtn->setEnabled(hasReadyDevice && !m_launcher->isRunning());
    // !isRunning(): hàm này chạy lại sau MỖI lần làm mới danh sách - trước đây nó bật lại nút "Chuyển
    // sang không dây" (đã bị onScrcpyStarted() tắt) ngay giữa lúc phiên điều khiển đang chạy.
    m_switchWirelessBtn->setEnabled(dev && dev->isReady() && !dev->isWireless() && !m_launcher->isRunning());
}

void DevicesTab::onPairClicked()
{
    WirelessPairDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString ipPort = dlg.ipAndPort();
    const QString code = dlg.pairingCode();
    if (ipPort.isEmpty() || code.isEmpty())
    {
        QMessageBox::warning(this, "Thiếu thông tin", "Vui lòng nhập đầy đủ IP:Cổng và mã ghép đôi.");
        return;
    }

    m_statusLabel->setText("⏳ Đang ghép đôi...");
    QString error;
    if (!AdbController::pairWireless(ipPort, code, &error))
    {
        showPlainMessage(this, QMessageBox::Critical, "Ghép đôi thất bại", error);
        m_statusLabel->setText("⚠ Ghép đôi thất bại: " + error);
        return;
    }

    m_statusLabel->setText("✓ Ghép đôi thành công. Dùng \"Làm mới\" hoặc chờ tự động để thấy thiết bị (có thể cần adb connect tới cổng kết nối thường, không phải cổng ghép đôi).");
    reloadDevices();
}

void DevicesTab::onSwitchToWirelessClicked()
{
    const auto* dev = selectedDevice();
    if (!dev)
        return;

    // Chép serial ra trước: prepareRecordPath() có thể mở hộp thoại (vòng lặp sự kiện lồng), danh sách
    // thiết bị được làm mới trong lúc đó và con trỏ `dev` không còn hợp lệ.
    const QString serial = dev->serial;

    ScrcpyOptions opts = collectOptions();
    opts.serial = serial;
    opts.switchToWireless = true;
    if (!prepareRecordPath(opts))
        return;

    QString error;
    if (!m_launcher->start(opts, &error))
    {
        showPlainMessage(this, QMessageBox::Critical, "Không khởi chạy được", error);
        return;
    }
    m_recordOverwriteConfirmed = false;
    m_statusLabel->setText("⏳ Đang chuyển \"" + serial + "\" sang không dây và khởi chạy điều khiển...");
}

void DevicesTab::onControlClicked()
{
    const auto* dev = selectedDevice();
    if (!dev)
        return;

    const QString serial = dev->serial; // chép ra trước - xem onSwitchToWirelessClicked()

    ScrcpyOptions opts = collectOptions();
    opts.serial = serial;
    if (!prepareRecordPath(opts))
        return;

    QString error;
    if (!m_launcher->start(opts, &error))
    {
        showPlainMessage(this, QMessageBox::Critical, "Không khởi chạy được", error);
        return;
    }
    m_recordOverwriteConfirmed = false;
    m_statusLabel->setText("⏳ Đang khởi chạy điều khiển \"" + serial + "\"...");
}

void DevicesTab::onStopClicked()
{
    stopActiveSession();
}

ScrcpyOptions DevicesTab::collectOptions() const
{
    ScrcpyOptions opts;
    opts.maxSize = m_maxSizeCombo->currentData().toInt();
    opts.bitRateMbps = m_bitRateSpin->value();
    opts.maxFps = m_maxFpsCombo->currentData().toInt();
    opts.stayAwake = m_stayAwakeCheck->isChecked();
    opts.turnScreenOff = m_turnScreenOffCheck->isChecked();
    opts.alwaysOnTop = m_alwaysOnTopCheck->isChecked();
    opts.fullscreen = m_fullscreenCheck->isChecked();
    opts.disableAudio = m_noAudioCheck->isChecked();
    if (m_recordCheck->isChecked())
        opts.recordFilePath = m_recordPathEdit->text();
    return opts;
}

QString DevicesTab::defaultRecordPath()
{
    // Tên kèm dấu thời gian: trước đây luôn là "android_session.mp4", nên phiên ghi hình thứ hai ÂM THẦM
    // ghi đè lên bản ghi của phiên trước (scrcpy --record không hỏi).
    const QString moviesDir = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    return (moviesDir.isEmpty() ? QDir::homePath() : moviesDir) + "/android_session_" +
           QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".mp4";
}

bool DevicesTab::prepareRecordPath(ScrcpyOptions& options)
{
    if (!m_recordCheck->isChecked())
        return true;

    if (m_recordPathIsAuto || m_recordPathEdit->text().trimmed().isEmpty())
    {
        // Tên mặc định: sinh lại dấu thời gian cho TỪNG phiên, không dùng lại tên của phiên trước.
        m_recordPathIsAuto = true;
        m_recordPathEdit->setText(defaultRecordPath());
        options.recordFilePath = m_recordPathEdit->text();
        return true;
    }

    // Tệp do người dùng tự chọn và đã tồn tại: hộp thoại "Chọn..." chỉ hỏi ghi đè MỘT lần lúc chọn, các
    // phiên sau dùng lại đúng đường dẫn đó thì phải hỏi lại.
    if (QFileInfo::exists(options.recordFilePath) && !m_recordOverwriteConfirmed)
    {
        QMessageBox box(QMessageBox::Question, "Ghi đè file ghi hình?",
                        "File ghi hình đã tồn tại và sẽ bị ghi đè:\n" + QDir::toNativeSeparators(options.recordFilePath) +
                            "\n\nTiếp tục?",
                        QMessageBox::Yes | QMessageBox::No, this);
        box.setTextFormat(Qt::PlainText);
        box.setDefaultButton(QMessageBox::No);
        if (box.exec() != QMessageBox::Yes)
            return false;
    }
    return true;
}

void DevicesTab::onRecordCheckToggled(bool checked)
{
    m_recordPathEdit->setEnabled(checked);
    m_recordBrowseBtn->setEnabled(checked);
    if (checked && m_recordPathEdit->text().isEmpty())
    {
        m_recordPathEdit->setText(defaultRecordPath());
        m_recordPathIsAuto = true;
    }
}

void DevicesTab::onBrowseRecordPath()
{
    const QString path = QFileDialog::getSaveFileName(this, "Chọn nơi lưu file ghi hình", m_recordPathEdit->text(), "Video (*.mp4)");
    if (!path.isEmpty())
    {
        m_recordPathEdit->setText(path);
        m_recordPathIsAuto = false;
        m_recordOverwriteConfirmed = true; // hộp thoại trên đã tự hỏi ghi đè nếu tệp tồn tại
    }
}

void DevicesTab::onScrcpyStarted()
{
    m_controlBtn->setVisible(false);
    m_switchWirelessBtn->setEnabled(false);
    m_stopBtn->setVisible(true);
    m_statusLabel->setText("✓ Đang điều khiển - cửa sổ gương màn hình sẽ mở riêng. Đóng cửa sổ đó hoặc bấm \"Dừng điều khiển\" để kết thúc.");
}

void DevicesTab::onScrcpyFinished(int exitCode)
{
    m_controlBtn->setVisible(true);
    m_stopBtn->setVisible(false);
    onRowSelectionChanged();
    m_statusLabel->setText(exitCode == 0
                               ? "Đã kết thúc phiên điều khiển."
                               : QString("⚠ Phiên điều khiển kết thúc với mã lỗi %1 (thiết bị có thể đã rút cáp, từ chối gỡ lỗi, hoặc mất kết nối không dây).").arg(exitCode));
    reloadDevices();
}

void DevicesTab::onScrcpyError(QString message)
{
    m_statusLabel->setText("⚠ " + message);
}
