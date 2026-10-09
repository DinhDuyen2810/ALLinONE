#pragma once

#include <QWidget>

#include "engine/DefenderController.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

/// Tab "Bảo vệ Web": bật/tắt Network Protection của Windows Defender (chặn kết nối tới trang/máy chủ
/// độc hại theo danh tiếng thời gian thực của Microsoft - CƠ CHẾ THẬT đứng sau "chặn link độc hại ngay
/// lúc truy vấn", hoạt động ở tầng hệ điều hành nên áp dụng cho MỌI trình duyệt/ứng dụng, không chỉ một
/// trình duyệt cụ thể), kèm danh sách chặn tùy chỉnh do người dùng tự khai báo qua hosts file (bổ sung
/// thủ công, không phụ thuộc Defender).
class WebProtectionTab : public QWidget
{
    Q_OBJECT

public:
    explicit WebProtectionTab(QWidget* parent = nullptr);

public slots:
    /// Khóa/mở nút "Chạy lại với quyền Quản trị" (nút đó gọi qApp->quit()) - SecurityGatewayWindow khóa
    /// nó trong lúc tab Quét mã độc đang quét, tránh thoát ứng dụng khi luồng quét còn chạy.
    void setRelaunchAllowed(bool allowed);

private slots:
    void onRefreshClicked();
    void onRelaunchElevatedClicked();
    void onToggleNetworkProtectionClicked();
    void onAddDomainClicked();
    void onRemoveDomainClicked();

private:
    void buildUi();
    void refreshStatus();
    void refreshBlocklist();
    void updateElevationBanner();

    QLabel* m_elevationBanner{nullptr};
    QPushButton* m_relaunchBtn{nullptr};

    QLabel* m_statusLabel{nullptr};
    QPushButton* m_refreshBtn{nullptr};
    QPushButton* m_toggleBtn{nullptr};
    QLabel* m_tamperWarning{nullptr};

    QLabel* m_blocklistErrorLabel{nullptr};
    QListWidget* m_domainList{nullptr};
    QLineEdit* m_domainEdit{nullptr};
    QPushButton* m_addDomainBtn{nullptr};
    QPushButton* m_removeDomainBtn{nullptr};

    DefenderController::DefenderStatus m_lastStatus;
};
