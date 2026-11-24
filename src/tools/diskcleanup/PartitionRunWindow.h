#pragma once

#include <QElapsedTimer>
#include <QWidget>

#include <atomic>
#include <functional>

#include "engine/PartitionRelocator.h"
#include "engine/VolumeHolders.h"

class QFrame;
class QLabel;
class QTableWidget;
class QThread;
class QProgressBar;
class QPushButton;
class QTimer;

namespace PartitionMover
{
class MoveWorker;
class PlanWorker;
} // namespace PartitionMover

/// Cửa sổ nhỏ của TIẾN TRÌNH CHẠY KẾ HOẠCH DỜI PHÂN VÙNG (bản sao tạm của ứng dụng, chạy với quyền Administrator - xem PartitionRelocator.h):
/// hiện kế hoạch, chờ ứng dụng cũ (đang giữ ổ) đóng hẳn, rồi chạy khi người dùng bấm "Bắt đầu" (KHÔNG tự chạy chỉ vì có ai gọi exe với tham số).
/// Tiến độ + tốc độ + Tạm dừng; xong thì "Mở lại One for ALL" (khởi chạy bản gốc bằng quyền thường). Đóng cửa sổ bị chặn trong lúc đang chạy.
class PartitionRunWindow : public QWidget
{
    Q_OBJECT

public:
    PartitionRunWindow(const PartitionRelocator::Plan& plan, quint32 waitPid, const QString& cleanupDir, QWidget* parent = nullptr);
    ~PartitionRunWindow() override;

    bool isRunning() const;
    bool startEnabled() const;
    /// CHỈ cho test UI: không hiện hộp thoại (nội dung vẫn nằm trong nhãn tiến độ).
    void setQuietForTest(bool quiet) { m_quiet = quiet; }
    QString startButtonText() const;
    /// Bấm "Bắt đầu" (dùng cho test/đường tự động hóa hiếm gặp) - chỉ có tác dụng khi nút đang bật.
    void start();

    /// CHỈ cho test UI: hiện bảng "đang giữ ổ" với kết quả dựng sẵn (không quét hệ thống thật).
    void setHoldersForTest(const VolumeHolders::Report& report);
    /// Đang quét/dừng chương trình giữ ổ (luồng nền).
    bool holdersBusy() const { return m_holdersBusy; }

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onPoll();
    void onProgress(int stepIndex, int stepCount, qint64 doneBytes, qint64 totalBytes, QString phase);
    void onFinished(bool ok, bool resumable, QString message, int failedStep);
    void onPause();
    void onReopen();
    /// Quét (luồng nền) các chương trình/dịch vụ đang giữ ổ của kế hoạch - xem VolumeHolders.
    void scanHolders();
    void onStopHolder(int row);
    void onStopAllHolders();

private:
    void scheduleCleanup();
    void showHolders(const VolumeHolders::Report& report);
    void setHoldersBusy(bool busy, const QString& text);
    void runInBackground(std::function<void()> work, std::function<void()> done);
    bool confirm(const QString& title, const QString& text);

    PartitionRelocator::Plan m_plan;
    quint32 m_waitPid{0};
    QString m_cleanupDir;
    QLabel* m_planLabel{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_progressLabel{nullptr};
    QProgressBar* m_bar{nullptr};
    QPushButton* m_startBtn{nullptr};
    QPushButton* m_pauseBtn{nullptr};
    QPushButton* m_reopenBtn{nullptr};
    QPushButton* m_closeBtn{nullptr};
    QTimer* m_poll{nullptr};
    PartitionMover::PlanWorker* m_planWorker{nullptr};
    PartitionMover::MoveWorker* m_resumeWorker{nullptr};
    // Bảng "chương trình/dịch vụ đang giữ ổ" (v1.33.0): tên + dịch vụ, PID, đang giữ gì, nút Dừng từng dòng + Dừng tất cả.
    QFrame* m_holdersPanel{nullptr};
    QLabel* m_holdersLabel{nullptr};
    QTableWidget* m_holdersTable{nullptr};
    QPushButton* m_holdersBtn{nullptr};
    QPushButton* m_rescanBtn{nullptr};
    QPushButton* m_stopAllBtn{nullptr};
    VolumeHolders::Report m_holders;
    QThread* m_holdersThread{nullptr};
    std::atomic_bool m_holdersStop{false};
    bool m_holdersBusy{false};
    QElapsedTimer m_waited;
    QElapsedTimer m_copyTimer;
    qint64 m_copyBase{0};
    bool m_running{false};
    bool m_parentGone{false};
    bool m_quiet{false};
};
