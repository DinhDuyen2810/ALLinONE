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
                               const QString& currentDesc, const QString& nextDesc, int targetX, int targetY);
    void onRunnerCountdownTick(qint64 remainingMs, const QString& phase);
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

private:
    void setupUi();
    void loadDefaultProfile();
    void syncUiWithCurrentChain();
    void updateRepeatFromUi();
    void startChain(int index);
    ActionChain* currentChain();

    std::vector<ActionChain> m_chains;
    int m_currentChainIndex{0};
    bool m_syncingUi{false};

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
