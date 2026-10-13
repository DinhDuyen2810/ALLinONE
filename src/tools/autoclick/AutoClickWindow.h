#pragma once

#include <QList>
#include <QWidget>
#include <QSplitter>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <map>
#include <memory>
#include <vector>

#include "model/ActionChain.h"
#include "widgets/ChainListWidget.h"
#include "widgets/ActionListWidget.h"
#include "widgets/ActionEditorWidget.h"
#include "engine/ActionRunner.h"
#include "capture/MouseCapture.h"
#include "capture/ActionRecorder.h"
#include "overlay/RuntimeOverlay.h"

class AutoClickWindow : public QWidget
{
    Q_OBJECT

public:
    explicit AutoClickWindow(QWidget* parent = nullptr);
    ~AutoClickWindow() override;

    /// Dừng NGAY chuỗi đang chạy (nếu có), không hỏi xác nhận, chờ luồng chạy kết thúc trong thời gian
    /// giới hạn - dùng cho AutoClickTool::stopBackgroundWorkForQuit() khi ứng dụng thoát theo đường
    /// KHÔNG đi qua closeEvent() của cửa sổ này (xem core/Tool.h).
    void stopForQuit();

private slots:
    // Chain slots
    void onChainSelected(int index);
    void onAddChain();
    void onCloneChain(int index);
    void onDeleteChain(int index);
    /// Nhiều chuỗi cùng lúc (Ctrl/Shift + bấm rồi chuột phải). Bản sao thêm vào CUỐI danh sách theo đúng thứ tự
    /// (như nhân bản một chuỗi). Không được xóa hết chuỗi - phải giữ lại ít nhất một.
    void onCloneChains(const QList<int>& indices);
    void onDeleteChains(const QList<int>& indices);
    void onRenameChain(int index, const QString& newName);
    void onTogglePinChain(int index);
    void onChainMoved(int from, int to);

    // Action slots
    void onActionSelected(int index);
    void onAddAction();
    void onCloneAction(int index);
    void onDeleteAction(int index);
    /// Nhiều hành động cùng lúc (Ctrl/Shift + bấm rồi chuột phải). Nhân bản MỘT hành động đặt bản sao NGAY SAU bản
    /// gốc; nhân bản NHIỀU hành động thì cả cụm bản sao (giữ thứ tự) được đẩy lùi về SAU hành động được chọn cuối
    /// cùng, không chen vào giữa các hành động gốc.
    void onCloneActions(const QList<int>& indices);
    void onDeleteActions(const QList<int>& indices);
    void onActionMoved(int from, int to);
    void onActionSaved(const Action& action, int actionIndex);
    void onEditorDirtyChanged(bool dirty);

    // Runner slots
    void onRunClicked();
    void onStopClicked();
    void onRunnerRoundStarted(int currentRound, int totalRounds);
    void onRunnerActionStarted(int currentRound, int totalRounds, int actionIndex, int totalActions,
                               const QString& currentDesc, const QString& nextDesc, int targetX, int targetY,
                               int sourceIndex);
    void onRunnerCountdownTick(qint64 remainingMs, const QString& phase);
    void onRunnerError(const QString& message);
    void onRunnerFinished();

    // Storage slots
    void onSaveProfile();
    void onExportProfile();
    void onImportProfile();

    // Capture slot
    void onCaptureRequested(int targetField);

    // Ghi nhớ chuỗi (RecordedMacro) slots - xem capture/ActionRecorder.h.
    void startMacroRecording();
    void onRecordingProgress(int stepCount, qint64 elapsedMs);
    void onRecordingFinished(std::vector<RecordedEvent> events);

protected:
    void closeEvent(QCloseEvent* event) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    void setupUi();
    void loadDefaultProfile();
    void syncUiWithCurrentChain();
    void updateRepeatFromUi();
    void startChain(int index);
    ActionChain* currentChain();
    void registerStopHotkey();
    void unregisterStopHotkey();
    /// Gọi khi THÊM/XÓA/ĐỔI CHỖ hành động của chain đang xem - nếu đó chính là chain đang chạy thì chỉ
    /// số hàng không còn khớp với bản sao ActionRunner đang giữ, phải thôi tô hàng "đang chạy".
    void noteActionsRestructured();
    void clearRunningMarker();

    // --- Cache chỉnh sửa chưa lưu (m_pendingEdits) - xem comment field bên dưới. ---
    /// Nếu ActionEditorWidget đang dirty (sửa dở, chưa bấm Apply), chụp lại getAction() vào
    /// m_pendingEdits[hàng đang nạp] - gọi TRƯỚC khi nạp dữ liệu khác vào editor (đổi hàng/chain/
    /// đóng cửa sổ) để không bao giờ ghi đè mất.
    void captureEditorIfDirty();
    bool hasPendingEdits() const { return !m_pendingEdits.empty(); }
    /// Ghi toàn bộ m_pendingEdits vào chain đang xem (y hệt bấm "Apply" từng cái) rồi xóa cache + dấu "*".
    void applyPendingEdits();
    /// Bỏ toàn bộ m_pendingEdits (không ghi gì), nạp lại editor từ dữ liệu THẬT của hàng đang chọn.
    void discardPendingEdits();
    /// Đồng bộ lại tập hàng "*" trên ActionListWidget theo đúng key hiện có trong m_pendingEdits.
    void refreshDirtyMarkersFromPendingEdits();
    /// Gọi ở các điểm "rời khỏi" quan trọng (RUN, đóng cửa sổ): captureEditorIfDirty() rồi nếu còn cache,
    /// hỏi Có (áp dụng)/Không (bỏ)/Hủy. Trả về false nếu người dùng chọn Hủy - nơi gọi phải dừng thao tác
    /// đang định làm (không chạy/không đóng).
    bool confirmPendingEditsBeforeLeaving(const QString& actionLabel);
    /// Đổi tên tệp hồ sơ mặc định bị hỏng thành "<tên>.<thời điểm>.bak" để KHÔNG BAO GIỜ ghi đè lên nó.
    bool backupCorruptDefaultProfile(QString* backupPath = nullptr);

    std::vector<ActionChain> m_chains;
    int m_currentChainIndex{0};
    bool m_syncingUi{false};

    // Trạng thái của lần chạy hiện tại
    bool m_runActive{false};            // true từ startChain() tới khi onRunnerFinished() ĐÃ xử lý xong lần chạy đó
    int m_runningChainIndex{-1};        // chỉ số (trong m_chains) của chain đang chạy, -1 = không chạy/không còn xác định
    bool m_runningChainRestructured{false};
    bool m_stopHotkeyRegistered{false};
    bool m_recordActive{false};         // true từ startMacroRecording() tới khi onRecordingFinished() xử lý xong
    QString m_lastRunError;             // lỗi ActionRunner báo về, hiện cho người dùng khi lần chạy kết thúc
    bool m_corruptDefaultProfilePending{false}; // tệp mặc định hỏng mà CHƯA đổi tên được - cấm ghi đè

    /// Cache các hành động ĐANG SỬA DỞ (chưa bấm Apply) của chain đang xem (m_currentChainIndex), theo
    /// chỉ số hành động -> bản ghi đang gõ dở trong editor - để đổi hàng xem KHÔNG làm mất chỉnh sửa
    /// (yêu cầu người dùng: chỉ hỏi "có lưu không" lúc RUN hoặc đóng cửa sổ, không hỏi lúc đổi hàng).
    /// LUÔN rỗng khi đổi sang chain khác (đổi chain tự áp dụng/làm sạch cache trước, xem onChainSelected()
    /// và các chỗ khác đổi m_currentChainIndex) - không cần một map lồng theo từng chain.
    std::map<int, Action> m_pendingEdits;
    /// true trong lúc một hàm RESTRUCTURE/LƯU (syncUiWithCurrentChain()/onActionMoved()/onCloneAction()/
    /// onDeleteAction()/onAddAction()/onActionSaved()/applyPendingEdits()) đang TỰ vẽ lại bảng
    /// (setActions()) và TỰ gọi setSelectedActionIndex() sau khi đã tự lo capture/remap m_pendingEdits của
    /// riêng nó - chặn captureEditorIfDirty() khỏi bị gọi THÊM LẦN NỮA một cách sai bởi chính
    /// onActionSelected() lan truyền qua currentCellChanged. CẢ setActions() (setRowCount(0) rồi
    /// setCurrentCell) LẪN setSelectedActionIndex() đều phát tín hiệu đó, nên cờ phải bao TRỌN đoạn vẽ lại
    /// bảng chứ không chỉ riêng setSelectedActionIndex(): nếu hở, (a) sau khi Lưu hành động editor còn
    /// "dirty" nên bản vừa lưu bị chụp lại thành cache "chưa lưu" ma - dấu "*" hiện lại, phải bấm Lưu LẦN
    /// THỨ HAI mới hết; (b) sau Nhân bản/Xóa/Dời, nội dung editor bị ghi nhầm vào chỉ số vừa remap xong.
    /// Đặt qua ScopedFlag (AutoClickWindow.cpp) để luôn được trả về giá trị cũ, kể cả khi return sớm.
    bool m_suppressPendingCapture{false};

    // UI Widgets
    QSplitter* m_splitter;
    ChainListWidget* m_chainListWidget;
    ActionListWidget* m_actionListWidget;
    ActionEditorWidget* m_actionEditorWidget;

    // Toolbar controls
    QPushButton* m_runButton;
    QSpinBox* m_repeatSpin;
    QCheckBox* m_infiniteCheck;
    QPushButton* m_saveButton;
    QPushButton* m_exportButton;
    QPushButton* m_importButton;

    // Components
    ActionRunner* m_runner;
    RuntimeOverlay* m_overlay;
    MouseCapture* m_mouseCapture;
    ActionRecorder* m_actionRecorder;
};
