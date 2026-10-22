#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class ConnectivityTester;

/// Tab "Kiểm tra kết nối": thử mở một kết nối TCP tới một máy chủ (host + cổng) và báo có "chạm" được
/// không + độ trễ. Dùng để kiểm tra nhanh một máy chủ VPN (hoặc bất kỳ máy chủ nào) còn sống/đường mạng
/// thông trước khi kết nối VPN. Thuần Qt Network (xem ConnectivityTester.h) - không cần quyền Administrator.
class ConnectivityTab : public QWidget
{
    Q_OBJECT

public:
    explicit ConnectivityTab(QWidget* parent = nullptr);

    /// Điền sẵn địa chỉ (gọi từ tab "Đổi IP theo quốc gia" khi người dùng bấm "Kiểm tra kết nối máy chủ này").
    void setTarget(const QString& host, quint16 port = 443);

private slots:
    void onCheckClicked();
    void onTestFinished(bool reachable, int elapsedMs, QString detail);

private:
    void buildUi();
    void setBusy(bool busy);
    void addLogRow(const QString& host, int port, bool reachable, int ms, const QString& detail);

    QLineEdit* m_hostEdit{nullptr};
    QSpinBox* m_portSpin{nullptr};
    QPushButton* m_checkBtn{nullptr};
    QLabel* m_resultLabel{nullptr};
    QTableWidget* m_log{nullptr};

    ConnectivityTester* m_tester{nullptr};
    QString m_pendingHost;
    int m_pendingPort{0};
};
