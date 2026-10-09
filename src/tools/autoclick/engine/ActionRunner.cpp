#include "ActionRunner.h"
#include "core/Logger.h"

#include <algorithm>
#include <chrono>

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
    m_stopRequested = false;
    m_pauseRequested = false;
}

void ActionRunner::requestStop()
{
    m_stopRequested = true;
    if (!isRunning())
        return;

    m_state = RunnerState::Stopping;
    emit stateChanged(RunnerState::Stopping);
}

void ActionRunner::requestPause()
{
    if (!isRunning() || m_stopRequested)
        return;

    m_pauseRequested = true;
    m_state = RunnerState::Paused;
    emit stateChanged(RunnerState::Paused);
}

void ActionRunner::requestResume()
{
    if (!isRunning() || m_stopRequested)
        return;

    m_pauseRequested = false;
    m_state = RunnerState::Running;
    emit stateChanged(RunnerState::Running);
}

bool ActionRunner::sleepWithCountdown(std::chrono::milliseconds duration, const QString& phase)
{
    using Clock = std::chrono::steady_clock;
    const qint64 tickInterval = 50; // emit tick every 50ms for smooth UI/overlay countdown

    // Đếm ngược theo MỐC THỜI GIAN tuyệt đối (steady_clock) thay vì trừ dần "50ms danh nghĩa" sau mỗi lần
    // msleep(50): Windows ngủ DƯ tới ~15ms mỗi lần (độ phân giải timer mặc định), cộng dồn qua hàng trăm
    // tick làm một lần chờ 5 giây thật ra dài hơn 5 giây thấy rõ - sai số tích lũy theo số vòng lặp.
    auto deadline = Clock::now() + duration;

    for (;;)
    {
        if (m_stopRequested)
            return false;

        if (m_pauseRequested)
        {
            // Thời gian tạm dừng không tính vào thời gian chờ - đẩy mốc kết thúc lùi lại đúng bằng
            // khoảng đã tạm dừng.
            const auto pausedAt = Clock::now();
            while (m_pauseRequested)
            {
                if (m_stopRequested)
                    return false;
                msleep(50);
            }
            deadline += Clock::now() - pausedAt;
        }

        const qint64 remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remaining <= 0)
            break;

        emit countdownTick(remaining, phase);
        msleep(static_cast<unsigned long>(std::min(tickInterval, remaining)));
    }

    emit countdownTick(0, phase);
    return true;
}

bool ActionRunner::executeAction(const Action& action)
{
    m_inputController->clearError();

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

    return !m_inputController->hasError();
}

void ActionRunner::run()
{
    m_state = RunnerState::Running;
    emit stateChanged(RunnerState::Running);

    Logger::instance().info("AutoClick", QString("Starting chain: %1 (Actions: %2, Repeat: %3)")
                                            .arg(QString::fromStdString(m_chain.name))
                                            .arg(m_chain.actions.size())
                                            .arg(m_chain.repeatCount));

    int totalRounds = m_chain.repeatCount;
    int currentRound = 0;

    // Chỉ chạy các hành động đang bật - giữ CHỈ SỐ GỐC của từng hành động (không chép ra danh sách mới
    // đánh số lại từ đầu) để báo đúng hàng cho giao diện qua actionStarted(..., sourceIndex).
    const std::vector<int> enabledIndices = m_chain.enabledActionIndices();

    if (enabledIndices.empty())
    {
        m_state = RunnerState::Finished;
        emit stateChanged(RunnerState::Finished);
        emit chainFinished();
        return;
    }

    QString failure; // khác rỗng = Windows từ chối một thao tác, dừng chuỗi (mục 63 thiết kế: On Error = Stop)

    while (failure.isEmpty())
    {
        if (m_stopRequested)
            break;

        if (totalRounds > 0 && currentRound >= totalRounds)
            break;

        currentRound++;
        emit roundStarted(currentRound, totalRounds);

        for (size_t i = 0; i < enabledIndices.size(); ++i)
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

            const int sourceIndex = enabledIndices[i];
            const auto& act = m_chain.actions[static_cast<size_t>(sourceIndex)];
            QString nextDesc = (i + 1 < enabledIndices.size())
                                   ? m_chain.actions[static_cast<size_t>(enabledIndices[i + 1])].description()
                                   : (currentRound < totalRounds || totalRounds == -1 ? "Next Round" : "End");

            int targetX = act.x;
            int targetY = act.y;
            if (act.type == ActionType::MouseDrag)
            {
                targetX = act.startX;
                targetY = act.startY;
            }

            emit actionStarted(currentRound, totalRounds, static_cast<int>(i + 1), static_cast<int>(enabledIndices.size()),
                               act.description(), nextDesc, targetX, targetY, sourceIndex);

            // logDescription(), KHÔNG phải description(): không ghi nội dung "Gõ văn bản" (thường là mật
            // khẩu) vào tệp log - xem Action.h.
            Logger::instance().info("AutoClick", QString("Round %1/%2 - Action %3/%4: %5")
                                                    .arg(currentRound)
                                                    .arg(totalRounds > 0 ? QString::number(totalRounds) : "Inf")
                                                    .arg(i + 1)
                                                    .arg(enabledIndices.size())
                                                    .arg(act.logDescription()));

            // 1. Wait Before
            if (act.waitBefore.count() > 0)
            {
                if (!sleepWithCountdown(act.waitBefore, "Chờ trước"))
                    break;
            }

            // 2. Execute Action
            if (m_stopRequested)
                break;

            if (!executeAction(act))
            {
                failure = QString("Hành động %1 (%2): %3")
                              .arg(sourceIndex + 1)
                              .arg(act.typeName(), m_inputController->lastError());
                break;
            }

            // 3. Wait After
            if (act.waitAfter.count() > 0)
            {
                if (!sleepWithCountdown(act.waitAfter, "Chờ sau"))
                    break;
            }

            emit actionFinished(static_cast<int>(i + 1));
        }
    }

    if (!failure.isEmpty())
    {
        // Trước đây mọi giá trị trả về của SendInput/SetCursorPos đều bị bỏ qua: chuỗi "chạy" hết các vòng
        // lặp trong khi không thao tác nào tới được cửa sổ đích, người dùng không biết vì sao.
        m_state = RunnerState::Error;
        Logger::instance().error("AutoClick", "Execution aborted: " + failure);
        emit stateChanged(RunnerState::Error);
        emit errorOccurred(failure);
        emit executionStopped();
    }
    else if (m_stopRequested)
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
