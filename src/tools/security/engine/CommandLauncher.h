#pragma once

#include <QObject>
#include <QString>

class QTimer;

/// Mở một cửa sổ PowerShell THẬT, riêng biệt (powershell.exe mới, không phải tiến trình con bị ẩn) và
/// DÁN (không tự chạy) lệnh đã được CommandAnalyzer xem xét vào đó, để người dùng tự xem lại lần cuối
/// và tự bấm Enter. Ứng dụng KHÔNG BAO GIỜ tự thực thi lệnh người dùng nhập - đây chỉ là bước "dán hộ"
/// để đỡ phải gõ lại, quyết định chạy hay không luôn thuộc về người dùng trên cửa sổ PowerShell thật.
class CommandLauncher : public QObject
{
    Q_OBJECT

public:
    explicit CommandLauncher(QObject* parent = nullptr);
    ~CommandLauncher() override;

    /// Đặt lên Clipboard, mở powershell.exe mới, rồi gửi Ctrl+V vào đúng cửa sổ đó khi nó xuất hiện.
    /// Phát tín hiệu finished(ok, error) khi xong (hoặc hết thời gian chờ cửa sổ xuất hiện).
    void launchAndPaste(const QString& command);

signals:
    void finished(bool ok, QString error);

private:
    void pollForWindow();

    QTimer* m_pollTimer{nullptr};
    qint64 m_targetPid{0};
    int m_attemptsLeft{0};
};
