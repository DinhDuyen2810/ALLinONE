#include "AutoClickWindow.h"
#include "storage/ActionSerializer.h"
#include "core/Logger.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QCloseEvent>
#include <QIcon>

AutoClickWindow::AutoClickWindow(QWidget* parent)
    : QWidget(parent)
    , m_runner(new ActionRunner(this))
    , m_overlay(new RuntimeOverlay())
    , m_mouseCapture(new MouseCapture(this, this))
{
    setupUi();

    // Wire runner signals
    connect(m_runner, &ActionRunner::stateChanged, this, &AutoClickWindow::onRunnerStateChanged);
    connect(m_runner, &ActionRunner::roundStarted, this, &AutoClickWindow::onRunnerRoundStarted);
    connect(m_runner, &ActionRunner::actionStarted, this, &AutoClickWindow::onRunnerActionStarted);
    connect(m_runner, &ActionRunner::countdownTick, this, &AutoClickWindow::onRunnerCountdownTick);
    connect(m_runner, &ActionRunner::chainFinished, this, &AutoClickWindow::onRunnerFinished);
    connect(m_runner, &ActionRunner::executionStopped, this, &AutoClickWindow::onRunnerFinished);

    // Overlay stop button
    connect(m_overlay, &RuntimeOverlay::stopClicked, this, &AutoClickWindow::onStopClicked);

    // Capture signals
    connect(m_actionEditorWidget, &ActionEditorWidget::captureRequested, this, &AutoClickWindow::onCaptureRequested);
    connect(m_mouseCapture, &MouseCapture::pointCaptured, m_actionEditorWidget, &ActionEditorWidget::onPositionCaptured);

    loadDefaultProfile();
}

AutoClickWindow::~AutoClickWindow()
{
    if (m_overlay)
    {
        m_overlay->close();
        delete m_overlay;
        m_overlay = nullptr;
    }
}

void AutoClickWindow::closeEvent(QCloseEvent* event)
{
    if (m_runner && m_runner->isRunningState())
    {
        auto reply = QMessageBox::question(this, "Đang thực thi", "Chuỗi thao tác đang chạy. Bạn có muốn dừng lại và đóng cửa sổ?", QMessageBox::Yes | QMessageBox::No);
        if (reply == QMessageBox::Yes)
        {
            m_runner->requestStop();
            m_runner->wait(2000);
            if (m_overlay) m_overlay->hide();
            event->accept();
        }
        else
        {
            event->ignore();
            return;
        }
    }
    if (m_overlay) m_overlay->hide();
    event->accept();
}

#include "core/IconHelper.h"

void AutoClickWindow::setupUi()
{
    setWindowTitle("One for ALL - Auto Click Module");
    setWindowIcon(IconHelper::makeBadgedIcon(":/icons/autoclicker.jpg", 48, 10, 4));
    resize(1050, 650);
    setMinimumSize(850, 520);
    setStyleSheet(
        "QWidget { background-color: #f6f8fa; color: #1f2328; font-family: 'Segoe UI', sans-serif; }"
        "QScrollBar:horizontal { height: 0px; background: transparent; }"
        "QScrollBar:vertical { background: #f0f2f5; width: 8px; margin: 0px; border-radius: 4px; }"
        "QScrollBar::handle:vertical { background: #c0c6cc; min-height: 24px; border-radius: 4px; }"
        "QScrollBar::handle:vertical:hover { background: #9aa0a6; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; background: none; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
    );

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 14, 14, 14);
    mainLayout->setSpacing(10);

    // 1. Top Toolbar
    auto* topBar = new QHBoxLayout();
    topBar->setSpacing(8);

    auto makeBtn = [](const QString& text, const QString& tooltip) {
        auto* btn = new QPushButton(text);
        btn->setToolTip(tooltip);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 6px 14px; font-size: 12px; font-weight: 500; }"
            "QPushButton:hover { background-color: #f3f4f6; color: #0969da; border-color: #0969da; }"
            "QPushButton:pressed { background-color: #ebecf0; }"
        );
        return btn;
    };

    m_saveButton = makeBtn("💾 Lưu cấu hình (Save)", "Lưu danh sách chain hiện tại vào profile mặc định");
    m_loadButton = makeBtn("📂 Nạp cấu hình (Load)", "Tải lại danh sách chain từ profile mặc định");
    m_exportButton = makeBtn("📤 Xuất file (Export)", "Xuất chain ra file JSON");
    m_importButton = makeBtn("📥 Nhập file (Import)", "Nhập chain từ file JSON");

    connect(m_saveButton, &QPushButton::clicked, this, &AutoClickWindow::onSaveProfile);
    connect(m_loadButton, &QPushButton::clicked, this, &AutoClickWindow::onLoadProfile);
    connect(m_exportButton, &QPushButton::clicked, this, &AutoClickWindow::onExportProfile);
    connect(m_importButton, &QPushButton::clicked, this, &AutoClickWindow::onImportProfile);

    topBar->addWidget(m_saveButton);
    topBar->addWidget(m_loadButton);
    topBar->addWidget(m_exportButton);
    topBar->addWidget(m_importButton);
    topBar->addStretch();
    mainLayout->addLayout(topBar);

    // 2. Central Splitter (3 Panels)
    m_splitter = new QSplitter(Qt::Horizontal, this);
    m_splitter->setStyleSheet("QSplitter::handle { background-color: #d0d7de; width: 6px; border-radius: 3px; }");
    m_splitter->setChildrenCollapsible(false);

    m_chainListWidget = new ChainListWidget(m_splitter);
    m_chainListWidget->setMinimumWidth(160);

    m_actionListWidget = new ActionListWidget(m_splitter);
    m_actionListWidget->setMinimumWidth(320);

    m_actionEditorWidget = new ActionEditorWidget(m_splitter);
    m_actionEditorWidget->setMinimumWidth(220);

    m_splitter->addWidget(m_chainListWidget);
    m_splitter->addWidget(m_actionListWidget);
    m_splitter->addWidget(m_actionEditorWidget);
    m_splitter->setStretchFactor(0, 2); // 20%
    m_splitter->setStretchFactor(1, 5); // 50%
    m_splitter->setStretchFactor(2, 3); // 30%
    mainLayout->addWidget(m_splitter, 1);

    // Wire Chain signals
    connect(m_chainListWidget, &ChainListWidget::chainSelectionChanged, this, &AutoClickWindow::onChainSelected);
    connect(m_chainListWidget, &ChainListWidget::addChainClicked, this, &AutoClickWindow::onAddChain);
    connect(m_chainListWidget, &ChainListWidget::cloneChainClicked, this, &AutoClickWindow::onCloneChain);
    connect(m_chainListWidget, &ChainListWidget::deleteChainClicked, this, &AutoClickWindow::onDeleteChain);
    connect(m_chainListWidget, &ChainListWidget::renameChainClicked, this, &AutoClickWindow::onRenameChain);
    connect(m_chainListWidget, &ChainListWidget::runChainClicked, this, [this](int /*index*/) { onRunClicked(); });

    // Wire Action signals
    connect(m_actionListWidget, &ActionListWidget::actionSelectionChanged, this, &AutoClickWindow::onActionSelected);
    connect(m_actionListWidget, &ActionListWidget::addActionClicked, this, &AutoClickWindow::onAddAction);
    connect(m_actionListWidget, &ActionListWidget::editActionClicked, this, &AutoClickWindow::onEditAction);
    connect(m_actionListWidget, &ActionListWidget::cloneActionClicked, this, &AutoClickWindow::onCloneAction);
    connect(m_actionListWidget, &ActionListWidget::deleteActionClicked, this, &AutoClickWindow::onDeleteAction);
    connect(m_actionListWidget, &ActionListWidget::insertActionClicked, this, &AutoClickWindow::onInsertAction);
    connect(m_actionListWidget, &ActionListWidget::moveActionClicked, this, &AutoClickWindow::onMoveAction);
    connect(m_actionEditorWidget, &ActionEditorWidget::actionSaved, this, &AutoClickWindow::onActionSaved);

    // 3. Bottom Control Bar
    auto* bottomBar = new QHBoxLayout();
    bottomBar->setSpacing(12);

    m_runButton = new QPushButton("▶ Bắt đầu chạy (RUN)", this);
    m_runButton->setFixedHeight(38);
    m_runButton->setCursor(Qt::PointingHandCursor);
    m_runButton->setStyleSheet(
        "QPushButton { background-color: #1f883d; color: white; border: none; border-radius: 10px; font-weight: bold; font-size: 13px; padding: 0 20px; }"
        "QPushButton:hover { background-color: #1a7f37; }"
        "QPushButton:pressed { background-color: #116329; }"
        "QPushButton:disabled { background-color: #eaeef2; color: #8c959f; }"
    );
    connect(m_runButton, &QPushButton::clicked, this, &AutoClickWindow::onRunClicked);

    m_stopButton = new QPushButton("■ Dừng lại (STOP)", this);
    m_stopButton->setFixedHeight(38);
    m_stopButton->setEnabled(false);
    m_stopButton->setCursor(Qt::PointingHandCursor);
    m_stopButton->setStyleSheet(
        "QPushButton { background-color: #cf222e; color: white; border: none; border-radius: 10px; font-weight: bold; font-size: 13px; padding: 0 20px; }"
        "QPushButton:hover { background-color: #a40e26; }"
        "QPushButton:pressed { background-color: #82071e; }"
        "QPushButton:disabled { background-color: #eaeef2; color: #8c959f; }"
    );
    connect(m_stopButton, &QPushButton::clicked, this, &AutoClickWindow::onStopClicked);

    auto* repeatLabel = new QLabel("Lặp lại (Repeat):", this);
    repeatLabel->setStyleSheet("color: #1f2328; font-weight: bold; font-size: 12px;");

    m_repeatSpin = new QSpinBox(this);
    m_repeatSpin->setRange(1, 999999);
    m_repeatSpin->setValue(1);
    m_repeatSpin->setFixedWidth(84);
    m_repeatSpin->setFixedHeight(32);
    m_repeatSpin->setStyleSheet("background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 4px; font-weight: bold;");

    m_infiniteCheck = new QCheckBox("Vô hạn (Infinite)", this);
    m_infiniteCheck->setStyleSheet("color: #57606a; font-weight: 500;");
    connect(m_infiniteCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_repeatSpin->setEnabled(!checked);
    });

    m_statusLabel = new QLabel("Trạng thái: Sẵn sàng (Idle)", this);
    m_statusLabel->setStyleSheet(
        "background-color: rgba(9, 105, 218, 0.1); color: #0969da; "
        "border: 1px solid rgba(9, 105, 218, 0.3); border-radius: 10px; "
        "padding: 6px 14px; font-weight: bold; font-size: 12px;"
    );

    bottomBar->addWidget(m_runButton);
    bottomBar->addWidget(m_stopButton);
    bottomBar->addSpacing(10);
    bottomBar->addWidget(repeatLabel);
    bottomBar->addWidget(m_repeatSpin);
    bottomBar->addWidget(m_infiniteCheck);
    bottomBar->addSpacing(15);
    bottomBar->addWidget(m_statusLabel);
    bottomBar->addStretch();

    mainLayout->addLayout(bottomBar);
}

void AutoClickWindow::loadDefaultProfile()
{
    QString path = ActionSerializer::getDefaultProfilePath();
    if (!ActionSerializer::loadFromFile(path, m_chains) || m_chains.empty())
    {
        // Create demo sample chain as documented in Section 94
        ActionChain defaultChain;
        defaultChain.id = "demo_chain";
        defaultChain.name = "Demo Thao Tác";
        defaultChain.description = "Chuỗi hành động mẫu Click, Type, Wait";
        defaultChain.repeatCount = 1;

        Action a1;
        a1.type = ActionType::MouseClick;
        a1.x = 500;
        a1.y = 300;
        a1.waitBefore = std::chrono::milliseconds(500);
        a1.waitAfter = std::chrono::milliseconds(500);

        Action a2;
        a2.type = ActionType::TypeText;
        a2.text = "admin";
        a2.waitBefore = std::chrono::milliseconds(200);
        a2.waitAfter = std::chrono::milliseconds(400);

        Action a3;
        a3.type = ActionType::KeyPress;
        a3.keyCode = 9; // TAB
        a3.keyName = "TAB";
        a3.waitBefore = std::chrono::milliseconds(100);
        a3.waitAfter = std::chrono::milliseconds(300);

        Action a4;
        a4.type = ActionType::TypeText;
        a4.text = "123456";
        a4.waitBefore = std::chrono::milliseconds(200);
        a4.waitAfter = std::chrono::milliseconds(500);

        Action a5;
        a5.type = ActionType::KeyPress;
        a5.keyCode = 13; // ENTER
        a5.keyName = "ENTER";
        a5.waitBefore = std::chrono::milliseconds(200);
        a5.waitAfter = std::chrono::milliseconds(1000);

        defaultChain.actions = {a1, a2, a3, a4, a5};
        m_chains.push_back(defaultChain);
        ActionSerializer::saveToFile(path, m_chains);
    }

    m_chainListWidget->setChains(m_chains);
    syncUiWithCurrentChain();
}

ActionChain* AutoClickWindow::currentChain()
{
    if (m_currentChainIndex >= 0 && m_currentChainIndex < static_cast<int>(m_chains.size()))
    {
        return &m_chains[m_currentChainIndex];
    }
    return nullptr;
}

void AutoClickWindow::syncUiWithCurrentChain()
{
    ActionChain* chain = currentChain();
    if (chain)
    {
        m_actionListWidget->setActions(chain->actions);
        if (chain->repeatCount == -1)
        {
            m_infiniteCheck->setChecked(true);
            m_repeatSpin->setEnabled(false);
        }
        else
        {
            m_infiniteCheck->setChecked(false);
            m_repeatSpin->setEnabled(true);
            m_repeatSpin->setValue(std::max(1, chain->repeatCount));
        }

        if (!chain->actions.empty())
        {
            m_actionListWidget->setSelectedActionIndex(0);
            m_actionEditorWidget->setAction(chain->actions[0], 0);
        }
        else
        {
            m_actionEditorWidget->clear();
        }
    }
    else
    {
        m_actionListWidget->setActions({});
        m_actionEditorWidget->clear();
    }
}

void AutoClickWindow::onChainSelected(int index)
{
    if (index >= 0 && index < static_cast<int>(m_chains.size()))
    {
        m_currentChainIndex = index;
        syncUiWithCurrentChain();
    }
}

void AutoClickWindow::onAddChain()
{
    ActionChain chain;
    chain.id = "chain_" + std::to_string(m_chains.size() + 1);
    chain.name = "Chain Mới " + std::to_string(m_chains.size() + 1);
    chain.description = "Mô tả chuỗi hành động";
    chain.repeatCount = 1;

    m_chains.push_back(chain);
    m_currentChainIndex = static_cast<int>(m_chains.size()) - 1;
    m_chainListWidget->setChains(m_chains);
    m_chainListWidget->setSelectedChainIndex(m_currentChainIndex);
    syncUiWithCurrentChain();
}

void AutoClickWindow::onCloneChain(int index)
{
    if (index >= 0 && index < static_cast<int>(m_chains.size()))
    {
        ActionChain cloned = m_chains[index].clone();
        m_chains.push_back(cloned);
        m_currentChainIndex = static_cast<int>(m_chains.size()) - 1;
        m_chainListWidget->setChains(m_chains);
        m_chainListWidget->setSelectedChainIndex(m_currentChainIndex);
        syncUiWithCurrentChain();
    }
}

void AutoClickWindow::onDeleteChain(int index)
{
    if (index >= 0 && index < static_cast<int>(m_chains.size()))
    {
        if (m_chains.size() == 1)
        {
            QMessageBox::warning(this, "Không thể xóa", "Phải giữ lại ít nhất một chuỗi hành động.");
            return;
        }

        m_chains.erase(m_chains.begin() + index);
        m_currentChainIndex = std::max(0, index - 1);
        m_chainListWidget->setChains(m_chains);
        m_chainListWidget->setSelectedChainIndex(m_currentChainIndex);
        syncUiWithCurrentChain();
    }
}

void AutoClickWindow::onRenameChain(int index, const QString& newName)
{
    if (index >= 0 && index < static_cast<int>(m_chains.size()))
    {
        m_chains[index].name = newName.toStdString();
        m_chainListWidget->setChains(m_chains);
    }
}

void AutoClickWindow::onActionSelected(int index)
{
    ActionChain* chain = currentChain();
    if (chain && index >= 0 && index < static_cast<int>(chain->actions.size()))
    {
        m_actionEditorWidget->setAction(chain->actions[index], index);
    }
}

void AutoClickWindow::onAddAction()
{
    ActionChain* chain = currentChain();
    if (!chain) return;

    Action newAct;
    chain->actions.push_back(newAct);
    int newIdx = static_cast<int>(chain->actions.size()) - 1;
    m_actionListWidget->setActions(chain->actions);
    m_actionListWidget->setSelectedActionIndex(newIdx);
    m_actionEditorWidget->setAction(newAct, newIdx);
}

void AutoClickWindow::onEditAction(int index)
{
    onActionSelected(index);
}

void AutoClickWindow::onCloneAction(int index)
{
    ActionChain* chain = currentChain();
    if (chain && index >= 0 && index < static_cast<int>(chain->actions.size()))
    {
        Action cloned = chain->actions[index];
        chain->actions.insert(chain->actions.begin() + index + 1, cloned);
        m_actionListWidget->setActions(chain->actions);
        m_actionListWidget->setSelectedActionIndex(index + 1);
        m_actionEditorWidget->setAction(cloned, index + 1);
    }
}

void AutoClickWindow::onDeleteAction(int index)
{
    ActionChain* chain = currentChain();
    if (chain && index >= 0 && index < static_cast<int>(chain->actions.size()))
    {
        chain->actions.erase(chain->actions.begin() + index);
        m_actionListWidget->setActions(chain->actions);
        int nextIdx = std::min(index, static_cast<int>(chain->actions.size()) - 1);
        if (nextIdx >= 0)
        {
            m_actionListWidget->setSelectedActionIndex(nextIdx);
            m_actionEditorWidget->setAction(chain->actions[nextIdx], nextIdx);
        }
        else
        {
            m_actionEditorWidget->clear();
        }
    }
}

void AutoClickWindow::onInsertAction(int index, bool before)
{
    ActionChain* chain = currentChain();
    if (!chain) return;

    Action newAct;
    int insertPos = before ? index : index + 1;
    if (insertPos < 0) insertPos = 0;
    if (insertPos > static_cast<int>(chain->actions.size())) insertPos = static_cast<int>(chain->actions.size());

    chain->actions.insert(chain->actions.begin() + insertPos, newAct);
    m_actionListWidget->setActions(chain->actions);
    m_actionListWidget->setSelectedActionIndex(insertPos);
    m_actionEditorWidget->setAction(newAct, insertPos);
}

void AutoClickWindow::onMoveAction(int index, int direction)
{
    ActionChain* chain = currentChain();
    if (!chain) return;

    int newIdx = index + direction;
    if (newIdx >= 0 && newIdx < static_cast<int>(chain->actions.size()))
    {
        std::swap(chain->actions[index], chain->actions[newIdx]);
        m_actionListWidget->setActions(chain->actions);
        m_actionListWidget->setSelectedActionIndex(newIdx);
    }
}

void AutoClickWindow::onActionSaved(const Action& action, int actionIndex)
{
    ActionChain* chain = currentChain();
    if (!chain) return;

    if (actionIndex >= 0 && actionIndex < static_cast<int>(chain->actions.size()))
    {
        chain->actions[actionIndex] = action;
        m_actionListWidget->setActions(chain->actions);
        m_actionListWidget->setSelectedActionIndex(actionIndex);
    }
    else
    {
        chain->actions.push_back(action);
        int newIdx = static_cast<int>(chain->actions.size()) - 1;
        m_actionListWidget->setActions(chain->actions);
        m_actionListWidget->setSelectedActionIndex(newIdx);
    }
}

void AutoClickWindow::onRunClicked()
{
    ActionChain* chain = currentChain();
    if (!chain || chain->actions.empty())
    {
        QMessageBox::information(this, "Thông báo", "Chuỗi hành động chưa có bước nào. Vui lòng thêm ít nhất một hành động.");
        return;
    }

    chain->repeatCount = m_infiniteCheck->isChecked() ? -1 : m_repeatSpin->value();

    m_runButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    m_statusLabel->setText("Trạng thái: Đang thực thi...");

    m_overlay->show();

    m_runner->setChain(*chain);
    m_runner->start();
}

void AutoClickWindow::onStopClicked()
{
    m_statusLabel->setText("Trạng thái: Đang dừng lại...");
    m_runner->requestStop();
}

void AutoClickWindow::onRunnerStateChanged(RunnerState state)
{
    switch (state)
    {
        case RunnerState::Running:
            m_statusLabel->setText("Trạng thái: Đang thực thi (Running)");
            break;
        case RunnerState::Stopping:
            m_statusLabel->setText("Trạng thái: Đang dừng (Stopping)");
            break;
        case RunnerState::Finished:
            m_statusLabel->setText("Trạng thái: Hoàn tất (Finished)");
            break;
        case RunnerState::Idle:
            m_statusLabel->setText("Trạng thái: Sẵn sàng (Idle)");
            break;
        default:
            break;
    }
}

void AutoClickWindow::onRunnerRoundStarted(int currentRound, int totalRounds)
{
    m_overlay->setRoundInfo(currentRound, totalRounds);
}

void AutoClickWindow::onRunnerActionStarted(int /*currentRound*/, int /*totalRounds*/, int actionIndex, int totalActions,
                                           const QString& currentDesc, const QString& nextDesc, int targetX, int targetY)
{
    m_overlay->setActionInfo(actionIndex, totalActions, currentDesc, nextDesc, targetX, targetY);
    m_actionListWidget->setSelectedActionIndex(actionIndex - 1);
}

void AutoClickWindow::onRunnerCountdownTick(qint64 remainingMs, const QString& phase)
{
    m_overlay->setCountdown(remainingMs, phase);
}

void AutoClickWindow::onRunnerFinished()
{
    m_runButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    m_overlay->hide();
    m_statusLabel->setText("Trạng thái: Sẵn sàng (Idle)");
}

void AutoClickWindow::onSaveProfile()
{
    QString path = ActionSerializer::getDefaultProfilePath();
    if (ActionSerializer::saveToFile(path, m_chains))
    {
        QMessageBox::information(this, "Lưu cấu hình", "Đã lưu thành công cấu hình vào: " + path);
    }
    else
    {
        QMessageBox::critical(this, "Lỗi", "Không thể lưu cấu hình vào: " + path);
    }
}

void AutoClickWindow::onLoadProfile()
{
    QString path = ActionSerializer::getDefaultProfilePath();
    if (ActionSerializer::loadFromFile(path, m_chains))
    {
        m_currentChainIndex = 0;
        m_chainListWidget->setChains(m_chains);
        syncUiWithCurrentChain();
        QMessageBox::information(this, "Nạp cấu hình", "Đã nạp thành công cấu hình từ: " + path);
    }
    else
    {
        QMessageBox::critical(this, "Lỗi", "Không thể nạp cấu hình từ: " + path);
    }
}

void AutoClickWindow::onExportProfile()
{
    QString path = QFileDialog::getSaveFileName(this, "Xuất cấu hình Auto Click", "profiles/export.json", "JSON Files (*.json)");
    if (!path.isEmpty())
    {
        if (ActionSerializer::saveToFile(path, m_chains))
        {
            QMessageBox::information(this, "Xuất file", "Đã xuất cấu hình ra file thành công!");
        }
        else
        {
            QMessageBox::critical(this, "Lỗi", "Không thể xuất file.");
        }
    }
}

void AutoClickWindow::onImportProfile()
{
    QString path = QFileDialog::getOpenFileName(this, "Nhập cấu hình Auto Click", "profiles", "JSON Files (*.json)");
    if (!path.isEmpty())
    {
        std::vector<ActionChain> loaded;
        if (ActionSerializer::loadFromFile(path, loaded))
        {
            m_chains = loaded;
            m_currentChainIndex = 0;
            m_chainListWidget->setChains(m_chains);
            syncUiWithCurrentChain();
            QMessageBox::information(this, "Nhập file", "Đã nhập cấu hình thành công!");
        }
        else
        {
            QMessageBox::critical(this, "Lỗi", "File không hợp lệ hoặc lỗi phân tích cú pháp JSON.");
        }
    }
}

void AutoClickWindow::onCaptureRequested(int /*targetField*/)
{
    m_mouseCapture->startCapture();
}
