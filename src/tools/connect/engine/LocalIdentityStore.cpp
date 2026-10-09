#include "LocalIdentityStore.h"

#include "ProtocolMessage.h"
#include "core/AppPaths.h"

#include <QDebug>
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
    : m_path(AppPaths::profileFile("connect_identity.json"))
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
        // Đóng NGAY sau khi đọc: nếu tệp hỏng, bên dưới sẽ ghi lại bằng QSaveFile (đổi tên ĐÈ lên đích) - trên
        // Windows việc đổi tên đè thất bại khi tệp đích còn đang mở. Để mở thì danh tính vừa sinh lại không
        // bao giờ lưu được: mỗi lần chạy ứng dụng ra một id mới, mọi máy đã ghép đôi coi máy này là máy lạ.
        f.close();
        if (doc.isObject())
        {
            const QJsonObject o = doc.object();
            m_identity.id = o.value("id").toString();
            m_identity.machineName = ConnectProtocol::sanitizeMachineName(o.value("machineName").toString());
            // id đi vào preamble + gói quảng bá của giao thức, nơi máy kia kiểm đúng định dạng này - tệp bị
            // sửa tay thành id sai định dạng thì sinh lại (đồng nghĩa phải ghép đôi lại) thay vì chạy với
            // một id mà không máy nào chấp nhận.
            if (ConnectProtocol::isValidPeerId(m_identity.id))
                return;
        }
    }

    m_identity.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_identity.machineName = QHostInfo::localHostName();
    if (m_identity.machineName.isEmpty())
        m_identity.machineName = "May-khong-ten";
    m_identity.machineName = ConnectProtocol::sanitizeMachineName(m_identity.machineName);

    const QFileInfo info(m_path);
    QDir().mkpath(info.absolutePath());
    QJsonObject o;
    o["id"] = m_identity.id;
    o["machineName"] = m_identity.machineName;

    // QSaveFile: ghi ra tệp tạm rồi ĐỔI TÊN ĐÈ nguyên tử lên đích khi commit() thành công - danh tính
    // (m_identity.id) được dùng trong bắt tay ghép đôi với mọi peer đã lưu; mất tệp này do crash giữa
    // chừng đồng nghĩa phải ghép đôi lại TỪ ĐẦU với tất cả (khác QFile::open(Truncate) cũ).
    QSaveFile out(m_path);
    bool ok = out.open(QIODevice::WriteOnly);
    if (ok)
    {
        out.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        ok = out.commit();
    }
    // connect_core (nơi lớp này thuộc về) không kéo theo Logger để giữ gọn như các *_core thuần khác
    // (xem CMakeLists.txt) - dùng qWarning() nhẹ sẵn có của Qt thay vì im lặng bỏ qua lỗi ghi (khác
    // PeerStore::save()/QRHistoryStore::save() có thể trả bool cho nơi gọi tự quyết định, hàm này được
    // gọi từ constructor nên không có nơi nào để propagate thất bại lên).
    if (!ok)
        qWarning("LocalIdentityStore: không ghi được danh tính máy vào %s", qUtf8Printable(m_path));
}
