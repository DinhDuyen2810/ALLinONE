#include "SpeedTestTab.h"

#include "WifiUiStyle.h"
#include "engine/SpeedTestRunner.h"
#include "engine/WlanController.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

SpeedTestTab::SpeedTestTab(WlanController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    buildUi();

    m_runner = new SpeedTestRunner(this);
    connect(m_runner, &SpeedTestRunner::phaseChanged, this, [this](const QString& phase) {
        m_phaseLabel->setStyleSheet("color: #57606a; font-size: 12px;");
        m_phaseLabel->setText(phase);
    });
    connect(m_runner, &SpeedTestRunner::pingUpdated, this, [this](double, double, double avg, double, double jitter) {
        m_pingValue->setText(QString::number(avg, 'f', 0));
        m_jitterValue->setText(QString::number(jitter, 'f', 1));
    });
    connect(m_runner, &SpeedTestRunner::downloadProgress, this, [this](double currentMbps, qint64) {
        m_downloadValue->setText(QString::number(currentMbps, 'f', 1));
    });
    connect(m_runner, &SpeedTestRunner::downloadFinished, this, [this](double finalMbps) {
        m_downloadValue->setText(QString::number(finalMbps, 'f', 1));
    });
    connect(m_runner, &SpeedTestRunner::uploadProgress, this, [this](double currentMbps, qint64) {
        m_uploadValue->setText(QString::number(currentMbps, 'f', 1));
    });
    connect(m_runner, &SpeedTestRunner::uploadFinished, this, [this](double finalMbps) {
        m_uploadValue->setText(QString::number(finalMbps, 'f', 1));
    });
    connect(m_runner, &SpeedTestRunner::errorOccurred, this, [this](const QString& message) {
        m_phaseLabel->setStyleSheet("color: #cf222e; font-size: 12px; font-weight: bold;");
        m_phaseLabel->setText("⚠ " + message);
    });
    connect(m_runner, &SpeedTestRunner::finished, this, [this] {
        m_startStopBtn->setText("▶ Bắt đầu đo");
        m_startStopBtn->setStyleSheet(WifiUi::primaryButtonStyle());
        m_progressBar->setVisible(false);
        if (!m_phaseLabel->text().startsWith("⚠"))
        {
            m_phaseLabel->setStyleSheet("color: #1a7f37; font-size: 12px; font-weight: bold;");
            m_phaseLabel->setText("✓ Hoàn tất.");
        }
        updateLinkRateLabel();
    });
}

QWidget* SpeedTestTab::makeMetricCard(const QString& title, QLabel** valueLabelOut, const QString& unit)
{
    auto* card = new QWidget(this);
    card->setStyleSheet(WifiUi::cardStyle());
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(4);

    auto* titleLabel = new QLabel(title, card);
    titleLabel->setStyleSheet("color: #57606a; font-size: 11px; font-weight: bold; letter-spacing: 1px; border: none; background: transparent;");
    layout->addWidget(titleLabel);

    auto* valueRow = new QHBoxLayout();
    auto* value = new QLabel("—", card);
    value->setStyleSheet("color: #0969da; font-size: 28px; font-weight: bold; border: none; background: transparent;");
    auto* unitLabel = new QLabel(unit, card);
    unitLabel->setStyleSheet("color: #8c959f; font-size: 12px; border: none; background: transparent;");
    unitLabel->setAlignment(Qt::AlignBottom);
    valueRow->addWidget(value);
    valueRow->addWidget(unitLabel);
    valueRow->addStretch();
    layout->addLayout(valueRow);

    *valueLabelOut = value;
    return card;
}

void SpeedTestTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(14);

    auto* intro = new QLabel(
        "Đo tốc độ mạng thực tế qua Internet (ping, jitter, tốc độ tải xuống/tải lên), "
        "dùng máy chủ đo tốc độ công khai của Cloudflare. Kết quả phụ thuộc vào tình trạng mạng thực tế, "
        "không chỉ riêng WiFi.", this);
    intro->setWordWrap(true);
    intro->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(intro);

    m_linkRateLabel = new QLabel(this);
    m_linkRateLabel->setWordWrap(true);
    m_linkRateLabel->setStyleSheet(WifiUi::bannerStyle(false));
    root->addWidget(m_linkRateLabel);

    auto* grid = new QGridLayout();
    grid->setSpacing(12);
    grid->addWidget(makeMetricCard("PING", &m_pingValue, "ms"), 0, 0);
    grid->addWidget(makeMetricCard("JITTER", &m_jitterValue, "ms"), 0, 1);
    grid->addWidget(makeMetricCard("TẢI XUỐNG (DOWNLOAD)", &m_downloadValue, "Mbps"), 1, 0);
    grid->addWidget(makeMetricCard("TẢI LÊN (UPLOAD)", &m_uploadValue, "Mbps"), 1, 1);
    root->addLayout(grid);

    m_phaseLabel = new QLabel("Sẵn sàng đo.", this);
    m_phaseLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    root->addWidget(m_phaseLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0); // dạng chạy liên tục, không biết trước thời lượng chính xác
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background-color: #eaeef2; border-radius: 3px; }"
        "QProgressBar::chunk { background-color: #0969da; border-radius: 3px; }");
    root->addWidget(m_progressBar);

    auto* btnRow = new QHBoxLayout();
    m_startStopBtn = new QPushButton("▶ Bắt đầu đo", this);
    m_startStopBtn->setStyleSheet(WifiUi::primaryButtonStyle());
    m_startStopBtn->setCursor(Qt::PointingHandCursor);
    m_startStopBtn->setFixedHeight(38);
    connect(m_startStopBtn, &QPushButton::clicked, this, &SpeedTestTab::onStartStopClicked);
    btnRow->addWidget(m_startStopBtn);
    btnRow->addStretch();
    root->addLayout(btnRow);

    root->addStretch();
}

void SpeedTestTab::setAdapter(const QString& guid)
{
    m_adapterGuid = guid;
    updateLinkRateLabel();
}

void SpeedTestTab::stopIfRunning()
{
    if (m_runner && m_runner->isRunning())
        m_runner->stop();
}

void SpeedTestTab::updateLinkRateLabel()
{
    if (m_adapterGuid.isEmpty())
        return;

    const WifiCurrentConnection cur = m_controller->currentConnection(m_adapterGuid);
    if (cur.isConnected)
    {
        m_linkRateLabel->setStyleSheet(WifiUi::bannerStyle(true));
        m_linkRateLabel->setText(QString("📶 Đang dùng WiFi \"%1\"  •  Tốc độ liên kết lý thuyết (PHY): ↓%2 Mbps ↑%3 Mbps "
                                        "— đây là tốc độ tối đa giữa máy và router, không phải tốc độ Internet thật.")
                                      .arg(cur.ssid).arg(cur.rxRateMbps, 0, 'f', 0).arg(cur.txRateMbps, 0, 'f', 0));
    }
    else
    {
        m_linkRateLabel->setStyleSheet(WifiUi::bannerStyle(false));
        m_linkRateLabel->setText("○ Chưa kết nối WiFi. Kết quả đo bên dưới phản ánh kết nối mạng hiện có (ví dụ: dây LAN) nếu có.");
    }
}

void SpeedTestTab::resetMetrics()
{
    for (QLabel* l : {m_pingValue, m_jitterValue, m_downloadValue, m_uploadValue})
        l->setText("—");
}

void SpeedTestTab::onStartStopClicked()
{
    if (m_runner->isRunning())
    {
        m_runner->stop();
        m_startStopBtn->setEnabled(false);
        m_phaseLabel->setText("Đang dừng...");
        QTimer::singleShot(50, this, [this] { m_startStopBtn->setEnabled(true); });
        return;
    }

    resetMetrics();
    updateLinkRateLabel();
    m_phaseLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    m_phaseLabel->setText("Đang chuẩn bị...");
    m_progressBar->setVisible(true);
    m_startStopBtn->setText("■ Dừng lại");
    m_startStopBtn->setStyleSheet(WifiUi::dangerButtonStyle());
    m_runner->start();
}
