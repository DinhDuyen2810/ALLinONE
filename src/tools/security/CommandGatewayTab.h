#pragma once

#include <QWidget>

#include "engine/CommandAnalyzer.h"

class QCheckBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class CommandLauncher;

/// Tab "Cổng lệnh PowerShell": nơi DÁN lệnh PowerShell khả nghi (lấy từ đâu đó, hoặc tự viết) vào để
/// phân tích TĨNH trước (CommandAnalyzer - tìm dấu hiệu tải+thực thi, mã hóa, công cụ tấn công đã
/// biết...), rồi nếu muốn tiếp tục thì ứng dụng mở một cửa sổ PowerShell THẬT riêng biệt và DÁN lệnh vào
/// đó - KHÔNG BAO GIỜ tự chạy lệnh. Người dùng luôn là người bấm Enter cuối cùng trên cửa sổ PowerShell
/// thật, giữ quyền quyết định thực thi hoàn toàn ở người dùng.
class CommandGatewayTab : public QWidget
{
    Q_OBJECT

public:
    explicit CommandGatewayTab(QWidget* parent = nullptr);

private slots:
    void onAnalyzeClicked();
    void onLaunchClicked();
    void onLaunchFinished(bool ok, QString error);
    void onTextChanged();
    void onAcknowledgeToggled(bool checked);

private:
    void buildUi();
    void updateLaunchButtonState();

    QPlainTextEdit* m_commandEdit{nullptr};
    QPushButton* m_analyzeBtn{nullptr};
    QLabel* m_verdictBanner{nullptr};
    QLabel* m_reasonsLabel{nullptr};
    QCheckBox* m_acknowledgeCheck{nullptr};
    QPushButton* m_launchBtn{nullptr};
    QLabel* m_statusLabel{nullptr};

    CommandAnalyzer::Verdict m_lastVerdict;
    bool m_analyzed{false};
    CommandLauncher* m_launcher{nullptr};
};
