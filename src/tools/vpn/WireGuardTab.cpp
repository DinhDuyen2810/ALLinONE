#include "WireGuardTab.h"

#include "VpnUiStyle.h"
#include "core/ToolManager.h"
#include "core/WinElevation.h"
#include "engine/WireGuardController.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QThread>
#include <QVBoxLayout>

#include <memory>

WireGuardTab::WireGuardTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    updateElevationBanner();
    updateInstalledBanner();
    reloadTunnels();
}

WireGuardTab::~WireGuardTab()
{
    // Mọi thao tác nền ở tab này (import/gỡ/bật/tắt) chỉ là một tiến trình ngắn (wireguard.exe/sc.exe,
    // timeout 15s) - không có trạng thái "đang dò/liên kết" cần hủy an toàn như VpnConnector, chỉ cần
    // chờ luồng hiện tại (nếu có) thoát hẳn trước khi hủy đối tượng QThread cùng tab này.
    if (m_backgroundThread)
        m_backgroundThread->wait();
}

void WireGuardTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* hint = new QLabel(
        "Quản lý tunnel WireGuard bằng WireGuard for Windows đã cài trên máy (không đóng gói driver nào "
        "- xem https://www.wireguard.com/install/). Nhập tệp .conf lấy từ nhà cung cấp VPN bạn đã có "
        "tài khoản (vd ProtonVPN/Windscribe/Mullvad bản free đều xuất được file WireGuard chuẩn) hoặc "
        "VPN tự dựng/cơ quan - ứng dụng không đi kèm máy chủ WireGuard nào.",
        this);
    hint->setWordWrap(true);
    hint->setStyleSheet(VpnUi::bannerStyle("info"));
    root->addWidget(hint);

    m_elevationBanner = new QLabel(this);
    m_elevationBanner->setWordWrap(true);
    root->addWidget(m_elevationBanner);
    m_relaunchBtn = new QPushButton("🔓 Chạy lại với quyền Administrator", this);
    m_relaunchBtn->setStyleSheet(VpnUi::buttonStyle());
    m_relaunchBtn->setCursor(Qt::PointingHandCursor);
    connect(m_relaunchBtn, &QPushButton::clicked, this, &WireGuardTab::onRelaunchElevatedClicked);
    root->addWidget(m_relaunchBtn);

    m_installBanner = new QLabel(this);
    m_installBanner->setWordWrap(true);
    m_installBanner->setTextFormat(Qt::PlainText);
    root->addWidget(m_installBanner);

    auto* topRow = new QHBoxLayout();
    m_refreshBtn = new QPushButton("🔄 Làm mới", this);
    m_refreshBtn->setStyleSheet(VpnUi::buttonStyle());
    m_refreshBtn->setCursor(Qt::PointingHandCursor);
    connect(m_refreshBtn, &QPushButton::clicked, this, &WireGuardTab::onRefreshClicked);
    topRow->addWidget(m_refreshBtn);

    m_importBtn = new QPushButton("📥 Nhập .conf...", this);
    m_importBtn->setStyleSheet(VpnUi::buttonStyle());
    m_importBtn->setCursor(Qt::PointingHandCursor);
    connect(m_importBtn, &QPushButton::clicked, this, &WireGuardTab::onImportClicked);
    topRow->addWidget(m_importBtn);

    m_removeBtn = new QPushButton("🗑 Gỡ tunnel", this);
    m_removeBtn->setStyleSheet(VpnUi::dangerButtonStyle());
    m_removeBtn->setCursor(Qt::PointingHandCursor);
    m_removeBtn->setEnabled(false);
    connect(m_removeBtn, &QPushButton::clicked, this, &WireGuardTab::onRemoveClicked);
    topRow->addWidget(m_removeBtn);
    topRow->addStretch();
    root->addLayout(topRow);

    m_table = new QTableWidget(0, 4, this);
    m_table->setHorizontalHeaderLabels({"Tên tunnel", "Quốc gia", "Máy chủ", "Trạng thái"});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_table->verticalHeader()->setVisible(false);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setStyleSheet(VpnUi::tableStyle());
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &WireGuardTab::onRowSelectionChanged);
    root->addWidget(m_table, 1);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_statusLabel);

    auto* bottomRow = new QHBoxLayout();
    m_startBtn = new QPushButton("▶ Bật", this);
    m_startBtn->setStyleSheet(VpnUi::primaryButtonStyle());
    m_startBtn->setCursor(Qt::PointingHandCursor);
    m_startBtn->setEnabled(false);
    connect(m_startBtn, &QPushButton::clicked, this, &WireGuardTab::onStartClicked);
    bottomRow->addWidget(m_startBtn);

    m_stopBtn = new QPushButton("⏹ Tắt", this);
    m_stopBtn->setStyleSheet(VpnUi::dangerButtonStyle());
    m_stopBtn->setCursor(Qt::PointingHandCursor);
    m_stopBtn->setEnabled(false);
    connect(m_stopBtn, &QPushButton::clicked, this, &WireGuardTab::onStopClicked);
    bottomRow->addWidget(m_stopBtn);
    bottomRow->addStretch();
    root->addLayout(bottomRow);
}

void WireGuardTab::updateElevationBanner()
{
    const bool elevated = WinElevation::isElevated();
    m_elevationBanner->setVisible(!elevated);
    m_relaunchBtn->setVisible(!elevated);
    if (!elevated)
    {
        m_elevationBanner->setText(
            "🔒 Đang chạy KHÔNG có quyền Administrator - WireGuard cần quyền này cho MỌI thao tác (cài/"
            "gỡ/bật/tắt tunnel là dịch vụ Windows). Bấm nút dưới để chạy lại.");
        m_elevationBanner->setStyleSheet(VpnUi::bannerStyle("warn"));
    }
}

void WireGuardTab::updateInstalledBanner()
{
    const bool installed = WireGuardController::isInstalled();
    m_installBanner->setVisible(!installed);
    if (!installed)
    {
        m_installBanner->setText(
            "⚠ Chưa tìm thấy WireGuard for Windows trên máy. Tải và cài bản chính thức tại "
            "https://www.wireguard.com/install/ rồi bấm \"Làm mới\".");
        m_installBanner->setStyleSheet(VpnUi::bannerStyle("danger"));
    }
    m_importBtn->setEnabled(installed);
}

void WireGuardTab::runInBackground(const QString& statusText, std::function<void()> work, std::function<void()> done)
{
    // Cùng mẫu VpnTab::runInBackground(): wireguard.exe/sc.exe mất tới vài giây (timeout 15s) - không
    // chạy thẳng trên luồng giao diện.
    m_statusLabel->setText(statusText);
    QThread* thread = QThread::create(std::move(work));
    thread->setParent(this);
    m_backgroundThread = thread;
    connect(thread, &QThread::finished, this, [this, thread, done = std::move(done)]() {
        if (m_backgroundThread == thread)
            m_backgroundThread = nullptr;
        thread->deleteLater();
        done();
        updateButtons();
    });
    thread->start();
    updateButtons();
}

void WireGuardTab::reloadTunnels()
{
    if (m_backgroundThread || !WireGuardController::isInstalled())
        return;

    auto tunnels = std::make_shared<QList<WireGuardTunnel>>();
    auto error = std::make_shared<QString>();
    runInBackground(
        "⏳ Đang đọc danh sách tunnel...",
        [tunnels, error]() { *tunnels = WireGuardController::listTunnels(error.get()); },
        [this, tunnels, error]() { populateTable(*tunnels, *error); });
}

void WireGuardTab::populateTable(const QList<WireGuardTunnel>& tunnels, const QString& error)
{
    m_tunnels = tunnels;
    if (!error.isEmpty())
        m_statusLabel->setText("⚠ " + error);
    else
        m_statusLabel->setText(QString("%1 tunnel đã cài.").arg(tunnels.size()));

    m_table->setRowCount(tunnels.size());
    for (int i = 0; i < tunnels.size(); ++i)
    {
        const WireGuardTunnel& t = tunnels[i];
        auto makeItem = [](const QString& text) {
            auto* item = new QTableWidgetItem(text);
            item->setFlags(item->flags() & ~Qt::ItemIsEditable);
            return item;
        };
        m_table->setItem(i, 0, makeItem(t.name));
        m_table->setItem(i, 1, makeItem(t.countryLabel));
        m_table->setItem(i, 2, makeItem(t.endpoint));
        m_table->setItem(i, 3, makeItem(t.isRunning ? "Đang chạy" : "Đã tắt"));
    }
    onRowSelectionChanged();
}

const WireGuardTunnel* WireGuardTab::selectedTunnel() const
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_tunnels.size())
        return nullptr;
    return &m_tunnels[row];
}

void WireGuardTab::onRowSelectionChanged()
{
    updateButtons();
}

void WireGuardTab::updateButtons()
{
    const bool busy = m_backgroundThread != nullptr;
    const bool installed = WireGuardController::isInstalled();
    const WireGuardTunnel* sel = selectedTunnel();

    m_refreshBtn->setEnabled(!busy && installed);
    m_importBtn->setEnabled(!busy && installed);
    m_removeBtn->setEnabled(!busy && sel != nullptr);
    m_startBtn->setEnabled(!busy && sel != nullptr && !sel->isRunning);
    m_stopBtn->setEnabled(!busy && sel != nullptr && sel->isRunning);
}

void WireGuardTab::onRefreshClicked()
{
    reloadTunnels();
}

void WireGuardTab::onImportClicked()
{
    const QString confPath = QFileDialog::getOpenFileName(this, "Nhập tunnel WireGuard", QString(), "WireGuard config (*.conf)");
    if (confPath.isEmpty())
        return;

    // Hộp thoại nhỏ xin tên tunnel (prefill từ tên tệp) + nhãn quốc gia - cùng tinh thần ConnectDialog/
    // ProfilesTab::onShareQrClicked() (dựng tại chỗ, không thêm tệp mới riêng).
    QDialog dlg(this);
    dlg.setWindowTitle("Nhập tunnel WireGuard");
    dlg.setStyleSheet("QDialog { background-color: #f6f8fa; } QLabel { color: #1f2328; background: transparent; }" + VpnUi::inputStyle());
    auto* layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(12);

    auto* form = new QFormLayout();
    auto* nameEdit = new QLineEdit(&dlg);
    nameEdit->setText(WireGuardController::internal::sanitizeTunnelName(QFileInfo(confPath).completeBaseName()));
    form->addRow("Tên tunnel:", nameEdit);
    auto* countryEdit = new QLineEdit(&dlg);
    countryEdit->setPlaceholderText("Vd: Hà Lan - ProtonVPN (chỉ để hiển thị)");
    form->addRow("Nhãn quốc gia:", countryEdit);
    layout->addLayout(form);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    buttons->button(QDialogButtonBox::Ok)->setText("Nhập");
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString desiredName = nameEdit->text();
    const QString countryLabel = countryEdit->text();

    auto resultName = std::make_shared<QString>();
    auto error = std::make_shared<QString>();
    auto ok = std::make_shared<bool>(false);
    runInBackground(
        "⏳ Đang nhập tunnel (cần quyền Administrator)...",
        [confPath, desiredName, countryLabel, resultName, error, ok]() {
            *ok = WireGuardController::importTunnel(confPath, desiredName, countryLabel, resultName.get(), error.get());
        },
        [this, resultName, error, ok]() {
            if (!*ok)
            {
                QMessageBox::critical(this, "Nhập tunnel thất bại", error->isEmpty() ? "Không nhập được." : *error);
                m_statusLabel->setText("⚠ " + *error);
                return;
            }
            m_statusLabel->setText("✓ Đã nhập và bật tunnel \"" + *resultName + "\".");
            reloadTunnels();
        });
}

void WireGuardTab::onStartClicked()
{
    const WireGuardTunnel* sel = selectedTunnel();
    if (!sel)
        return;
    const QString name = sel->name;

    auto error = std::make_shared<QString>();
    auto ok = std::make_shared<bool>(false);
    runInBackground(
        "⏳ Đang bật tunnel \"" + name + "\"...",
        [name, error, ok]() { *ok = WireGuardController::startTunnel(name, error.get()); },
        [this, name, error, ok]() {
            if (!*ok)
            {
                QMessageBox::critical(this, "Không bật được", error->isEmpty() ? "Không bật được tunnel." : *error);
                m_statusLabel->setText("⚠ " + *error);
                return;
            }
            m_statusLabel->setText("✓ Đã bật tunnel \"" + name + "\".");
            reloadTunnels();
        });
}

void WireGuardTab::onStopClicked()
{
    const WireGuardTunnel* sel = selectedTunnel();
    if (!sel)
        return;
    const QString name = sel->name;

    auto error = std::make_shared<QString>();
    auto ok = std::make_shared<bool>(false);
    runInBackground(
        "⏳ Đang tắt tunnel \"" + name + "\"...",
        [name, error, ok]() { *ok = WireGuardController::stopTunnel(name, error.get()); },
        [this, name, error, ok]() {
            if (!*ok)
            {
                QMessageBox::critical(this, "Không tắt được", error->isEmpty() ? "Không tắt được tunnel." : *error);
                m_statusLabel->setText("⚠ " + *error);
                return;
            }
            m_statusLabel->setText("✓ Đã tắt tunnel \"" + name + "\".");
            reloadTunnels();
        });
}

void WireGuardTab::onRemoveClicked()
{
    const WireGuardTunnel* sel = selectedTunnel();
    if (!sel)
        return;
    const QString name = sel->name;

    if (QMessageBox::question(this, "Gỡ tunnel", QString("Gỡ hẳn tunnel \"%1\"? Cấu hình đã lưu trong WireGuard sẽ bị xóa.").arg(name),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    auto error = std::make_shared<QString>();
    auto ok = std::make_shared<bool>(false);
    runInBackground(
        "⏳ Đang gỡ tunnel \"" + name + "\"...",
        [name, error, ok]() { *ok = WireGuardController::removeTunnel(name, error.get()); },
        [this, name, error, ok]() {
            if (!*ok)
            {
                QMessageBox::critical(this, "Không gỡ được", error->isEmpty() ? "Không gỡ được tunnel." : *error);
                m_statusLabel->setText("⚠ " + *error);
                return;
            }
            m_statusLabel->setText("✓ Đã gỡ tunnel \"" + name + "\".");
            reloadTunnels();
        });
}

void WireGuardTab::onRelaunchElevatedClicked()
{
    if (m_backgroundThread)
    {
        QMessageBox::information(this, "Chạy lại với quyền Quản trị",
                                 "Đang có thao tác WireGuard đang chạy - đợi xong (vài giây) rồi thử lại.");
        return;
    }
    // qApp->quit() ở cuối hàm này KHÔNG tự gọi closeEvent() của các cửa sổ tool KHÁC đang mở - nếu một cửa
    // sổ khác (vd Disk Cleanup) đang đổi kích thước phân vùng thật, buộc thoát ngay bây giờ sẽ TerminateProcess
    // giữa chừng một thao tác không an toàn để hủy - CHẶN trước, cùng mẫu WebProtectionTab/MalwareScanTab.
    QString busyReason;
    if (ToolManager::instance().anyToolWindowBusy(&busyReason))
    {
        QMessageBox::warning(this, "Không thể chạy lại lúc này",
            "Đang có một thao tác không an toàn để hủy giữa chừng (" + busyReason + ") - chạy lại ứng "
            "dụng lúc này có thể làm hỏng dữ liệu. Vui lòng đợi thao tác đó hoàn tất rồi thử lại.");
        return;
    }

    QString error;
    if (!WinElevation::relaunchElevated(&error))
    {
        QMessageBox::warning(this, "Chạy lại với quyền Quản trị", error.isEmpty() ? "Không thực hiện được." : error);
        return;
    }
    qApp->quit();
}
