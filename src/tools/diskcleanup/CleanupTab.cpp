#include "CleanupTab.h"

#include "DiskCleanupWindow.h"
#include "DiskUiStyle.h"
#include "core/ToolManager.h"
#include "engine/CategoryRegistry.h"
#include "engine/CleanupExecutor.h"
#include "engine/CleanupSystem.h"
#include "engine/DiskSpaceInfo.h"
#include "engine/PartitionManager.h"
#include "ui/widgets/FlowLayout.h"

#include <QApplication>
#include <QCheckBox>
#include <QColor>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include <algorithm>

namespace
{
enum Column
{
    ColName = 0,
    ColRisk = 1,
    ColCount = 2,
    ColSize = 3,
    ColNote = 4,
};

constexpr int kRoleCategory = Qt::UserRole;      // int(CleanupCategoryId)
constexpr int kRoleKind = Qt::UserRole + 1;      // "cat" | "item" | "source"
constexpr int kRoleItemIndex = Qt::UserRole + 2; // chỉ số trong m_itemsByCategory[id]

QString nativePath(const QString& p)
{
    return QDir::toNativeSeparators(p);
}
} // namespace

CleanupTab::CleanupTab(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
    reloadDriveOverview();

    m_scanner = new CleanupScanner(this);
    connect(m_scanner, &CleanupScanner::categoryStarted, this, &CleanupTab::onCategoryStarted);
    connect(m_scanner, &CleanupScanner::itemFound, this, &CleanupTab::onItemFound);
    connect(m_scanner, &CleanupScanner::categoryFinished, this, &CleanupTab::onCategoryFinished);
    connect(m_scanner, &CleanupScanner::scanFinished, this, &CleanupTab::onScanFinished);
    connect(m_scanner, &CleanupScanner::scanStopped, this, &CleanupTab::onScanStopped);

    m_executor = new CleanupExecutor(this);
    connect(m_executor, &CleanupExecutor::executionFinished, this, &CleanupTab::onExecutionFinished);
    connect(m_executor, &CleanupExecutor::progress, this, [this](qint64 done, qint64 total) {
        if (total <= 0)
            return;
        m_progressBar->setRange(0, 1000);
        m_progressBar->setValue(static_cast<int>(qBound(qint64(0), done * 1000 / total, qint64(1000))));
    });
}

CleanupTab::~CleanupTab()
{
    if (m_scanner && m_scanner->isRunning())
    {
        m_scanner->requestStop();
        m_scanner->wait(3000);
    }
    // Lệnh Shell (Thùng rác) xử lý theo lô, không có móc dừng giữa chừng - bình thường không tới đây trong lúc
    // đang dọn: DiskCleanupWindow::closeEvent() đã CHẶN đóng cửa sổ hẳn khi đang dọn dẹp - đây chỉ là lưới an
    // toàn cuối cùng (phần xóa từng tệp thì dừng ngay theo requestStop()).
    if (m_executor && m_executor->isRunning())
    {
        m_executor->requestStop();
        m_executor->wait();
    }
}

bool CleanupTab::isCleaningNow() const
{
    return m_executor && m_executor->isRunning();
}

bool CleanupTab::isBusy() const
{
    return m_scanning || isCleaningNow();
}

void CleanupTab::stopScanIfRunning()
{
    if (m_scanner && m_scanner->isRunning())
        m_scanner->requestStop();
}

void CleanupTab::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 10, 10, 10);
    root->setSpacing(10);

    auto* overviewLabel = new QLabel("DUNG LƯỢNG Ổ ĐĨA", this);
    overviewLabel->setStyleSheet("color: #0969da; font-size: 11px; font-weight: bold; letter-spacing: 1.5px;");
    root->addWidget(overviewLabel);

    m_driveOverview = new QWidget(this);
    m_driveOverviewLayout = new FlowLayout(m_driveOverview, 0, 10, 10);
    root->addWidget(m_driveOverview);

    auto* catLabel = new QLabel("HẠNG MỤC DỌN DẸP", this);
    catLabel->setStyleSheet("color: #0969da; font-size: 11px; font-weight: bold; letter-spacing: 1.5px; margin-top: 6px;");
    root->addWidget(catLabel);

    auto* adminRow = new QHBoxLayout();
    m_adminBanner = new QLabel(this);
    m_adminBanner->setObjectName("cleanupAdminBanner");
    m_adminBanner->setWordWrap(true);
    m_adminBanner->setStyleSheet(DiskUi::bannerStyle("warn"));
    m_adminBanner->setText("🔒 Đang chạy KHÔNG có quyền Administrator - các hạng mục của Windows (cập nhật, tệp tạm hệ "
                           "thống, dump, Prefetch...) không được tick sẵn vì sẽ không xóa được.");
    m_adminBanner->setVisible(false);
    adminRow->addWidget(m_adminBanner, 1);
    m_relaunchBtn = new QPushButton("Chạy lại với quyền Quản trị...", this);
    m_relaunchBtn->setObjectName("cleanupRelaunchButton");
    m_relaunchBtn->setVisible(false);
    connect(m_relaunchBtn, &QPushButton::clicked, this, &CleanupTab::onRelaunchElevatedClicked);
    adminRow->addWidget(m_relaunchBtn);
    root->addLayout(adminRow);

    m_tree = new QTreeWidget(this);
    m_tree->setObjectName("cleanupCategoryTree");
    m_tree->setColumnCount(5);
    m_tree->setHeaderLabels({"Hạng mục", "Mức độ", "Số mục", "Dung lượng", "Ghi chú"});
    // QTreeWidget mặc định kéo giãn CỘT CUỐI - để cột tên giãn, các cột số vừa nội dung (xem mục 4f).
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(ColName, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(ColRisk, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(ColCount, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(ColSize, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(ColNote, QHeaderView::Interactive);
    m_tree->header()->resizeSection(ColNote, 300);
    m_tree->setSelectionMode(QAbstractItemView::NoSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_tree->setTextElideMode(Qt::ElideMiddle);
    m_tree->setUniformRowHeights(true);
    m_tree->setStyleSheet(DiskUi::tableStyle());
    connect(m_tree, &QTreeWidget::itemChanged, this, &CleanupTab::onTreeItemChanged);
    root->addWidget(m_tree, 1);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(6);
    m_progressBar->setVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { background-color: #eaeef2; border-radius: 3px; }"
        "QProgressBar::chunk { background-color: #0969da; border-radius: 3px; }");
    root->addWidget(m_progressBar);

    m_statusLabel = new QLabel("Bấm \"Quét\" để tìm tệp có thể dọn dẹp.", this);
    m_statusLabel->setStyleSheet("color: #57606a; font-size: 12px;");
    m_statusLabel->setWordWrap(true);
    root->addWidget(m_statusLabel);

    auto* bottomRow = new QHBoxLayout();
    m_scanBtn = new QPushButton("🔍 Quét", this);
    m_scanBtn->setStyleSheet(DiskUi::primaryButtonStyle());
    m_scanBtn->setCursor(Qt::PointingHandCursor);
    connect(m_scanBtn, &QPushButton::clicked, this, &CleanupTab::onScanClicked);
    bottomRow->addWidget(m_scanBtn);

    m_cleanBtn = new QPushButton("🧹 Dọn dẹp mục đã chọn", this);
    m_cleanBtn->setStyleSheet(DiskUi::dangerButtonStyle());
    m_cleanBtn->setCursor(Qt::PointingHandCursor);
    m_cleanBtn->setEnabled(false);
    connect(m_cleanBtn, &QPushButton::clicked, this, &CleanupTab::onCleanClicked);
    bottomRow->addWidget(m_cleanBtn);

    // Bộ nhớ đệm/tệp tạm LUÔN xóa hẳn (vào Thùng rác không giải phóng byte nào); ô này chỉ quyết định với TỆP CỦA
    // NGƯỜI DÙNG (bộ cài cũ, tệp tải về, Windows.old, thư mục mồ côi) - mặc định vào Thùng rác.
    m_permanentCheck = new QCheckBox("Xóa vĩnh viễn cả tệp của bạn (bỏ qua Thùng rác)", this);
    m_permanentCheck->setToolTip("Bộ nhớ đệm, tệp tạm, dump, nhật ký (tự tạo lại) luôn được xóa hẳn để thật sự giải phóng "
                                 "dung lượng - đưa chúng vào Thùng rác không giải phóng byte nào.\nÔ này chỉ áp dụng cho TỆP "
                                 "CỦA BẠN (bộ cài cũ, tệp tải về, Windows.old, thư mục còn sót): mặc định chúng vào Thùng rác "
                                 "để khôi phục được.");
    m_permanentCheck->setStyleSheet("color: #57606a; font-size: 11px;");
    bottomRow->addWidget(m_permanentCheck);

    bottomRow->addStretch();
    m_summaryLabel = new QLabel("Đã chọn: 0 B", this);
    m_summaryLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 13px;");
    bottomRow->addWidget(m_summaryLabel);
    root->addLayout(bottomRow);
}

void CleanupTab::reloadDriveOverview()
{
    QLayoutItem* child;
    while ((child = m_driveOverviewLayout->takeAt(0)) != nullptr)
    {
        delete child->widget();
        delete child;
    }

    for (const DriveSpaceInfo& d : DiskSpaceInfo::listDrives())
    {
        auto* card = new QWidget(m_driveOverview);
        // FlowLayout định kích thước mỗi thẻ theo sizeHint() của nó - QProgressBar co giãn ngang theo
        // mặc định nên sizeHint() của card một mình sẽ rất hẹp nếu không ấn định độ rộng rõ ràng.
        card->setFixedWidth(220);
        card->setStyleSheet("background-color: #ffffff; border: 1px solid #d0d7de; border-radius: 10px;");
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(14, 10, 14, 10);
        layout->setSpacing(4);

        auto* nameLabel = new QLabel(QString("%1 (%2)").arg(d.displayName, d.rootPath), card);
        nameLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px; border: none; background: transparent;");
        layout->addWidget(nameLabel);

        auto* bar = new QProgressBar(card);
        bar->setRange(0, 100);
        bar->setValue(static_cast<int>(d.usedRatio() * 100));
        bar->setTextVisible(false);
        bar->setFixedHeight(8);
        const QString barColor = d.usedRatio() > 0.9 ? "#cf222e" : (d.usedRatio() > 0.75 ? "#9a6700" : "#1f883d");
        bar->setStyleSheet(QString("QProgressBar { background-color: #eaeef2; border-radius: 4px; } "
                                   "QProgressBar::chunk { background-color: %1; border-radius: 4px; }")
                               .arg(barColor));
        layout->addWidget(bar);

        auto* detailLabel = new QLabel(QString("Còn trống %1 / Tổng %2")
                                           .arg(DiskUi::formatBytes(d.freeBytes), DiskUi::formatBytes(d.totalBytes)),
                                       card);
        detailLabel->setStyleSheet("color: #57606a; font-size: 11px; border: none; background: transparent;");
        layout->addWidget(detailLabel);

        m_driveOverviewLayout->addWidget(card);
    }
}

const CleanupCategory* CleanupTab::categoryById(CleanupCategoryId id) const
{
    for (const CleanupCategory& c : m_categories)
        if (c.id == id)
            return &c;
    return nullptr;
}

QString CleanupTab::runningLabel(const CleanupCategory& c) const
{
    for (const QString& exe : c.processNames)
        if (m_runningExes.contains(exe))
            return c.appLabel.isEmpty() ? c.name : c.appLabel;
    return {};
}

QString CleanupTab::categoryNote(const CleanupCategory& c) const
{
    QStringList parts;
    if (c.needsAdmin && !m_elevated)
        parts << "🔒 Cần quyền Quản trị";
    const QString running = runningLabel(c);
    if (!running.isEmpty())
        parts << "⚠ " + running + " đang chạy - tệp đang mở sẽ được bỏ qua";
    if (!c.note.isEmpty())
        parts << c.note;
    return parts.join(" · ");
}

void CleanupTab::onScanClicked()
{
    if (m_scanning)
        return;

    // Tránh quét chồng chéo trên cùng ổ đĩa với một tab khác đang đổi kích thước/dọn dẹp/quét - xem
    // DiskCleanupWindow::isAnyOtherTabBusy().
    if (auto* win = qobject_cast<DiskCleanupWindow*>(window()))
    {
        if (win->isAnyOtherTabBusy(this))
        {
            QMessageBox::warning(this, "Đang có thao tác khác",
                "Một tab khác trong Disk Cleanup đang quét/dọn dẹp/đổi kích thước phân vùng - vui lòng "
                "đợi xong để tránh xung đột trên cùng ổ đĩa, rồi thử lại.");
            return;
        }
    }

    m_elevated = PartitionManager::isElevated();
    m_runningExes = CleanupSystem::runningExeNamesLower();
    const QList<CleanupCategory> all = CategoryRegistry::buildCategories(CleanupEnvironment::current());
    m_categories.clear();
    for (const CleanupCategory& c : all)
        if (CategoryRegistry::isAvailable(c))
            m_categories.push_back(c);

    m_itemsByCategory.clear();
    m_categoryNode.clear();
    m_groupNode.clear();
    m_updatingTree = true;
    m_tree->clear();

    bool anyNeedsAdmin = false;
    QFont boldFont = m_tree->font();
    boldFont.setBold(true);
    for (const QString& group : CategoryRegistry::groupOrder())
    {
        QTreeWidgetItem* groupNode = nullptr;
        for (const CleanupCategory& c : m_categories)
        {
            if (c.group != group)
                continue;
            if (!groupNode)
            {
                groupNode = new QTreeWidgetItem(m_tree);
                groupNode->setText(ColName, group);
                groupNode->setFont(ColName, boldFont);
                groupNode->setFlags(Qt::ItemIsEnabled);
                groupNode->setData(ColName, kRoleKind, "group");
                groupNode->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
                groupNode->setTextAlignment(ColCount, Qt::AlignCenter);
                m_groupNode.insert(group, groupNode);
            }
            auto* node = new QTreeWidgetItem(groupNode);
            Qt::ItemFlags flags = Qt::ItemIsUserCheckable | Qt::ItemIsEnabled;
            if (c.showItems)
                flags |= Qt::ItemIsAutoTristate;
            node->setFlags(flags);
            const bool blockedByAdmin = c.needsAdmin && !m_elevated;
            anyNeedsAdmin = anyNeedsAdmin || blockedByAdmin;
            node->setCheckState(ColName, (c.risk == CleanupRisk::Safe && !blockedByAdmin) ? Qt::Checked : Qt::Unchecked);
            node->setText(ColName, c.name);
            QString tip = c.description;
            if (!c.rootPaths.isEmpty())
            {
                tip += "\n\nThư mục:";
                for (const QString& r : c.rootPaths)
                    tip += "\n  " + nativePath(r);
            }
            tip += c.regenerable ? "\n\nCách dọn: xóa hẳn (tự tạo lại) - giải phóng dung lượng thật."
                                 : "\n\nCách dọn: tệp của bạn - mặc định vào Thùng rác.";
            node->setToolTip(ColName, tip);
            node->setData(ColName, kRoleCategory, static_cast<int>(c.id));
            node->setData(ColName, kRoleKind, "cat");
            const QString risk = CategoryRegistry::riskName(c.risk);
            node->setText(ColRisk, risk);
            node->setForeground(ColRisk, QColor(DiskUi::riskColor(risk)));
            node->setText(ColCount, "—");
            node->setTextAlignment(ColCount, Qt::AlignCenter);
            node->setText(ColSize, "Đang quét...");
            node->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
            const QString note = categoryNote(c);
            node->setText(ColNote, note);
            node->setToolTip(ColNote, note);
            m_categoryNode.insert(c.id, node);
        }
        if (groupNode)
            groupNode->setExpanded(true);
    }
    m_updatingTree = false;
    m_adminBanner->setVisible(anyNeedsAdmin);
    m_relaunchBtn->setVisible(anyNeedsAdmin);

    if (m_categories.isEmpty())
    {
        m_statusLabel->setText("Không tìm thấy hạng mục nào khả dụng trên máy này.");
        return;
    }

    // Lượt quét trước đã báo xong nhưng luồng có thể CHƯA thoát hẳn: startScan() lúc đó bị bỏ qua (đúng hợp đồng
    // "không chồng lượt" - xem disk_vpn_stress_tests) nên tab sẽ kẹt ở trạng thái "Đang quét..." vì không còn tín
    // hiệu kết thúc nào tới. Chờ ngắn cho luồng đó thoát hẳn TRƯỚC khi chuyển giao diện sang trạng thái bận.
    if (m_scanner->isRunning() && !m_scanner->wait(3000))
    {
        m_statusLabel->setText("⚠ Lượt quét trước chưa kết thúc - vui lòng thử lại sau giây lát.");
        return;
    }
    m_scanning = true;
    m_scanBtn->setEnabled(false);
    m_cleanBtn->setEnabled(false);
    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(true);
    m_statusLabel->setText("⏳ Đang quét...");

    m_scanner->setCategories(m_categories);
    // Ưu tiên thấp: nhường CPU cho luồng giao diện, tránh bị Windows báo "Không phản hồi" khi gặp
    // hạng mục lớn (vd Windows.old hàng chục GB).
    m_scanner->startScan(QThread::LowPriority);
}

void CleanupTab::onCategoryStarted(QString name)
{
    m_statusLabel->setText("⏳ Đang quét: " + name);
}

void CleanupTab::onItemFound(CleanupItem item)
{
    m_itemsByCategory[item.categoryId].push_back(item);
}

void CleanupTab::onCategoryFinished(CleanupCategoryId id, qint64 bytes, int count)
{
    QTreeWidgetItem* node = m_categoryNode.value(id, nullptr);
    const CleanupCategory* cat = categoryById(id);
    if (!node || !cat)
        return;

    m_updatingTree = true;
    node->setText(ColCount, QString::number(count));
    node->setText(ColSize, DiskUi::formatBytes(bytes));
    const QList<CleanupItem>& items = m_itemsByCategory[id];

    if (cat->showItems)
    {
        // Hạng mục "xem kỹ": từng mục một, KHÔNG tick sẵn - người dùng tự chọn.
        for (int i = 0; i < items.size(); ++i)
        {
            const CleanupItem& it = items[i];
            auto* child = new QTreeWidgetItem(node);
            child->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
            child->setCheckState(ColName, Qt::Unchecked);
            QString label = QFileInfo(it.path).fileName();
            for (const QString& r : cat->searchRoots)
                if (it.path.startsWith(r + QLatin1Char('/'), Qt::CaseInsensitive))
                    label = QFileInfo(r).fileName() + " › " + nativePath(it.path.mid(r.size() + 1));
            child->setText(ColName, label);
            child->setToolTip(ColName, nativePath(it.path));
            child->setText(ColSize, DiskUi::formatBytes(it.sizeBytes));
            child->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
            child->setText(ColNote, it.detail);
            child->setToolTip(ColNote, it.detail);
            child->setData(ColName, kRoleCategory, static_cast<int>(id));
            child->setData(ColName, kRoleKind, "item");
            child->setData(ColName, kRoleItemIndex, i);
        }
        // Hạng mục cha tự tính trạng thái từ các mục con (tất cả chưa tick -> chưa tick).
    }
    else if (cat->kind == CleanupKind::FolderContents && !items.isEmpty())
    {
        // Nguồn: mỗi thư mục gốc một dòng (chỉ xem) - người dùng thấy cache NẰM Ở ĐÂU và chiếm bao nhiêu.
        QStringList order;
        QHash<QString, QPair<qint64, int>> perRoot;
        for (const CleanupItem& it : items)
        {
            const QString key = it.root.isEmpty() ? it.path : it.root;
            if (!perRoot.contains(key))
                order << key;
            perRoot[key].first += it.sizeBytes;
            perRoot[key].second += 1;
        }
        std::sort(order.begin(), order.end(),
                  [&](const QString& a, const QString& b) { return perRoot[a].first > perRoot[b].first; });
        for (const QString& r : order)
        {
            auto* child = new QTreeWidgetItem(node);
            child->setFlags(Qt::ItemIsEnabled);
            child->setText(ColName, nativePath(r));
            child->setToolTip(ColName, nativePath(r));
            child->setText(ColCount, QString::number(perRoot[r].second));
            child->setTextAlignment(ColCount, Qt::AlignCenter);
            child->setText(ColSize, DiskUi::formatBytes(perRoot[r].first));
            child->setTextAlignment(ColSize, Qt::AlignRight | Qt::AlignVCenter);
            child->setForeground(ColName, QColor("#57606a"));
            child->setData(ColName, kRoleKind, "source");
        }
    }
    else if (!items.isEmpty() && !items.first().detail.isEmpty())
    {
        QString note = categoryNote(*cat);
        note = note.isEmpty() ? items.first().detail : items.first().detail + " · " + note;
        node->setText(ColNote, note);
        node->setToolTip(ColNote, note);
    }

    // Hạng mục không có gì để dọn: ẩn đi cho gọn (giống roomy) - vẫn giữ trong danh sách, không ảnh hưởng gì.
    node->setHidden(count == 0);
    m_updatingTree = false;
    updateGroupTotals();
}

void CleanupTab::updateGroupTotals()
{
    for (auto it = m_groupNode.constBegin(); it != m_groupNode.constEnd(); ++it)
    {
        QTreeWidgetItem* groupNode = it.value();
        qint64 bytes = 0;
        int visible = 0;
        for (int i = 0; i < groupNode->childCount(); ++i)
        {
            QTreeWidgetItem* catNode = groupNode->child(i);
            const auto id = static_cast<CleanupCategoryId>(catNode->data(ColName, kRoleCategory).toInt());
            for (const CleanupItem& item : m_itemsByCategory.value(id))
                bytes += item.sizeBytes;
            if (!catNode->isHidden())
                ++visible;
        }
        m_updatingTree = true;
        groupNode->setText(ColSize, DiskUi::formatBytes(bytes));
        m_updatingTree = false;
        if (!m_scanning)
            groupNode->setHidden(visible == 0);
    }
}

void CleanupTab::onScanFinished(qint64 totalBytes, int totalItems)
{
    m_scanning = false;
    m_scanBtn->setEnabled(true);
    m_progressBar->setVisible(false);
    updateGroupTotals();
    m_statusLabel->setText(QString("✓ Quét xong: %1 mục, tổng %2 có thể dọn dẹp.")
                               .arg(totalItems)
                               .arg(DiskUi::formatBytes(totalBytes)));
    updateSelectedSummary();
}

void CleanupTab::onScanStopped()
{
    m_scanning = false;
    m_scanBtn->setEnabled(true);
    m_progressBar->setVisible(false);
    m_statusLabel->setText("Đã dừng quét.");
    updateSelectedSummary();
}

void CleanupTab::onTreeItemChanged(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(item);
    if (m_updatingTree || column != ColName)
        return;
    updateSelectedSummary();
}

qint64 CleanupTab::selectedTotalBytes() const
{
    qint64 total = 0;
    for (const CleanupCategory& c : m_categories)
    {
        QTreeWidgetItem* node = m_categoryNode.value(c.id, nullptr);
        if (!node || node->isHidden())
            continue;
        const QList<CleanupItem> items = m_itemsByCategory.value(c.id);
        if (c.showItems)
        {
            for (int i = 0; i < node->childCount(); ++i)
            {
                QTreeWidgetItem* child = node->child(i);
                const int idx = child->data(ColName, kRoleItemIndex).toInt();
                if (child->checkState(ColName) == Qt::Checked && idx >= 0 && idx < items.size())
                    total += items[idx].sizeBytes;
            }
        }
        else if (node->checkState(ColName) == Qt::Checked)
        {
            for (const CleanupItem& item : items)
                total += item.sizeBytes;
        }
    }
    return total;
}

void CleanupTab::updateSelectedSummary()
{
    const qint64 total = selectedTotalBytes();
    m_summaryLabel->setText("Đã chọn: " + DiskUi::formatBytes(total));
    m_cleanBtn->setEnabled(!m_scanning && !isCleaningNow() && total > 0);
}

void CleanupTab::onRelaunchElevatedClicked()
{
    // Giống PartitionTab::onRelaunchElevatedClicked: không thoát giữa một thao tác không an toàn để hủy.
    QString busyReason;
    if (ToolManager::instance().anyToolWindowBusy(&busyReason))
    {
        QMessageBox::warning(this, "Không thể chạy lại lúc này",
            "Đang có một thao tác không an toàn để hủy giữa chừng (" + busyReason + ") - chạy lại ứng "
            "dụng lúc này có thể làm hỏng dữ liệu. Vui lòng đợi thao tác đó hoàn tất rồi thử lại.");
        return;
    }
    QString error;
    if (!PartitionManager::relaunchElevated(&error))
    {
        QMessageBox::warning(this, "Chạy lại với quyền Quản trị", error.isEmpty() ? "Không thực hiện được." : error);
        return;
    }
    qApp->quit();
}

void CleanupTab::onCleanClicked()
{
    // Tránh xóa chồng chéo trên cùng ổ đĩa với một tab khác đang đổi kích thước/dọn dẹp/quét - xem
    // DiskCleanupWindow::isAnyOtherTabBusy().
    if (auto* win = qobject_cast<DiskCleanupWindow*>(window()))
    {
        if (win->isAnyOtherTabBusy(this))
        {
            QMessageBox::warning(this, "Đang có thao tác khác",
                "Một tab khác trong Disk Cleanup đang quét/dọn dẹp/đổi kích thước phân vùng - vui lòng "
                "đợi xong để tránh xung đột trên cùng ổ đĩa, rồi thử lại.");
            return;
        }
    }

    QList<CleanupTask> tasks;
    QStringList categoryLines;
    QStringList runningApps;
    qint64 total = 0;
    qint64 regenerableBytes = 0;
    qint64 userBytes = 0;
    bool includesHighRisk = false;
    bool includesRecycleBin = false;
    bool includesHibernation = false;

    for (const CleanupCategory& cat : m_categories)
    {
        QTreeWidgetItem* node = m_categoryNode.value(cat.id, nullptr);
        if (!node || node->isHidden())
            continue;
        const QList<CleanupItem> items = m_itemsByCategory.value(cat.id);
        QList<CleanupItem> chosen;
        if (cat.showItems)
        {
            for (int i = 0; i < node->childCount(); ++i)
            {
                QTreeWidgetItem* child = node->child(i);
                const int idx = child->data(ColName, kRoleItemIndex).toInt();
                if (child->checkState(ColName) == Qt::Checked && idx >= 0 && idx < items.size())
                    chosen << items[idx];
            }
        }
        else if (node->checkState(ColName) == Qt::Checked)
        {
            chosen = items;
        }
        if (chosen.isEmpty())
            continue;

        qint64 catBytes = 0;
        for (const CleanupItem& item : chosen)
        {
            tasks << CleanupTask::forItem(cat, item);
            catBytes += item.sizeBytes;
        }
        total += catBytes;
        if (cat.kind == CleanupKind::RecycleBin)
            includesRecycleBin = true;
        else if (cat.kind == CleanupKind::Hibernation)
            includesHibernation = true;
        else if (cat.regenerable)
            regenerableBytes += catBytes;
        else
            userBytes += catBytes;
        if (cat.risk == CleanupRisk::High)
            includesHighRisk = true;
        const QString running = runningLabel(cat);
        if (!running.isEmpty() && !runningApps.contains(running))
            runningApps << running;

        // Tên hạng mục + (các) thư mục gốc THẬT sẽ bị dọn - hiện trong hộp xác nhận bên dưới.
        QString line = QString("%1 (%2)").arg(cat.name, DiskUi::formatBytes(catBytes));
        if (cat.showItems)
            line += QString("\n        ↳ %1 mục bạn đã chọn").arg(chosen.size());
        else
        {
            const int shown = qMin(3, static_cast<int>(cat.rootPaths.size()));
            for (int i = 0; i < shown; ++i)
                line += "\n        ↳ " + nativePath(cat.rootPaths[i]);
            if (cat.rootPaths.size() > shown)
                line += QString("\n        ↳ ... và %1 thư mục khác").arg(cat.rootPaths.size() - shown);
        }
        categoryLines << line;
    }

    if (tasks.isEmpty())
    {
        QMessageBox::information(this, "Dọn dẹp", "Chưa có hạng mục nào được chọn.");
        return;
    }

    const bool permanent = m_permanentCheck->isChecked();
    // Hộp xác nhận hiện RÕ thư mục gốc của từng hạng mục: người dùng phải thấy chính xác nơi sắp bị dọn
    // (vd biến môi trường TEMP của máy bị trỏ sang một thư mục lạ) trước khi bấm Yes, không chỉ tên hạng mục.
    QString message = QString("Sẽ dọn %1 thuộc %2 hạng mục:\n\n  • %3\n")
                          .arg(DiskUi::formatBytes(total))
                          .arg(categoryLines.size())
                          .arg(categoryLines.join("\n  • "));
    QStringList how;
    if (regenerableBytes > 0)
        how << QString("Bộ nhớ đệm / tệp tạm / dump / nhật ký (%1): XÓA HẲN để giải phóng dung lượng thật - chúng tự "
                       "tạo lại khi cần. Tệp đang được ứng dụng mở sẽ được bỏ qua.")
                   .arg(DiskUi::formatBytes(regenerableBytes));
    if (userBytes > 0)
        how << (permanent ? QString("⚠ Tệp của bạn (%1): XÓA VĨNH VIỄN - không qua Thùng rác, không thể khôi phục.")
                                .arg(DiskUi::formatBytes(userBytes))
                          : QString("Tệp của bạn (%1): chuyển vào Thùng rác, khôi phục được. Mục nào KHÔNG vào Thùng rác "
                                    "được (quá lớn, ổ không có Thùng rác) được GIỮ NGUYÊN.")
                                .arg(DiskUi::formatBytes(userBytes)));
    if (includesRecycleBin)
        how << "⚠ Thùng rác sẽ được LÀM TRỐNG - các tệp trong đó bị xóa hẳn, không khôi phục được.";
    if (includesHibernation)
        how << "⚠ Tắt ngủ đông: Windows sẽ hỏi quyền Quản trị (UAC) rồi tự gỡ hiberfil.sys; mất chế độ Ngủ đông và "
               "Khởi động nhanh (bật lại: powercfg /hibernate on).";
    message += "\n" + how.join("\n\n");
    if (!runningApps.isEmpty())
        message += "\n\n⚠ Đang chạy: " + runningApps.join(", ") +
                   " - nên đóng trước để dọn được hết (tệp đang mở sẽ được giữ lại, không ép xóa).";
    if (includesHighRisk)
        message += "\n\n⚠ Có hạng mục RỦI RO CAO - hãy chắc chắn trước khi tiếp tục.";

    if (QMessageBox::question(this, "Xác nhận dọn dẹp", message, QMessageBox::Yes | QMessageBox::No,
                              QMessageBox::No) != QMessageBox::Yes)
        return;

    m_cleanBtn->setEnabled(false);
    m_scanBtn->setEnabled(false);
    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(true);
    m_statusLabel->setText("⏳ Đang dọn dẹp...");

    m_executor->setTasks(tasks);
    m_executor->setPermanentDelete(permanent);
    m_executor->start(QThread::LowPriority);
}

void CleanupTab::onExecutionFinished(bool success, QString error, qint64 freedBytes, int deletedCount, qint64 recycledBytes)
{
    m_scanBtn->setEnabled(true);
    m_progressBar->setVisible(false);
    reloadDriveOverview();

    if (success)
    {
        const qint64 reallyFreed = qMax<qint64>(0, freedBytes - recycledBytes);
        QString info = QString("Đã giải phóng %1 (%2 tệp/mục).").arg(DiskUi::formatBytes(reallyFreed)).arg(deletedCount);
        if (recycledBytes > 0)
            info += QString("\n%1 đã chuyển vào Thùng rác - dung lượng này chỉ được giải phóng khi làm trống Thùng rác.")
                        .arg(DiskUi::formatBytes(recycledBytes));
        m_statusLabel->setText("✓ " + info.section('\n', 0, 0));
        // 'error' ở đây là GHI CHÚ, không phải lỗi nghiêm trọng - vài tệp tạm tự mất giữa lúc quét và
        // lúc xóa (hoặc đang được chương trình khác dùng) là chuyện bình thường, không đáng báo đỏ.
        if (!error.isEmpty())
            info += "\n\nLưu ý:\n" + error;
        QMessageBox::information(this, "Hoàn tất", info);
        // Xóa xong, quét lại để làm mới danh sách (các mục đã xóa không còn nữa)
        onScanClicked();
    }
    else
    {
        m_statusLabel->setText("⚠ Dọn dẹp thất bại: " + error);
        QMessageBox::critical(this, "Lỗi", "Không dọn dẹp được:\n" + error);
        updateSelectedSummary();
    }
}
