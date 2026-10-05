#pragma once

#include <QWidget>

#include "engine/WlanController.h"

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

/// Tab "Hồ sơ đã lưu": xem/ẩn mật khẩu đã lưu trên máy, kết nối lại, xuất/nhập XML, xóa.
class ProfilesTab : public QWidget
{
    Q_OBJECT

public:
    explicit ProfilesTab(WlanController* controller, QWidget* parent = nullptr);

    void setAdapter(const QString& guid);
    void reload();

private slots:
    void onShowPasswordsToggled(bool on);
    void onConnectClicked();
    void onExportClicked();
    void onImportClicked();
    void onDeleteClicked();
    void onCopyPasswordClicked();
    void onSelectionChanged();

private:
    void buildUi();
    int selectedRow() const;

    WlanController* m_controller;
    QString m_adapterGuid;

    QCheckBox* m_showPasswordsCheck{nullptr};
    QTableWidget* m_table{nullptr};
    QPushButton* m_connectBtn{nullptr};
    QPushButton* m_copyPasswordBtn{nullptr};
    QPushButton* m_exportBtn{nullptr};
    QPushButton* m_importBtn{nullptr};
    QPushButton* m_deleteBtn{nullptr};
    QLabel* m_statusLabel{nullptr};

    QList<WifiProfile> m_profiles;
    bool m_passwordsVisible{false};
};
