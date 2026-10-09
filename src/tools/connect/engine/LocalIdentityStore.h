#pragma once

#include <QString>

/// Định danh ổn định của máy này, sinh một lần và lưu lại giữa các lần chạy
/// (<dữ liệu người dùng>/profiles/connect_identity.json - xem core/AppPaths.h).
struct LocalIdentity
{
    QString id;
    QString machineName;
};

class LocalIdentityStore
{
public:
    static LocalIdentityStore& instance();

    const LocalIdentity& identity() const { return m_identity; }
    void setFilePath(const QString& path); // dùng cho kiểm thử; gọi trước khi truy cập identity()

private:
    LocalIdentityStore();
    void loadOrCreate();

    QString m_path;
    LocalIdentity m_identity;
};
