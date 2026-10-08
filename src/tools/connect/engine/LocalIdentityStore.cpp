#include "LocalIdentityStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

LocalIdentityStore& LocalIdentityStore::instance()
{
    static LocalIdentityStore s;
    return s;
}

LocalIdentityStore::LocalIdentityStore()
{
    loadOrCreate();
}

void LocalIdentityStore::setFilePath(const QString& path)
{
    m_path = path;
    loadOrCreate();
}

void LocalIdentityStore::loadOrCreate()
{
    QFile f(m_path);
    if (f.open(QIODevice::ReadOnly))
    {
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject())
        {
            const QJsonObject o = doc.object();
            m_identity.id = o.value("id").toString();
            m_identity.machineName = o.value("machineName").toString();
            if (!m_identity.id.isEmpty() && !m_identity.machineName.isEmpty())
                return;
        }
    }

    m_identity.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_identity.machineName = QHostInfo::localHostName();
    if (m_identity.machineName.isEmpty())
        m_identity.machineName = "May-khong-ten";

    const QFileInfo info(m_path);
    QDir().mkpath(info.absolutePath());
    QJsonObject o;
    o["id"] = m_identity.id;
    o["machineName"] = m_identity.machineName;

    // QSaveFile: ghi ra tệp tạm rồi ĐỔI TÊN ĐÈ nguyên tử lên đích khi commit() thành công - danh tính
    // (m_identity.id) được dùng trong bắt tay ghép đôi với mọi peer đã lưu; mất tệp này do crash giữa
    // chừng đồng nghĩa phải ghép đôi lại TỪ ĐẦU với tất cả (khác QFile::open(Truncate) cũ).
    QSaveFile out(m_path);
    if (out.open(QIODevice::WriteOnly))
    {
        out.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        out.commit();
    }
}
