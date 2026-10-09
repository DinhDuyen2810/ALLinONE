#pragma once

#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

/// Tab lịch sử các mã đã tạo / đã quét.
class QRHistoryTab : public QWidget
{
    Q_OBJECT

public:
    explicit QRHistoryTab(QWidget* parent = nullptr);

    int rowCount() const;

signals:
    void recreateRequested(const QString& text);

private slots:
    void reload();
    void copySelected();
    void recreateSelected();
    void deleteSelected();
    void clearAll();

private:
    int selectedIndex() const;

    QTableWidget* m_table{nullptr};
    QPushButton* m_copyBtn{nullptr};
    QPushButton* m_recreateBtn{nullptr};
    QPushButton* m_deleteBtn{nullptr};
    QPushButton* m_clearBtn{nullptr};
    QCheckBox* m_revealCheck{nullptr}; // mặc định TẮT: mật khẩu WiFi trong bảng/tooltip hiện thành ••••••••
    QLabel* m_warning{nullptr};        // báo khi ghi tệp lịch sử thất bại
};
