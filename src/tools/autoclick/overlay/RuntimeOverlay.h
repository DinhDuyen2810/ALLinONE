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
    /// Hiện phím tắt dừng toàn cục đang có hiệu lực (vd "Ctrl+Alt+F8") cạnh nút Dừng; chuỗi rỗng = phím
    /// tắt không đăng ký được, chỉ còn nút bấm - xem engine/StopHotkey.h.
    void setStopHotkeyHint(const QString& hotkeyLabel);

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
    QLabel* m_hotkeyHintLabel;

    int m_targetX{0};
    int m_targetY{0};
    int m_totalCountdownMs{1000};
    qint64 m_lastRemainingMs{0};

    // Vị trí mặc định (góc trên-phải màn hình chính, tính một lần trong constructor) - trước đây sau khi
    // né sang góc đối diện để tránh che hành động, HUD đứng yên ở đó MÃI MÃI dù hành động kế tiếp không
    // còn gì va chạm nữa. m_isDisplaced theo dõi có đang ở vị trí né hay không, xem updateOverlayPosition().
    QPoint m_homePos;
    bool m_isDisplaced{false};
};
