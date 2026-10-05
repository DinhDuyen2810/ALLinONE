#pragma once

#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QVBoxLayout>

/**
 * @brief HUD overlay hiển thị trạng thái thực thi chuỗi hành động và thời gian đếm ngược.
 * Tuân thủ mục 38, 39, 40, 41 trong tài liệu thiết kế.
 */
class RuntimeOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit RuntimeOverlay(QWidget* parent = nullptr);

    void setRoundInfo(int currentRound, int totalRounds);
    void setActionInfo(int actionIndex, int totalActions, const QString& currentDesc, const QString& nextDesc, int targetX, int targetY);
    void setCountdown(qint64 remainingMs, const QString& phase);

signals:
    void stopClicked();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updateOverlayPosition(int targetX, int targetY);

    QLabel* m_chainLabel;
    QLabel* m_roundLabel;
    QLabel* m_actionLabel;
    QLabel* m_nextActionLabel;
    QLabel* m_countdownLabel;
    QProgressBar* m_progressBar;
    QPushButton* m_stopButton;

    int m_targetX{0};
    int m_targetY{0};
    int m_totalCountdownMs{1000};
    qint64 m_lastRemainingMs{0};
};
