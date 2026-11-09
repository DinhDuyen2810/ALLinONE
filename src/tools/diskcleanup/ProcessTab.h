#pragma once

#include "engine/ProcessMonitor.h"

#include <QWidget>

#include <atomic>
#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QSortFilterProxyModel;
class QTableView;
class MeterCard;
class ModulesLoader;
class ProcessTableModel;
class UsageHistoryWidget;

/// Tab "Tiến trình & RAM": mọi tiến trình đang chạy (CPU, RAM, cấp phát, luồng, handle, I/O, người dùng, đường dẫn), RAM/CPU
/// toàn máy kèm đồ thị lịch sử và thông tin thanh RAM. Chỉ XEM. Luồng lấy mẫu chỉ chạy khi tab đang hiện (ngủ khi ẩn).
class ProcessTab : public QWidget
{
    Q_OBJECT

public:
    explicit ProcessTab(QWidget* parent = nullptr);
    ~ProcessTab() override;

    /// Dừng HẲN luồng lấy mẫu + việc đọc thanh RAM (khi thoát ứng dụng). Cửa sổ chỉ bị ẩn thì không cần gọi - hideEvent đã tắt.
    void stopMonitoring();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onSampled(std::shared_ptr<SystemSnapshot> snapshot);
    void onContextMenu(const QPoint& pos);
    void onModulesLoaded(QString summary);

private:
    void buildUi();
    void updateSamplingState();
    void applyFilter();
    quint32 selectedPid() const;
    QString selectedPath() const;
    void openSelectedInExplorer();

    ProcessSamplerThread* m_sampler{nullptr};
    ProcessTableModel* m_model{nullptr};
    QSortFilterProxyModel* m_proxy{nullptr};
    QTableView* m_table{nullptr};
    QLineEdit* m_filterEdit{nullptr};
    QCheckBox* m_groupCheck{nullptr};
    QCheckBox* m_pauseCheck{nullptr};
    QComboBox* m_intervalCombo{nullptr};
    QLabel* m_countLabel{nullptr};
    QLabel* m_ramModulesLabel{nullptr};
    QLabel* m_noteLabel{nullptr};
    MeterCard* m_cpuCard{nullptr};
    MeterCard* m_ramCard{nullptr};
    MeterCard* m_commitCard{nullptr};
    MeterCard* m_systemCard{nullptr};
    UsageHistoryWidget* m_history{nullptr};

    ModulesLoader* m_modulesLoader{nullptr};
    std::atomic_bool m_cancelModules{false};
    bool m_modulesRequested{false};
    std::shared_ptr<SystemSnapshot> m_last;
    bool m_stopped{false};
};
