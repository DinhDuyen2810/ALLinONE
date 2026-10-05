#include "ActionRunner.h"
#include "core/Logger.h"
#include <QDateTime>

ActionRunner::ActionRunner(QObject* parent)
    : QThread(parent)
    , m_inputController(std::make_unique<InputController>())
{
}

ActionRunner::~ActionRunner()
{
    requestStop();
    wait(3000);
}

void ActionRunner::setChain(const ActionChain& chain)
{
    m_chain = chain;
}

void ActionRunner::requestStop()
{
    m_stopRequested = true;
    m_state = RunnerState::Stopping;
    emit stateChanged(RunnerState::Stopping);
}

void ActionRunner::requestPause()
{
    m_pauseRequested = true;
    m_state = RunnerState::Paused;
    emit stateChanged(RunnerState::Paused);
}

void ActionRunner::requestResume()
{
    m_pauseRequested = false;
    m_state = RunnerState::Running;
    emit stateChanged(RunnerState::Running);
}

bool ActionRunner::sleepWithCountdown(std::chrono::milliseconds duration, const QString& phase)
{
    qint64 totalMs = duration.count();
    qint64 remaining = totalMs;
    const int tickInterval = 50; // emit tick every 50ms for smooth UI/overlay countdown

    while (remaining > 0)
    {
        if (m_stopRequested)
            return false;

        while (m_pauseRequested)
        {
            if (m_stopRequested)
                return false;
            msleep(50);
        }

        emit countdownTick(remaining, phase);

        int sleepTime = std::min(static_cast<qint64>(tickInterval), remaining);
        msleep(sleepTime);
        remaining -= sleepTime;
    }

    emit countdownTick(0, phase);
    return true;
}

void ActionRunner::executeAction(const Action& action)
{
    switch (action.type)
    {
        case ActionType::MouseClick:
            m_inputController->moveMouse(action.x, action.y);
            msleep(20);
            m_inputController->click(action.mouseButton);
            break;

        case ActionType::MouseDrag:
            m_inputController->drag(action.startX, action.startY, action.endX, action.endY, action.duration, &m_stopRequested);
            break;

        case ActionType::MouseHold:
            m_inputController->hold(action.x, action.y, action.mouseButton, action.duration, &m_stopRequested);
            break;

        case ActionType::TypeText:
            m_inputController->typeText(action.text, action.textMode, action.duration, &m_stopRequested);
            break;

        case ActionType::Hotkey:
            m_inputController->hotkey(action.modCtrl, action.modAlt, action.modShift, action.modWin, action.keyCode);
            break;

        case ActionType::KeyPress:
            m_inputController->pressKey(action.keyCode);
            break;

        case ActionType::Scroll:
            m_inputController->scroll(action.scrollDirection, action.scrollAmount, action.duration, &m_stopRequested);
            break;
    }
}

void ActionRunner::run()
{
    m_stopRequested = false;
    m_pauseRequested = false;
    m_state = RunnerState::Running;
    emit stateChanged(RunnerState::Running);

    Logger::instance().info("AutoClick", QString("Starting chain: %1 (Actions: %2, Repeat: %3)")
                                            .arg(QString::fromStdString(m_chain.name))
                                            .arg(m_chain.actions.size())
                                            .arg(m_chain.repeatCount));

    int totalRounds = m_chain.repeatCount;
    int currentRound = 0;

    // Filter enabled actions
    std::vector<Action> enabledActions;
    for (const auto& act : m_chain.actions)
    {
        if (act.enabled)
            enabledActions.push_back(act);
    }

    if (enabledActions.empty())
    {
        m_state = RunnerState::Finished;
        emit stateChanged(RunnerState::Finished);
        emit chainFinished();
        return;
    }

    while (true)
    {
        if (m_stopRequested)
            break;

        if (totalRounds > 0 && currentRound >= totalRounds)
            break;

        currentRound++;
        emit roundStarted(currentRound, totalRounds);

        for (size_t i = 0; i < enabledActions.size(); ++i)
        {
            if (m_stopRequested)
                break;

            while (m_pauseRequested)
            {
                if (m_stopRequested)
                    break;
                msleep(50);
            }
            if (m_stopRequested)
                break;

            const auto& act = enabledActions[i];
            QString nextDesc = (i + 1 < enabledActions.size()) ? enabledActions[i + 1].description() : (currentRound < totalRounds || totalRounds == -1 ? "Next Round" : "End");

            int targetX = act.x;
            int targetY = act.y;
            if (act.type == ActionType::MouseDrag)
            {
                targetX = act.startX;
                targetY = act.startY;
            }

            emit actionStarted(currentRound, totalRounds, static_cast<int>(i + 1), static_cast<int>(enabledActions.size()),
                               act.description(), nextDesc, targetX, targetY);

            Logger::instance().info("AutoClick", QString("Round %1/%2 - Action %3/%4: %5")
                                                    .arg(currentRound)
                                                    .arg(totalRounds > 0 ? QString::number(totalRounds) : "Inf")
                                                    .arg(i + 1)
                                                    .arg(enabledActions.size())
                                                    .arg(act.description()));

            // 1. Wait Before
            if (act.waitBefore.count() > 0)
            {
                if (!sleepWithCountdown(act.waitBefore, "Wait Before"))
                    break;
            }

            // 2. Execute Action
            if (m_stopRequested)
                break;

            executeAction(act);

            // 3. Wait After
            if (act.waitAfter.count() > 0)
            {
                if (!sleepWithCountdown(act.waitAfter, "Wait After"))
                    break;
            }

            emit actionFinished(static_cast<int>(i + 1));
        }
    }

    if (m_stopRequested)
    {
        m_state = RunnerState::Idle;
        emit stateChanged(RunnerState::Idle);
        emit executionStopped();
        Logger::instance().info("AutoClick", "Execution stopped by user.");
    }
    else
    {
        m_state = RunnerState::Finished;
        emit stateChanged(RunnerState::Finished);
        emit chainFinished();
        Logger::instance().info("AutoClick", "Execution finished successfully.");
    }
}
