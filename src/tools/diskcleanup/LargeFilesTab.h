#pragma once

#include <QWidget>

#include "engine/LargeFileScanner.h"

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTableWidget;

/// Tab "Tìm tệp lớn": người dùng chọn thư mục gốc (hoặc cả ổ), ngưỡng kích thước tối thiểu, quét và
/// liệt kê các tệp lớn nhất tìm được - không gắn với hạng mục dọn dẹp định sẵn, do người dùng tự quyết
/// định xóa hay không (mở thư mục chứa / xóa vào Thùng rác).
class LargeFilesTab : public QWidget
{
    Q_OBJECT

public:
    explicit LargeFilesTab(QWidget* parent = nullptr);
    ~LargeFilesTab() override;

private slots:
    void onBrowseClicked();
    void onScanClicked();
    void onStopClicked();
    void onProgressTick(qint64 filesScanned);
    void onScanFinished(QList<LargeFileEntry> results);
    void onScanStopped();
    void onDeleteSelectedClicked();
    void onOpenFolderClicked();
    void onTableSelectionChanged();

private:
    void buildUi();
    void populateTable(const QList<LargeFileEntry>& results);
    QString selectedPath() const;

    LargeFileScanner* m_scanner{nullptr};

    QComboBox* m_driveCombo{nullptr};
    QPushButton* m_browseBtn{nullptr};
    QLabel* m_pathLabel{nullptr};
    QSpinBox* m_minSizeSpin{nullptr}; // đơn vị MB
    QPushButton* m_scanBtn{nullptr};
    QPushButton* m_stopBtn{nullptr};
    QPushButton* m_openFolderBtn{nullptr};
    QPushButton* m_deleteBtn{nullptr};
    QProgressBar* m_progressBar{nullptr};
    QLabel* m_statusLabel{nullptr};
    QTableWidget* m_table{nullptr}; // [Đường dẫn][Kích thước][Sửa đổi lần cuối]

    QString m_customRoot; // rỗng = dùng ổ đã chọn trong m_driveCombo
    QList<LargeFileEntry> m_results;
};
