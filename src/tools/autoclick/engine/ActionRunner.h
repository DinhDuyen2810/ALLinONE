#pragma once

#include <QThread>
#include <atomic>
#include <memory>
#include "../model/ActionChain.h"
#include "InputController.h"

enum class RunnerState
{
    Idle,
    Running,
    Paused,
    Stopping,
    Finished,
    Error
};

class ActionRunner : public QThread
{
    Q_OBJECT

public:
    explicit ActionRunner(QObject* parent = nullptr);
    ~ActionRunner() override;

    void setChain(const ActionChain& chain);
    void requestStop();
    void requestPause();
    void requestResume();

    bool isRunningState() const { return m_state == RunnerState::Running; }
    RunnerState runnerState() const { return m_state; }

signals:
    void stateChanged(RunnerState state);
    void roundStarted(int currentRound, int totalRounds);
    /// actionIndex/totalActions đếm trong các hành động ĐANG BẬT (để hiện "Hành động 3/6" trên HUD);
    /// sourceIndex là chỉ số gốc (từ 0) của hành động đó trong ActionChain::actions - giao diện dùng số
    /// này để tô đúng hàng trong danh sách (hai số lệch nhau khi có hành động bị tắt đứng trước).
    void actionStarted(int currentRound, int totalRounds, int actionIndex, int totalActions,
                       const QString& currentDesc, const QString& nextDesc, int targetX, int targetY,
                       int sourceIndex);
    void countdownTick(qint64 remainingMs, const QString& phase);
    void actionFinished(int actionIndex);
    void chainFinished();
    void executionStopped();
    void errorOccurred(const QString& message);

protected:
    void run() override;

private:
    bool sleepWithCountdown(std::chrono::milliseconds duration, const QString& phase);
    /// Trả về false nếu Windows từ chối thao tác (xem InputController::hasError()).
    bool executeAction(const Action& action);

    ActionChain m_chain;
    std::atomic_bool m_stopRequested{false};
    std::atomic_bool m_pauseRequested{false};
    std::atomic<RunnerState> m_state{RunnerState::Idle};

    std::unique_ptr<InputController> m_inputController;
};
