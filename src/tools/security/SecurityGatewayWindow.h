#pragma once

#include <QWidget>

class QTabWidget;
class CommandGatewayTab;
class WebProtectionTab;
class MalwareScanTab;

/// Cửa sổ Security Gateway: cổng lệnh PowerShell (phân tích trước khi chạy), bảo vệ web (Network
/// Protection + danh sách chặn tùy chỉnh), và quét/xử lý mã độc - tham khảo các tính năng tương đương
/// của Kaspersky nhưng xây trên nền Windows Defender có sẵn (xem từng engine để biết lý do).
class SecurityGatewayWindow : public QWidget
{
    Q_OBJECT

public:
    explicit SecurityGatewayWindow(QWidget* parent = nullptr);
    ~SecurityGatewayWindow() override;

private:
    void buildUi();

    QTabWidget* m_tabs{nullptr};
    CommandGatewayTab* m_commandTab{nullptr};
    WebProtectionTab* m_webTab{nullptr};
    MalwareScanTab* m_scanTab{nullptr};
};
