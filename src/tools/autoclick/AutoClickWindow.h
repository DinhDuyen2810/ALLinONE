#pragma once

#include <QWidget>
#include <QSplitter>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <memory>
#include <vector>

#include "model/ActionChain.h"
#include "widgets/ChainListWidget.h"
#include "widgets/ActionListWidget.h"
#include "widgets/ActionEditorWidget.h"
#include "engine/ActionRunner.h"
#include "capture/MouseCapture.h"
#include "overlay/RuntimeOverlay.h"

class AutoClickWindow : public QWidget
{
    Q_OBJECT

public:
    explicit AutoClickWindow(QWidget* parent = nullptr);
    ~AutoClickWindow() override;

    /// Dừng NGAY chuỗi đang chạy (nếu có), không hỏi xác nhận, chờ luồng chạy kết thúc trong thời gian
    /// giới hạn - dùng cho AutoClickTool::stopBackgroundWorkForQuit() khi ứng dụng thoát theo đường
    /// KHÔNG đi qua closeEvent() của cửa sổ này (xem core/Tool.h).
    void stopForQuit();

private slots:
    // Chain slots
    void onChainSelected(int index);
    void onAddChain();
    void onCloneChain(int index);
    void onDeleteChain(int index);
    void onRenameChain(int index, const QString& newName);

    // Action slots
    void onActionSelected(int index);
    void onAddAction();
    void onEditAction(int index);
    void onCloneAction(int index);
    void onDeleteAction(int index);
    void onActionMoved(int from, int to);
    void onActionSaved(const Action& action, int actionIndex);
    void onEditorDirtyChanged(bool dirty);

    // Runner slots
    void onRunClicked();
    void onStopClicked();
    void onRunnerStateChanged(RunnerState state);
    void onRunnerRoundStarted(int currentRound, int totalRounds);
    void onRunnerActionStarted(int currentRound, int totalRounds, int actionIndex, int totalActions,
                               const QString& currentDesc, const QString& nextDesc, int targetX, int targetY,
                               int sourceIndex);
    void onRunnerCountdownTick(qint64 remainingMs, const QString& phase);
    void onRunnerError(const QString& message);
    void onRunnerFinished();

    // Storage slots
    void onSaveProfile();
    void onLoadProfile();
    void onExportProfile();
    void onImportProfile();

    // Capture slot
    void onCaptureRequested(int targetField);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void setupUi();
    void loadDefaultProfile();
    void syncUiWithCurrentChain();
    void updateRepeatFromUi();
    void startChain(int index);
    ActionChain* currentChain();
    void registerStopHotkey();
    void unregisterStopHotkey();
    /// Gọi khi THÊM/XÓA/ĐỔI CHỖ hành động của chain đang xem - nếu đó chính là chain đang chạy thì chỉ
    /// số hàng không còn khớp với bản sao ActionRunner đang giữ, phải thôi tô hàng "đang chạy".
    void noteActionsRestructured();
    void clearRunningMarker();
    /// Đổi tên tệp hồ sơ mặc định bị hỏng thành "<tên>.<thời điểm>.bak" để KHÔNG BAO GIỜ ghi đè lên nó.
    bool backupCorruptDefaultProfile(QString* backupPath = nullptr);

    std::vector<ActionChain> m_chains;
    int m_currentChainIndex{0};
    bool m_syncingUi{false};

    // Trạng thái của lần chạy hiện tại
    bool m_runActive{false};            // true từ startChain() tới khi onRunnerFinished() ĐÃ xử lý xong lần chạy đó
    int m_runningChainIndex{-1};        // chỉ số (trong m_chains) của chain đang chạy, -1 = không chạy/không còn xác định
    bool m_runningChainRestructured{false};
    bool m_stopHotkeyRegistered{false};
    QString m_lastRunError;             // lỗi ActionRunner báo về, hiện cho người dùng khi lần chạy kết thúc
    bool m_corruptDefaultProfilePending{false}; // tệp mặc định hỏng mà CHƯA đổi tên được - cấm ghi đè

    // UI Widgets
    QSplitter* m_splitter;
    ChainListWidget* m_chainListWidget;
    ActionListWidget* m_actionListWidget;
    ActionEditorWidget* m_actionEditorWidget;

    // Toolbar controls
    QPushButton* m_runButton;
    QPushButton* m_stopButton;
    QSpinBox* m_repeatSpin;
    QCheckBox* m_infiniteCheck;
    QPushButton* m_saveButton;
    QPushButton* m_loadButton;
    QPushButton* m_exportButton;
    QPushButton* m_importButton;
    QLabel* m_statusLabel;

    // Components
    ActionRunner* m_runner;
    RuntimeOverlay* m_overlay;
    MouseCapture* m_mouseCapture;
};
