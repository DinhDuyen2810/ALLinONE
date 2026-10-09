#include "AutoClickWindow.h"
#include "storage/ActionSerializer.h"
#include "engine/StopHotkey.h"
#include "core/Logger.h"

#include <algorithm>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QCloseEvent>
#include <QIcon>
#include <QStandardPaths>
#include <QTimer>

namespace
{
/// Thư mục gợi ý cho hộp thoại Xuất/Nhập: Documents của người dùng. Trước đây là "profiles" TƯƠNG ĐỐI
/// theo thư mục làm việc của tiến trình - tức thường là thư mục cài đặt (không ghi được nếu cài cho mọi
/// người dùng), và là thư mục khác nhau tùy cách mở ứng dụng.
QString exchangeDir()
{
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return docs.isEmpty() ? QDir::homePath() : docs;
}

ActionChain makeDemoChain()
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
    return defaultChain;
}
} // namespace

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
    connect(m_runner, &ActionRunner::errorOccurred, this, &AutoClickWindow::onRunnerError);
    connect(m_runner, &ActionRunner::chainFinished, this, &AutoClickWindow::onRunnerFinished);
    connect(m_runner, &ActionRunner::executionStopped, this, &AutoClickWindow::onRunnerFinished);

    // Overlay stop button
    connect(m_overlay, &RuntimeOverlay::stopClicked, this, &AutoClickWindow::onStopClicked);

    // Capture signals
    connect(m_actionEditorWidget, &ActionEditorWidget::captureRequested, this, &AutoClickWindow::onCaptureRequested);
    connect(m_mouseCapture, &MouseCapture::pointCaptured, m_actionEditorWidget, &ActionEditorWidget::onPositionCaptured);
    connect(m_actionEditorWidget, &ActionEditorWidget::dragGestureCaptureRequested, m_mouseCapture, &MouseCapture::startDragGestureCapture);
    connect(m_mouseCapture, &MouseCapture::dragGestureCaptured, m_actionEditorWidget, &ActionEditorWidget::onDragGestureCaptured);
    connect(m_mouseCapture, &MouseCapture::captureFailed, this, [this](const QString& message) {
        QMessageBox::warning(this, "Không bắt được thao tác", message);
    });

    loadDefaultProfile();
}

AutoClickWindow::~AutoClickWindow()
{
    unregisterStopHotkey();
    if (m_overlay)
    {
        m_overlay->close();
        delete m_overlay;
        m_overlay = nullptr;
    }
}

void AutoClickWindow::closeEvent(QCloseEvent* event)
{
    if (m_runner && m_runner->isRunning())
    {
        auto reply = QMessageBox::question(this, "Đang thực thi", "Chuỗi thao tác đang chạy. Bạn có muốn dừng lại và đóng cửa sổ?", QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes)
        {
            event->ignore();
            return;
        }
        m_runner->requestStop();
        // Cửa sổ này đóng là chỉ ẨN đi (AutoClickTool tái dùng nó) - tuyệt đối không được ẩn trong khi
        // luồng chạy còn đang điều khiển chuột/bàn phím: lúc đó cả cửa sổ lẫn HUD (nơi có nút Dừng) đều
        // đã biến mất. Trước đây kết quả wait() bị bỏ qua. Mọi khoảng ngủ của ActionRunner/InputController
        // nay đều kiểm tra cờ dừng mỗi <=50ms nên bình thường luồng thoát gần như tức thì.
        if (!m_runner->wait(3000))
        {
            m_statusLabel->setText("Trạng thái: Chưa dừng được chuỗi - thử đóng lại sau");
            event->ignore();
            return;
        }
    }
    unregisterStopHotkey();
    if (m_overlay) m_overlay->hide();
    event->accept();
}

void AutoClickWindow::stopForQuit()
{
    if (m_runner && m_runner->isRunning())
    {
        m_runner->requestStop();
        m_runner->wait(2000); // có giới hạn - không được treo quá trình thoát ứng dụng
    }
    unregisterStopHotkey();
    if (m_overlay) m_overlay->hide();
}

bool AutoClickWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    // WM_HOTKEY của phím dừng toàn cục (chỉ đăng ký trong lúc chuỗi đang chạy - xem registerStopHotkey()).
    if (m_stopHotkeyRegistered && StopHotkey::isStopMessage(message))
    {
        onStopClicked();
        if (result) *result = 0;
        return true;
    }
    return QWidget::nativeEvent(eventType, message, result);
}

void AutoClickWindow::registerStopHotkey()
{
    if (!m_stopHotkeyRegistered)
        m_stopHotkeyRegistered = StopHotkey::registerFor(winId());

    if (!m_stopHotkeyRegistered)
    {
        // Thường do ứng dụng khác đã chiếm đúng tổ hợp này - vẫn cho chạy, chỉ là phải dừng bằng nút.
        Logger::instance().warning("AutoClick", QString("Không đăng ký được phím dừng toàn cục %1 - chỉ dừng "
                                                        "được bằng nút Dừng.").arg(StopHotkey::label()));
    }
    m_overlay->setStopHotkeyHint(m_stopHotkeyRegistered ? StopHotkey::label() : QString());
}

void AutoClickWindow::unregisterStopHotkey()
{
    if (!m_stopHotkeyRegistered)
        return;
    StopHotkey::unregisterFor(winId());
    m_stopHotkeyRegistered = false;
}

void AutoClickWindow::clearRunningMarker()
{
    m_actionListWidget->setRunningRow(-1);
}

void AutoClickWindow::noteActionsRestructured()
{
    if (m_runningChainIndex >= 0 && m_currentChainIndex == m_runningChainIndex)
    {
        m_runningChainRestructured = true;
        clearRunningMarker();
    }
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
    // Danh sách hành động rộng hơn, Cài đặt hẹp hơn một chút - cột Mô tả trước đây bị cắt cụt quá sớm
    // ("Clic...", "Gõ ...") do panel Cài đặt chiếm quá nhiều chỗ so với nhu cầu thực tế của nó.
    m_splitter->setStretchFactor(0, 2); // 20%
    m_splitter->setStretchFactor(1, 6); // 60%
    m_splitter->setStretchFactor(2, 2); // 20%
    mainLayout->addWidget(m_splitter, 1);

    // Wire Chain signals
    connect(m_chainListWidget, &ChainListWidget::chainSelectionChanged, this, &AutoClickWindow::onChainSelected);
    connect(m_chainListWidget, &ChainListWidget::addChainClicked, this, &AutoClickWindow::onAddChain);
    connect(m_chainListWidget, &ChainListWidget::cloneChainClicked, this, &AutoClickWindow::onCloneChain);
    connect(m_chainListWidget, &ChainListWidget::deleteChainClicked, this, &AutoClickWindow::onDeleteChain);
    connect(m_chainListWidget, &ChainListWidget::renameChainClicked, this, &AutoClickWindow::onRenameChain);
    connect(m_chainListWidget, &ChainListWidget::runChainClicked, this, [this](int index) { startChain(index); });

    // Wire Action signals
    connect(m_actionListWidget, &ActionListWidget::actionSelectionChanged, this, &AutoClickWindow::onActionSelected);
    connect(m_actionListWidget, &ActionListWidget::addActionClicked, this, &AutoClickWindow::onAddAction);
    connect(m_actionListWidget, &ActionListWidget::editActionClicked, this, &AutoClickWindow::onEditAction);
    connect(m_actionListWidget, &ActionListWidget::cloneActionClicked, this, &AutoClickWindow::onCloneAction);
    connect(m_actionListWidget, &ActionListWidget::deleteActionClicked, this, &AutoClickWindow::onDeleteAction);
    connect(m_actionListWidget, &ActionListWidget::actionMoved, this, &AutoClickWindow::onActionMoved);
    connect(m_actionEditorWidget, &ActionEditorWidget::actionSaved, this, &AutoClickWindow::onActionSaved);
    connect(m_actionEditorWidget, &ActionEditorWidget::dirtyChanged, this, &AutoClickWindow::onEditorDirtyChanged);

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
    // Chỉ cho nhập số, bỏ nút mũi tên tăng/giảm theo yêu cầu - gõ số trực tiếp nhanh hơn khi cần số lần
    // lặp lớn (vd vài trăm/nghìn lần).
    m_repeatSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_repeatSpin->setAlignment(Qt::AlignCenter);
    m_repeatSpin->setStyleSheet("background-color: #ffffff; color: #1f2328; border: 1px solid #d0d7de; border-radius: 8px; padding: 4px; font-weight: bold;");

    m_infiniteCheck = new QCheckBox("Vô hạn (Infinite)", this);
    m_infiniteCheck->setStyleSheet("color: #57606a; font-weight: 500;");
    connect(m_infiniteCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_repeatSpin->setEnabled(!checked);
        updateRepeatFromUi();
    });
    connect(m_repeatSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int) {
        updateRepeatFromUi();
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

bool AutoClickWindow::backupCorruptDefaultProfile(QString* backupPath)
{
    const QString path = ActionSerializer::getDefaultProfilePath();
    const QString backup = path + "." + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".bak";
    if (!QFile::rename(path, backup))
        return false;
    if (backupPath) *backupPath = backup;
    return true;
}

void AutoClickWindow::loadDefaultProfile()
{
    const QString path = ActionSerializer::getDefaultProfilePath();
    QString notice;

    if (QFileInfo::exists(path))
    {
        QString loadError;
        if (!ActionSerializer::loadFromFile(path, m_chains, &loadError))
        {
            // Tệp CÓ nhưng không đọc được (JSON hỏng/sai định dạng). Trước đây trường hợp này đi chung
            // nhánh với "chưa có tệp": tạo chuỗi mẫu rồi GHI ĐÈ luôn lên tệp của người dùng - mất sạch
            // mọi chuỗi đã lưu chỉ vì một ký tự hỏng. Nay đổi tên tệp hỏng thành .bak để giữ lại, rồi
            // mới tạo tệp mới.
            m_chains.clear();
            QString backupPath;
            if (backupCorruptDefaultProfile(&backupPath))
            {
                notice = QString("Tệp hồ sơ Auto Click không đọc được (%1).\n\nBản gốc đã được giữ lại tại:\n%2\n\n"
                                 "Đã tạo một hồ sơ mẫu mới.")
                             .arg(loadError, QDir::toNativeSeparators(backupPath));
            }
            else
            {
                m_corruptDefaultProfilePending = true;
                notice = QString("Tệp hồ sơ Auto Click không đọc được (%1) và không đổi tên được để giữ lại:\n%2\n\n"
                                 "Chuỗi mẫu đang hiển thị CHƯA được lưu - tệp gốc vẫn còn nguyên.")
                             .arg(loadError, QDir::toNativeSeparators(path));
            }
            Logger::instance().warning("AutoClick", QString(notice).replace('\n', ' '));
        }
    }

    if (m_chains.empty())
    {
        m_chains.push_back(makeDemoChain());
        QString saveError;
        if (!m_corruptDefaultProfilePending && !ActionSerializer::saveToFile(path, m_chains, &saveError))
            Logger::instance().warning("AutoClick", QString("Không ghi được hồ sơ mẫu vào %1: %2").arg(path, saveError));
    }

    m_chainListWidget->setChains(m_chains);
    syncUiWithCurrentChain();

    if (!notice.isEmpty())
    {
        m_statusLabel->setText("Trạng thái: Hồ sơ cũ bị hỏng - đã tạo hồ sơ mẫu");
        // Hàm này chạy trong constructor (cửa sổ chưa hiện) - hoãn hộp thoại tới khi vòng lặp sự kiện chạy.
        QTimer::singleShot(0, this, [this, notice]() {
            if (isVisible())
                QMessageBox::warning(this, "Hồ sơ Auto Click bị hỏng", notice);
        });
    }
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
    m_syncingUi = true;
    // Dấu "▶ đang chạy" thuộc về chain đang chạy - chuyển sang xem chain khác thì bỏ (hàng cùng số thứ tự
    // của chain khác không phải hành động đang chạy).
    if (m_currentChainIndex != m_runningChainIndex)
        clearRunningMarker();
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
    m_syncingUi = false;
}

void AutoClickWindow::updateRepeatFromUi()
{
    if (m_syncingUi)
        return;
    if (ActionChain* chain = currentChain())
        chain->repeatCount = m_infiniteCheck->isChecked() ? -1 : m_repeatSpin->value();
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
    // Giữ chỉ số đích trong biến CỤC BỘ: setChains() dựng lại danh sách rồi tự chọn lại hàng CŨ, phát
    // chainSelectionChanged -> onChainSelected() ghi đè m_currentChainIndex về hàng cũ đó. Trước đây dòng
    // setSelectedChainIndex(m_currentChainIndex) ngay sau vì thế chọn lại đúng hàng cũ - chuỗi vừa thêm/
    // nhân bản không bao giờ được chọn.
    const int newIndex = static_cast<int>(m_chains.size()) - 1;
    m_chainListWidget->setChains(m_chains);
    m_chainListWidget->setSelectedChainIndex(newIndex);
    m_currentChainIndex = newIndex;
    syncUiWithCurrentChain();
}

void AutoClickWindow::onCloneChain(int index)
{
    if (index >= 0 && index < static_cast<int>(m_chains.size()))
    {
        ActionChain cloned = m_chains[index].clone();
        m_chains.push_back(cloned);
        const int newIndex = static_cast<int>(m_chains.size()) - 1; // biến cục bộ - xem onAddChain()
        m_chainListWidget->setChains(m_chains);
        m_chainListWidget->setSelectedChainIndex(newIndex);
        m_currentChainIndex = newIndex;
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
        // Chain đang chạy (ActionRunner giữ bản sao riêng nên vẫn chạy tiếp được) bị xóa hoặc bị dồn chỉ số.
        if (index == m_runningChainIndex)
        {
            m_runningChainIndex = -1;
            clearRunningMarker();
        }
        else if (index < m_runningChainIndex)
        {
            --m_runningChainIndex;
        }
        const int newIndex = std::max(0, index - 1); // biến cục bộ - xem onAddChain()
        m_chainListWidget->setChains(m_chains);
        m_chainListWidget->setSelectedChainIndex(newIndex);
        m_currentChainIndex = newIndex;
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

    noteActionsRestructured();
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
        noteActionsRestructured();
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
        noteActionsRestructured();
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

void AutoClickWindow::onEditorDirtyChanged(bool dirty)
{
    m_actionListWidget->setRowDirty(m_actionListWidget->selectedActionIndex(), dirty);
}

void AutoClickWindow::onActionMoved(int from, int to)
{
    ActionChain* chain = currentChain();
    if (!chain) return;

    const int count = static_cast<int>(chain->actions.size());
    if (from < 0 || from >= count || to < 0 || to >= count || from == to)
        return;

    // Di chuyển đúng MỘT phần tử từ chỉ số from sang đúng chỉ số to (giữ nguyên thứ tự tương đối của
    // mọi phần tử còn lại) - cách chuẩn dùng std::rotate, tránh tính sai lệch chỉ số dễ gặp nếu tự
    // erase() rồi insert() thủ công (chỉ số to đổi ý nghĩa ngay sau khi erase phần tử phía trước nó).
    noteActionsRestructured();
    auto& actions = chain->actions;
    if (from < to)
        std::rotate(actions.begin() + from, actions.begin() + from + 1, actions.begin() + to + 1);
    else
        std::rotate(actions.begin() + to, actions.begin() + from, actions.begin() + from + 1);

    m_actionListWidget->setActions(chain->actions);
    m_actionListWidget->setSelectedActionIndex(to);
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
    startChain(m_currentChainIndex);
}

void AutoClickWindow::startChain(int index)
{
    // Lần chạy trước chưa được onRunnerFinished() xử lý xong (kể cả khi luồng đã thoát nhưng tín hiệu kết
    // thúc còn nằm trong hàng đợi sự kiện): không bắt đầu lần mới. Nếu cho chạy, tín hiệu kết thúc CŨ tới
    // sau sẽ ẩn HUD, hủy phím dừng toàn cục và bật lại nút Chạy ngay giữa lúc chuỗi MỚI đang chạy (đường
    // vào: menu chuột phải "Chạy chuỗi này" không bị vô hiệu hóa như nút RUN).
    if (m_runActive)
        return;

    if (m_runner->isRunning())
    {
        // Thread có thể vừa phát tín hiệu kết thúc nhưng chưa thoát hẳn
        if (!m_runner->wait(300))
            return;
    }

    if (index < 0 || index >= static_cast<int>(m_chains.size()))
        return;

    if (index != m_currentChainIndex)
    {
        m_currentChainIndex = index;
        m_chainListWidget->setSelectedChainIndex(index);
        syncUiWithCurrentChain();
    }

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

    m_lastRunError.clear();
    m_runActive = true;
    m_runningChainIndex = m_currentChainIndex;
    m_runningChainRestructured = false;
    clearRunningMarker();

    // Phím dừng toàn cục CHỈ có hiệu lực trong lúc chuỗi chạy (hủy ở onRunnerFinished) - đăng ký thất
    // bại không chặn việc chạy.
    registerStopHotkey();

    // Thu nhỏ cửa sổ quản lý TRƯỚC khi bắt đầu thao tác thật - tránh chính cửa sổ này che/nhận nhầm
    // click/gõ phím của chuỗi hành động (vd nếu vị trí click trùng với cửa sổ quản lý). HUD (RuntimeOverlay)
    // vẫn hiện riêng để theo dõi tiến trình.
    showMinimized();
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
        case RunnerState::Error:
            m_statusLabel->setText("Trạng thái: Đã dừng do lỗi (Error)");
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
                                           const QString& currentDesc, const QString& nextDesc, int targetX, int targetY,
                                           int sourceIndex)
{
    m_overlay->setActionInfo(actionIndex, totalActions, currentDesc, nextDesc, targetX, targetY);

    // Ba lỗi của cách cũ (setSelectedActionIndex(actionIndex - 1)):
    //  - ĐỔI HÀNG ĐANG CHỌN kéo theo nạp lại ActionEditorWidget -> xóa sạch chỉnh sửa người dùng đang gõ
    //    dở mà chưa bấm "Lưu hành động", ở MỖI hành động của chuỗi đang chạy.
    //  - actionIndex đếm trong các hành động ĐANG BẬT, không phải số hàng (lệch khi có hành động bị tắt).
    //  - Tô luôn cả khi người dùng đang xem một chain KHÁC chain đang chạy.
    // Nay chỉ ĐÁNH DẤU hàng (không đụng lựa chọn/editor), theo chỉ số gốc, và chỉ khi danh sách đang hiện
    // đúng là chain đang chạy với cấu trúc chưa bị sửa.
    const bool showingRunningChain =
        m_runningChainIndex >= 0 && m_currentChainIndex == m_runningChainIndex && !m_runningChainRestructured;
    m_actionListWidget->setRunningRow(showingRunningChain ? sourceIndex : -1);
}

void AutoClickWindow::onRunnerError(const QString& message)
{
    // Chỉ ghi nhận - hiện cho người dùng ở onRunnerFinished() (executionStopped tới ngay sau), khi cửa
    // sổ đã được khôi phục khỏi trạng thái thu nhỏ.
    m_lastRunError = message;
}

void AutoClickWindow::onRunnerCountdownTick(qint64 remainingMs, const QString& phase)
{
    m_overlay->setCountdown(remainingMs, phase);
}

void AutoClickWindow::onRunnerFinished()
{
    unregisterStopHotkey();
    m_runActive = false;
    m_runningChainIndex = -1;
    m_runningChainRestructured = false;
    clearRunningMarker();

    m_runButton->setEnabled(true);
    m_stopButton->setEnabled(false);
    m_overlay->hide();
    // Không đặt lại nhãn trạng thái ở đây: onRunnerStateChanged() vừa đặt "Hoàn tất"/"Sẵn sàng"/"Lỗi" ngay
    // trước tín hiệu này - trước đây dòng ghi đè "Sẵn sàng (Idle)" làm "Hoàn tất" không bao giờ kịp hiện.

    // startChain() đã thu nhỏ cửa sổ này - chạy xong thì đưa nó trở lại (trước đây người dùng phải tự
    // tìm lại trên thanh tác vụ). Chỉ khi nó còn đang hiện-và-thu-nhỏ: nếu người dùng đã ĐÓNG cửa sổ giữa
    // lúc chạy (closeEvent tự dừng chuỗi) thì không tự bật nó lên lại.
    if (isVisible() && isMinimized())
    {
        setWindowState((windowState() & ~Qt::WindowMinimized) | Qt::WindowActive);
        raise();
        activateWindow();
    }

    if (!m_lastRunError.isEmpty())
    {
        const QString error = m_lastRunError;
        m_lastRunError.clear();
        if (isVisible())
        {
            QMessageBox::warning(this, "Auto Click đã dừng do lỗi",
                                 error + "\n\nChuỗi hành động đã dừng tại bước này. Nếu cửa sổ đích chạy với quyền "
                                         "Administrator, hãy chạy One for ALL cùng mức quyền.");
        }
    }
}

void AutoClickWindow::onSaveProfile()
{
    QString path = ActionSerializer::getDefaultProfilePath();

    // Tệp mặc định hỏng mà lúc mở cửa sổ chưa đổi tên giữ lại được (xem loadDefaultProfile()) - thử lại
    // trước khi ghi; vẫn không được thì KHÔNG ghi đè lên nó.
    if (m_corruptDefaultProfilePending)
    {
        if (QFileInfo::exists(path) && !backupCorruptDefaultProfile())
        {
            QMessageBox::critical(this, "Lỗi",
                                  "Tệp hồ sơ cũ bị hỏng và không đổi tên được để giữ lại, nên chưa lưu đè lên nó:\n" +
                                      QDir::toNativeSeparators(path) + "\n\nHãy dùng \"Xuất file\" để lưu ra nơi khác.");
            return;
        }
        m_corruptDefaultProfilePending = false;
    }

    QString error;
    if (ActionSerializer::saveToFile(path, m_chains, &error))
    {
        QMessageBox::information(this, "Lưu cấu hình", "Đã lưu thành công cấu hình vào: " + QDir::toNativeSeparators(path));
    }
    else
    {
        QMessageBox::critical(this, "Lỗi",
                              QString("Không thể lưu cấu hình vào: %1\n\n%2").arg(QDir::toNativeSeparators(path), error));
    }
}

void AutoClickWindow::onLoadProfile()
{
    QString path = ActionSerializer::getDefaultProfilePath();
    std::vector<ActionChain> loaded;
    QString error;
    if (!ActionSerializer::loadFromFile(path, loaded, &error))
    {
        QMessageBox::critical(this, "Lỗi",
                              QString("Không thể nạp cấu hình từ: %1\n\n%2").arg(QDir::toNativeSeparators(path), error));
        return;
    }
    if (loaded.empty())
    {
        QMessageBox::warning(this, "Nạp cấu hình", "Tệp cấu hình đã lưu không có chuỗi hành động nào.");
        return;
    }

    // Nạp lại THAY TOÀN BỘ danh sách chain đang có trong bộ nhớ - kể cả những gì chưa bấm Lưu cấu hình.
    // Trước đây làm ngay không hỏi.
    if (!m_chains.empty() &&
        QMessageBox::question(this, "Nạp cấu hình",
                              QString("Nạp lại sẽ thay toàn bộ %1 chuỗi đang có bằng %2 chuỗi trong tệp đã lưu. "
                                      "Mọi thay đổi chưa lưu sẽ mất.\n\nTiếp tục?")
                                  .arg(m_chains.size())
                                  .arg(loaded.size()),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }

    m_chains = loaded;
    m_runningChainIndex = -1; // chain đang chạy (nếu có) không còn nằm trong danh sách mới
    clearRunningMarker();
    m_chainListWidget->setChains(m_chains);
    m_chainListWidget->setSelectedChainIndex(0);
    m_currentChainIndex = 0;
    syncUiWithCurrentChain();
    QMessageBox::information(this, "Nạp cấu hình", "Đã nạp thành công cấu hình từ: " + QDir::toNativeSeparators(path));
}

void AutoClickWindow::onExportProfile()
{
    QString path = QFileDialog::getSaveFileName(this, "Xuất cấu hình Auto Click",
                                                exchangeDir() + "/autoclick_export.json", "JSON Files (*.json)");
    if (!path.isEmpty())
    {
        QString error;
        if (ActionSerializer::saveToFile(path, m_chains, &error))
        {
            QMessageBox::information(this, "Xuất file", "Đã xuất cấu hình ra file thành công!");
        }
        else
        {
            QMessageBox::critical(this, "Lỗi", "Không thể xuất file.\n\n" + error);
        }
    }
}

void AutoClickWindow::onImportProfile()
{
    QString path = QFileDialog::getOpenFileName(this, "Nhập cấu hình Auto Click", exchangeDir(), "JSON Files (*.json)");
    if (path.isEmpty())
        return;

    std::vector<ActionChain> loaded;
    QString error;
    if (!ActionSerializer::loadFromFile(path, loaded, &error))
    {
        QMessageBox::critical(this, "Lỗi", "File không hợp lệ hoặc lỗi phân tích cú pháp JSON.\n\n" + error);
        return;
    }
    if (loaded.empty())
    {
        QMessageBox::critical(this, "Lỗi", "File không có chuỗi hành động nào.");
        return;
    }

    // Nhập THAY TOÀN BỘ danh sách chain đang có (không gộp thêm) - hỏi trước, xem onLoadProfile().
    if (!m_chains.empty() &&
        QMessageBox::question(this, "Nhập file",
                              QString("Nhập file sẽ thay toàn bộ %1 chuỗi đang có bằng %2 chuỗi trong file. "
                                      "Mọi thay đổi chưa lưu sẽ mất.\n\nTiếp tục?")
                                  .arg(m_chains.size())
                                  .arg(loaded.size()),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
    {
        return;
    }

    m_chains = loaded;
    m_runningChainIndex = -1;
    clearRunningMarker();
    m_chainListWidget->setChains(m_chains);
    m_chainListWidget->setSelectedChainIndex(0);
    m_currentChainIndex = 0;
    syncUiWithCurrentChain();
    QMessageBox::information(this, "Nhập file", "Đã nhập cấu hình thành công!");
}

void AutoClickWindow::onCaptureRequested(int /*targetField*/)
{
    m_mouseCapture->startCapture();
}
