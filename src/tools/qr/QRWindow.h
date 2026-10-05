#pragma once

#include <QWidget>

class QRGenerateTab;
class QRHistoryTab;
class QRScanTab;
class QTabWidget;

/// Cửa sổ QR Tools: Tạo mã - Quét mã - Lịch sử.
class QRWindow : public QWidget
{
    Q_OBJECT

public:
    explicit QRWindow(QWidget* parent = nullptr);

    QRGenerateTab* generateTab() const { return m_generate; }
    QRScanTab* scanTab() const { return m_scan; }
    QRHistoryTab* historyTab() const { return m_history; }
    QTabWidget* tabs() const { return m_tabs; }

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void recreateFromText(const QString& text);

    QTabWidget* m_tabs{nullptr};
    QRGenerateTab* m_generate{nullptr};
    QRScanTab* m_scan{nullptr};
    QRHistoryTab* m_history{nullptr};
};
