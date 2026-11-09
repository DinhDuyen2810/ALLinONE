#include "ProcessTab.h"

#include "DiskUiStyle.h"
#include "ProcessTableModel.h"
#include "engine/FsSafety.h"
#include "engine/PartitionManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPainter>
#include <QPolygonF>
#include <QProcess>
#include <QProgressBar>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QThread>
#include <QVBoxLayout>

#include <deque>

// ---------------------------------------------------------------------------------------------------------------
// Thẻ chỉ số (CPU / RAM / Cấp phát / Hệ thống)
// ---------------------------------------------------------------------------------------------------------------
class MeterCard : public QFrame
{
public:
    explicit MeterCard(const QString& title, QWidget* parent = nullptr)
        : QFrame(parent)
    {
        // Chọn theo thuộc tính (không theo objectName) để mỗi thẻ vẫn đặt được tên riêng cho test/tra cứu.
        setProperty("meterCard", true);
        setStyleSheet("QFrame[meterCard=\"true\"] { background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 10px; }"
                      "QFrame[meterCard=\"true\"] QLabel { background: transparent; }");
        auto* lay = new QVBoxLayout(this);
        lay->setContentsMargins(12, 8, 12, 8);
        lay->setSpacing(3);
        m_title = new QLabel(title, this);
        m_title->setStyleSheet("color: #57606a; font-size: 11px; font-weight: 600;");
        m_value = new QLabel("-", this);
        m_value->setObjectName("meterValue");
        m_value->setStyleSheet("color: #1f2328; font-size: 15px; font-weight: 700;");
        m_bar = new QProgressBar(this);
        m_bar->setRange(0, 1000);
        m_bar->setTextVisible(false);
        m_bar->setFixedHeight(7);
        m_bar->setStyleSheet(DiskUi::thinBarStyle());
        m_sub = new QLabel(this);
        m_sub->setObjectName("meterSub");
        m_sub->setStyleSheet("color: #57606a; font-size: 11px;");
        m_sub->setWordWrap(true);
        lay->addWidget(m_title);
        lay->addWidget(m_value);
        lay->addWidget(m_bar);
        lay->addWidget(m_sub);
    }

    /// percent < 0: không có thanh.
    void setValue(const QString& main, const QString& sub, double percent)
    {
        m_value->setText(main);
        m_sub->setText(sub);
        m_bar->setVisible(percent >= 0.0);
        if (percent >= 0.0)
        {
            m_bar->setValue(static_cast<int>(qBound(0.0, percent, 100.0) * 10));
            const QString color = DiskUi::usageColor(percent);
            if (color != m_color)
            {
                m_color = color;
                m_bar->setStyleSheet(DiskUi::thinBarStyle(color));
            }
        }
    }

private:
    QLabel* m_title{nullptr};
    QLabel* m_value{nullptr};
    QLabel* m_sub{nullptr};
    QProgressBar* m_bar{nullptr};
    QString m_color;
};

// ---------------------------------------------------------------------------------------------------------------
// Đồ thị lịch sử CPU + RAM
// ---------------------------------------------------------------------------------------------------------------
class UsageHistoryWidget : public QWidget
{
public:
    explicit UsageHistoryWidget(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedHeight(112);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    void addSample(double cpu, double ram)
    {
        m_cpu.push_back(cpu);
        m_ram.push_back(ram);
        while (m_cpu.size() > kMax)
            m_cpu.pop_front();
        while (m_ram.size() > kMax)
            m_ram.pop_front();
        update();
    }
    void clear()
    {
        m_cpu.clear();
        m_ram.clear();
        update();
    }
    int sampleCount() const { return static_cast<int>(m_cpu.size()); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);
        const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        p.setPen(QPen(QColor("#d0d7de"), 1));
        p.setBrush(QColor("#ffffff"));
        p.drawRoundedRect(frame, 10, 10);

        const QRectF plot = frame.adjusted(12, 28, -12, -10);
        p.setPen(QPen(QColor("#eaeef2"), 1));
        for (int i = 0; i <= 4; ++i)
        {
            const qreal y = plot.top() + plot.height() * i / 4.0;
            p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        }

        const QColor cpuColor("#0969da");
        const QColor ramColor("#8250df");
        const auto drawSeries = [&](const std::deque<double>& v, const QColor& c) {
            if (v.size() < 2)
                return;
            const qreal dx = plot.width() / (kMax - 1);
            const qreal x0 = plot.right() - dx * static_cast<qreal>(v.size() - 1);
            QPolygonF line;
            for (size_t i = 0; i < v.size(); ++i)
                line << QPointF(x0 + dx * static_cast<qreal>(i), plot.bottom() - plot.height() * qBound(0.0, v[i], 100.0) / 100.0);
            QPolygonF area = line;
            area << QPointF(line.last().x(), plot.bottom()) << QPointF(line.first().x(), plot.bottom());
            QColor fill = c;
            fill.setAlpha(36);
            p.setPen(Qt::NoPen);
            p.setBrush(fill);
            p.drawPolygon(area);
            p.setPen(QPen(c, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            p.setBrush(Qt::NoBrush);
            p.drawPolyline(line);
        };
        drawSeries(m_ram, ramColor);
        drawSeries(m_cpu, cpuColor);

        // Chú giải kèm giá trị mới nhất.
        QFont f = font();
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        const auto legend = [&](qreal x, const QColor& c, const QString& text) {
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawRoundedRect(QRectF(x, frame.top() + 11, 10, 10), 3, 3);
            p.setPen(QColor("#1f2328"));
            p.drawText(QPointF(x + 15, frame.top() + 21), text);
        };
        legend(frame.left() + 12, cpuColor, QStringLiteral("CPU %1%").arg(m_cpu.empty() ? 0 : qRound(m_cpu.back())));
        legend(frame.left() + 110, ramColor, QStringLiteral("RAM %1%").arg(m_ram.empty() ? 0 : qRound(m_ram.back())));
        p.setPen(QColor("#8c959f"));
        QFont small = font();
        small.setPixelSize(11);
        p.setFont(small);
        p.drawText(QRectF(frame.left(), frame.top() + 8, frame.width() - 12, 16), Qt::AlignRight | Qt::AlignVCenter,
                   QStringLiteral("%1 mẫu gần nhất").arg(kMax));
    }

private:
    static constexpr int kMax = 90;
    std::deque<double> m_cpu;
    std::deque<double> m_ram;
};

// ---------------------------------------------------------------------------------------------------------------
// Đọc thanh RAM ở luồng nền (PowerShell - có thể mất vài giây)
// ---------------------------------------------------------------------------------------------------------------
class ModulesLoader : public QThread
{
    Q_OBJECT

public:
    ModulesLoader(const std::atomic_bool* cancel, QObject* parent)
        : QThread(parent), m_cancel(cancel)
    {
    }

signals:
    void loaded(QString summary);

protected:
    void run() override
    {
        QString error;
        const QList<MemoryModule> modules = ProcessMonitor::memoryModules(&error, m_cancel);
        if (!m_cancel->load())
            emit loaded(ProcessMonitor::describeMemoryModules(modules));
    }

private:
    const std::atomic_bool* m_cancel;
};

// ---------------------------------------------------------------------------------------------------------------
// ProcessTab
// ---------------------------------------------------------------------------------------------------------------
namespace
{
QString gb(qint64 bytes)
{
    return DiskUi::formatBytes(bytes);
}

QString uptimeText(qint64 ms)
{
    const qint64 totalMin = ms / 60000;
    const qint64 days = totalMin / (60 * 24);
    const qint64 hours = (totalMin / 60) % 24;
    const qint64 minutes = totalMin % 60;
    if (days > 0)
        return QStringLiteral("%1 ngày %2 giờ").arg(days).arg(hours);
    if (hours > 0)
        return QStringLiteral("%1 giờ %2 phút").arg(hours).arg(minutes);
    return QStringLiteral("%1 phút").arg(minutes);
}
} // namespace

ProcessTab::ProcessTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();

    m_sampler = new ProcessSamplerThread(this);
    m_sampler->setObjectName("processSampler");
    m_sampler->setIntervalMs(m_intervalCombo->currentData().toInt());
    connect(m_sampler, &ProcessSamplerThread::sampled, this, &ProcessTab::onSampled);
    m_sampler->start(QThread::LowPriority); // luồng ngủ cho tới khi tab hiện (setActive)
}

ProcessTab::~ProcessTab()
{
    m_cancelModules = true;
    if (m_sampler)
    {
        m_sampler->requestStop();
        m_sampler->wait(3000);
    }
    if (m_modulesLoader)
        m_modulesLoader->wait(3000);
}

void ProcessTab::stopMonitoring()
{
    m_stopped = true;
    m_cancelModules = true;
    if (m_sampler)
    {
        m_sampler->setActive(false);
        m_sampler->requestStop();
        m_sampler->wait(3000);
    }
    if (m_modulesLoader)
        m_modulesLoader->wait(3000);
}

void ProcessTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(8);

    auto* cards = new QHBoxLayout();
    cards->setSpacing(8);
    m_cpuCard = new MeterCard("CPU", this);
    m_cpuCard->setObjectName("cpuCard");
    m_ramCard = new MeterCard("RAM", this);
    m_ramCard->setObjectName("ramCard");
    m_commitCard = new MeterCard("BỘ NHỚ CẤP PHÁT (RAM + PAGEFILE)", this);
    m_commitCard->setObjectName("commitCard");
    m_systemCard = new MeterCard("HỆ THỐNG", this);
    m_systemCard->setObjectName("systemCard");
    for (MeterCard* c : {m_cpuCard, m_ramCard, m_commitCard, m_systemCard})
        cards->addWidget(c, 1);
    root->addLayout(cards);

    m_history = new UsageHistoryWidget(this);
    m_history->setObjectName("usageHistory");
    root->addWidget(m_history);

    m_ramModulesLabel = new QLabel(this);
    m_ramModulesLabel->setObjectName("ramModulesLabel");
    m_ramModulesLabel->setStyleSheet("color: #57606a; font-size: 11px;");
    m_ramModulesLabel->setTextFormat(Qt::PlainText);
    m_ramModulesLabel->setVisible(false);
    root->addWidget(m_ramModulesLabel);

    auto* controls = new QHBoxLayout();
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setObjectName("processFilter");
    m_filterEdit->setStyleSheet(DiskUi::inputStyle());
    m_filterEdit->setPlaceholderText("Lọc theo tên, PID, người dùng, đường dẫn...");
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setMinimumWidth(240);
    connect(m_filterEdit, &QLineEdit::textChanged, this, [this]() { applyFilter(); });
    controls->addWidget(m_filterEdit, 1);

    m_groupCheck = new QCheckBox("Gộp theo tên", this);
    m_groupCheck->setObjectName("processGroupCheck");
    m_groupCheck->setToolTip("Gộp các tiến trình cùng tên (vd chrome.exe) thành một dòng, cộng CPU/RAM/luồng/handle.");
    connect(m_groupCheck, &QCheckBox::toggled, this, [this]() {
        if (m_last)
            onSampled(m_last); // vẽ lại ngay với mẫu gần nhất, không đợi lần lấy mẫu kế tiếp
    });
    controls->addWidget(m_groupCheck);

    m_pauseCheck = new QCheckBox("Tạm dừng", this);
    m_pauseCheck->setObjectName("processPauseCheck");
    m_pauseCheck->setToolTip("Đóng băng danh sách để đọc/chọn dòng mà không bị nhảy vị trí.");
    connect(m_pauseCheck, &QCheckBox::toggled, this, [this]() { updateSamplingState(); });
    controls->addWidget(m_pauseCheck);

    auto* intervalLabel = new QLabel("Làm mới mỗi:", this);
    intervalLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    controls->addWidget(intervalLabel);
    m_intervalCombo = new QComboBox(this);
    m_intervalCombo->setObjectName("processIntervalCombo");
    m_intervalCombo->setStyleSheet(DiskUi::inputStyle());
    m_intervalCombo->addItem("1 giây", 1000);
    m_intervalCombo->addItem("2 giây", 2000);
    m_intervalCombo->addItem("5 giây", 5000);
    m_intervalCombo->setCurrentIndex(1);
    connect(m_intervalCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_sampler)
            m_sampler->setIntervalMs(m_intervalCombo->currentData().toInt());
    });
    controls->addWidget(m_intervalCombo);

    m_countLabel = new QLabel(this);
    m_countLabel->setObjectName("processCountLabel");
    m_countLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    controls->addWidget(m_countLabel);
    root->addLayout(controls);

    m_model = new ProcessTableModel(this);
    m_proxy = new QSortFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);
    m_proxy->setSortRole(ProcessTableModel::SortRole);
    m_proxy->setFilterKeyColumn(-1);
    m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    m_proxy->setDynamicSortFilter(true);

    m_table = new QTableView(this);
    m_table->setObjectName("processTable");
    m_table->setModel(m_proxy);
    m_table->setStyleSheet(DiskUi::viewStyle());
    m_table->setAlternatingRowColors(true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setShowGrid(false);
    m_table->setWordWrap(false);
    m_table->setSortingEnabled(true);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(26);
    QHeaderView* h = m_table->horizontalHeader();
    h->setSectionResizeMode(QHeaderView::Interactive);
    h->setStretchLastSection(true);
    h->setMinimumSectionSize(40);
    const int widths[ProcessTableModel::ColumnCount] = {170, 56, 58, 84, 84, 50, 60, 76, 140, 240};
    for (int i = 0; i < ProcessTableModel::ColumnCount; ++i)
        h->resizeSection(i, widths[i]);
    m_table->sortByColumn(ProcessTableModel::RamColumn, Qt::DescendingOrder);
    connect(m_table, &QTableView::customContextMenuRequested, this, &ProcessTab::onContextMenu);
    root->addWidget(m_table, 1);

    m_noteLabel = new QLabel(this);
    m_noteLabel->setObjectName("processNoteLabel");
    m_noteLabel->setStyleSheet("color: #8c959f; font-size: 11px;");
    m_noteLabel->setWordWrap(true);
    m_noteLabel->setTextFormat(Qt::PlainText);
    root->addWidget(m_noteLabel);
}

void ProcessTab::updateSamplingState()
{
    if (!m_sampler || m_stopped)
        return;
    m_sampler->setActive(isVisible() && !m_pauseCheck->isChecked());
}

void ProcessTab::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_sampler && !m_stopped)
        m_sampler->setActive(!m_pauseCheck->isChecked());
    if (!m_modulesRequested && !m_stopped)
    {
        m_modulesRequested = true;
        m_modulesLoader = new ModulesLoader(&m_cancelModules, this);
        connect(m_modulesLoader, &ModulesLoader::loaded, this, &ProcessTab::onModulesLoaded);
        m_modulesLoader->start(QThread::LowPriority);
    }
}

void ProcessTab::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (m_sampler && !m_stopped)
        m_sampler->setActive(false); // tab/cửa sổ không còn hiện: luồng lấy mẫu ngủ, không tốn CPU
}

void ProcessTab::onModulesLoaded(QString summary)
{
    m_ramModulesLabel->setText(summary.isEmpty() ? QString() : QStringLiteral("🧩 ") + summary);
    m_ramModulesLabel->setVisible(!summary.isEmpty());
}

quint32 ProcessTab::selectedPid() const
{
    const QModelIndex cur = m_table->currentIndex();
    return cur.isValid() ? m_proxy->data(cur, ProcessTableModel::PidRole).toUInt() : 0;
}

QString ProcessTab::selectedPath() const
{
    const QModelIndex cur = m_table->currentIndex();
    return cur.isValid() ? m_proxy->data(cur, ProcessTableModel::PathRole).toString() : QString();
}

void ProcessTab::applyFilter()
{
    m_proxy->setFilterFixedString(m_filterEdit->text().trimmed());
    m_countLabel->setText(QString("%1 / %2 tiến trình").arg(m_proxy->rowCount()).arg(m_model->rowCount()));
}

void ProcessTab::onSampled(std::shared_ptr<SystemSnapshot> snapshot)
{
    if (!snapshot || m_stopped)
        return;
    const bool fromTimer = snapshot != m_last; // false khi vẽ lại mẫu cũ (đổi chế độ gộp)
    m_last = snapshot;

    // Giữ lựa chọn + vị trí cuộn khi bảng phải dựng lại (tập tiến trình đổi: có tiến trình mới/đã thoát).
    const quint32 selPid = selectedPid();
    const int scroll = m_table->verticalScrollBar()->value();
    const bool reset = m_model->update(*snapshot, m_groupCheck->isChecked());
    if (reset)
    {
        if (selPid != 0)
        {
            for (int r = 0; r < m_proxy->rowCount(); ++r)
                if (m_proxy->index(r, 0).data(ProcessTableModel::PidRole).toUInt() == selPid)
                {
                    m_table->setCurrentIndex(m_proxy->index(r, 0));
                    m_table->selectRow(r);
                    break;
                }
        }
        m_table->verticalScrollBar()->setValue(scroll);
    }

    const MemoryStatus& m = snapshot->memory;
    m_cpuCard->setValue(QLocale().toString(snapshot->cpuPercent, 'f', 1) + " %",
                        QString("%1 lõi logic · %2 tiến trình").arg(snapshot->logicalProcessors).arg(snapshot->processCount),
                        snapshot->cpuPercent);
    m_ramCard->setValue(QString("%1 / %2").arg(gb(m.usedPhysical()), gb(m.totalPhysical)),
                        QString("Đang dùng %1% · còn trống %2 · bộ nhớ đệm hệ thống %3")
                            .arg(QString::number(m.usedPercent(), 'f', 0), gb(m.availablePhysical), gb(m.systemCache)),
                        m.usedPercent());
    m_commitCard->setValue(QString("%1 / %2").arg(gb(m.commitUsed), gb(m.commitLimit)),
                           QString("Đỉnh %1 · paged pool nhân %2 · non-paged pool nhân %3")
                               .arg(gb(m.commitPeak), gb(m.kernelPaged), gb(m.kernelNonpaged)),
                           m.commitPercent());
    m_systemCard->setValue(QString("%1 tiến trình").arg(QLocale().toString(snapshot->processCount)),
                           QString("%1 luồng · %2 handle · máy đã chạy %3")
                               .arg(QLocale().toString(snapshot->threadCount), QLocale().toString(snapshot->handleCount), uptimeText(snapshot->uptimeMs)),
                           -1.0);
    if (fromTimer)
        m_history->addSample(snapshot->cpuPercent, m.usedPercent());

    QStringList notes;
    if (!snapshot->nativeApi)
        notes << "⚠ Đang dùng chế độ dự phòng: thiếu RAM riêng/handle/I-O của một số tiến trình hệ thống.";
    if (!PartitionManager::isElevated())
        notes << "ℹ Chạy KHÔNG có quyền Administrator: vẫn thấy mọi tiến trình và số liệu RAM/CPU, nhưng đường dẫn và người dùng của một số tiến trình hệ thống có thể để trống.";
    m_noteLabel->setText(notes.join("\n"));
    m_noteLabel->setVisible(!notes.isEmpty());
    applyFilter();
}

void ProcessTab::openSelectedInExplorer()
{
    const QString path = selectedPath();
    if (path.isEmpty() || !QFileInfo::exists(path))
        return;
    const QString winDir = FsSafety::windowsDirectory();
    if (winDir.isEmpty())
        return;
    // Đường dẫn tuyệt đối tới explorer.exe - xem LargeFilesTab::onOpenFolderClicked().
    QProcess::startDetached(QDir::toNativeSeparators(winDir + "/explorer.exe"), {"/select,", QDir::toNativeSeparators(path)});
}

void ProcessTab::onContextMenu(const QPoint& pos)
{
    const QModelIndex idx = m_table->indexAt(pos);
    if (!idx.isValid())
        return;
    m_table->setCurrentIndex(m_proxy->index(idx.row(), 0));
    m_table->selectRow(idx.row());
    const QString path = selectedPath();
    QMenu menu(this);
    QAction* open = menu.addAction("📂 Mở thư mục chứa tệp");
    open->setEnabled(!path.isEmpty() && QFileInfo::exists(path));
    QAction* copyPath = menu.addAction("📋 Sao chép đường dẫn");
    copyPath->setEnabled(!path.isEmpty());
    QAction* copyName = menu.addAction("📋 Sao chép tên");
    QAction* copyPid = menu.addAction("📋 Sao chép PID");
    copyPid->setEnabled(selectedPid() != 0);
    QAction* chosen = menu.exec(m_table->viewport()->mapToGlobal(pos));
    if (!chosen)
        return;
    if (chosen == open)
        openSelectedInExplorer();
    else if (chosen == copyPath)
        QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    else if (chosen == copyName)
    {
        const ProcessTableModel::Row* row = m_model->rowAt(m_proxy->mapToSource(idx).row());
        if (row)
            QApplication::clipboard()->setText(row->name);
    }
    else if (chosen == copyPid)
        QApplication::clipboard()->setText(QString::number(selectedPid()));
}

#include "ProcessTab.moc"
