#pragma once

#include <QWidget>

class QLabel;
class QProgressBar;
class QPushButton;
class SpeedTestRunner;
class WlanController;

/// Tab "Đo tốc độ mạng": ping, jitter, tải xuống, tải lên qua Internet thật.
class SpeedTestTab : public QWidget
{
    Q_OBJECT

public:
    explicit SpeedTestTab(WlanController* controller, QWidget* parent = nullptr);

    void setAdapter(const QString& guid);

private slots:
    void onStartStopClicked();

private:
    void buildUi();
    QWidget* makeMetricCard(const QString& title, QLabel** valueLabelOut, const QString& unit);
    void resetMetrics();
    void updateLinkRateLabel();

    WlanController* m_controller;
    QString m_adapterGuid;
    SpeedTestRunner* m_runner{nullptr};

    QPushButton* m_startStopBtn{nullptr};
    QLabel* m_phaseLabel{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QLabel* m_linkRateLabel{nullptr};

    QLabel* m_pingValue{nullptr};
    QLabel* m_jitterValue{nullptr};
    QLabel* m_downloadValue{nullptr};
    QLabel* m_uploadValue{nullptr};
};
