#pragma once

#include "engine/FolderTreeScanner.h"

#include <QWidget>

#include <memory>
#include <vector>

class QComboBox;
class QShortcut;
class QThread;
class QLabel;
class QProgressBar;
class QPushButton;
class QTreeView;
class FolderTreeModel;

/// Tab "Cây thư mục": quét TOÀN BỘ một ổ đĩa/thư mục rồi hiện cây thư mục sắp theo dung lượng giảm dần (kèm % của
/// thư mục cha) để thấy ngay thư mục nào chiếm nhiều chỗ. Mở dòng gộp "[N tệp nhỏ]"/"[N thư mục nhỏ]" để xem từng mục
/// (đọc trực tiếp từ đĩa). Chọn một hay nhiều mục rồi "Xóa vào Thùng rác" (khôi phục được) hoặc "Xóa vĩnh viễn".
///
/// An toàn khi xóa: mọi đường dẫn đi qua TreeDeleteGuard (không bao giờ xóa gốc ổ/thư mục Windows/gốc đang xem/thư mục
/// hồ sơ...), qua cùng lớp RecycleBinOps với các tab khác (kiểm tra Thùng rác chứa được không, tệp đang bị khóa...), luôn
/// có hộp xác nhận mặc định "Không", và chạy trên luồng nền. Xóa xong cây được sửa tại chỗ (không phải quét lại).
class FolderTreeTab : public QWidget
{
    Q_OBJECT

public:
    explicit FolderTreeTab(QWidget* parent = nullptr);
    ~FolderTreeTab() override;

    bool isScanningNow() const;
    /// Yêu cầu dừng lượt quét đang chạy (an toàn: quét chỉ đọc).
    void stopScanIfRunning();

    /// Đang xóa mục đã chọn trên luồng nền - KHÔNG an toàn để đóng cửa sổ giữa chừng (xóa dở một thư mục).
    bool isDeletingNow() const { return m_deleting; }

    /// Xóa các mục đang chọn (qua hộp xác nhận). permanent = xóa vĩnh viễn, không qua Thùng rác. Chạy nền; xong thì cây tự cập nhật.
    void deleteSelected(bool permanent);

    /// Đặt thư mục sẽ quét (thay cho hộp thoại "Chọn thư mục khác...") - dùng cho test.
    void setScanRoot(const QString& path);

    // ---- Chỉ dành cho bộ test UI ----
    /// Bật: mọi hộp xác nhận/thông báo tự động coi như "Đồng ý" và nội dung được ghi lại (lastMessageForTest) thay vì hiện hộp thoại.
    void setAutoConfirmForTest(bool on) { m_autoConfirm = on; }
    QString lastMessageForTest() const { return m_lastMessage; }

private slots:
    void onScanClicked();
    void onStopClicked();
    void onBrowseClicked();
    void onProgressTick(qint64 files, qint64 folders, qint64 bytes, QString currentFolder);
    void onScanFinished(std::shared_ptr<FolderTree> tree);
    void onScanStopped();
    void onSelectionChanged();
    void onContextMenu(const QPoint& pos);
    void onExpandHeaviestClicked();
    void onOpenInExplorerClicked();
    void onCopyPathClicked();

private:
    struct PendingDelete;
    struct ExpandedState;
    void buildUi();
    void notify(const QString& title, const QString& text, bool critical = false);
    bool confirm(const QString& title, const QString& text, bool dangerous);
    void finishDelete(const std::vector<PendingDelete>& items, bool permanent, bool ok, const QString& error);
    ExpandedState captureExpanded() const;
    void restoreExpanded(const ExpandedState& state);
    void updateDeleteButtons();
    QString selectedPath() const;
    void updateDriveInfo();
    void setScanningUi(bool scanning);
    QString buildSummary(const FolderTree& tree) const;

    QComboBox* m_driveCombo{nullptr};
    QPushButton* m_browseBtn{nullptr};
    QLabel* m_driveLabel{nullptr};
    QProgressBar* m_driveBar{nullptr};
    QLabel* m_pathLabel{nullptr};
    QTreeView* m_view{nullptr};
    FolderTreeModel* m_model{nullptr};
    QProgressBar* m_busyBar{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_summaryLabel{nullptr};
    QPushButton* m_scanBtn{nullptr};
    QPushButton* m_stopBtn{nullptr};
    QPushButton* m_expandBtn{nullptr};
    QPushButton* m_collapseBtn{nullptr};
    QPushButton* m_heaviestBtn{nullptr};
    QPushButton* m_explorerBtn{nullptr};
    QPushButton* m_copyBtn{nullptr};
    QPushButton* m_recycleBtn{nullptr};
    QPushButton* m_deleteBtn{nullptr};
    QLabel* m_hintLabel{nullptr};
    QThread* m_deleteThread{nullptr};
    bool m_deleting{false};
    bool m_autoConfirm{false};
    QString m_lastMessage;

    FolderTreeScanner* m_scanner{nullptr};
    QString m_customRoot;
    QString m_scannedRoot;
};
