#include "UpdateInstaller.h"

#include "tools/downloader/engine/FileDownloader.h"
#include "tools/downloader/model/DownloadItem.h"
#include "core/Logger.h"
#include "core/ToolManager.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

UpdateInstaller::UpdateInstaller(QObject* parent)
    : QObject(parent)
{
    m_downloader = new FileDownloader(this);

    connect(m_downloader, &FileDownloader::itemUpdated, this, [this](int id) {
        const DownloadItem* it = m_downloader->item(id);
        if (it)
            emit progress(it->receivedBytes, it->totalBytes > 0 ? it->totalBytes : 0);
    });

    connect(m_downloader, &FileDownloader::itemFinished, this, [this](int /*id*/, bool success) {
        if (!success)
        {
            Logger::instance().warning("Update", "Tải trình cài đặt bản mới thất bại.");
            emit downloadFailed("Tải trình cài đặt thất bại - kiểm tra lại kết nối mạng.");
            return;
        }

        Logger::instance().info("Update", "Đã tải xong trình cài đặt bản mới: " + m_installerPath);

        // Sắp chạy trình cài đặt rồi tự qApp->quit() (xem MainWindow.cpp, nối tín hiệu aboutToRestart
        // bên dưới) - việc này KHÔNG tự gọi closeEvent() của các cửa sổ tool KHÁC đang mở. Nếu một cửa sổ
        // khác (vd Disk Cleanup) đang đổi kích thước phân vùng thật, tự cập nhật ngay bây giờ sẽ
        // TerminateProcess giữa chừng một thao tác không an toàn để hủy - hoãn lại, KHÔNG chạy installer.
        QString busyReason;
        if (ToolManager::instance().anyToolWindowBusy(&busyReason))
        {
            Logger::instance().info("Update", "Hoãn cài đặt bản mới - đang bận: " + busyReason);
            emit downloadFailed(
                "Đang có một thao tác không an toàn để hủy giữa chừng (" + busyReason +
                ") - đã hoãn cài đặt bản mới. Vui lòng đợi thao tác đó xong rồi mở lại ứng dụng để tự "
                "cập nhật.");
            return;
        }

        // /VERYSILENT: không hiện wizard. /SUPPRESSMSGBOXES: không hỏi gì giữa chừng. /NORESTART: không
        // tự khởi động lại MÁY (chỉ ứng dụng). /CLOSEAPPLICATIONS + /RESTARTAPPLICATIONS: nhờ Windows
        // Restart Manager tự đóng OneForAll.exe đang chạy (nhận diện qua AppMutex trong .iss) rồi tự mở
        // lại sau khi cài xong - không cần [Run] postinstall (vốn bị Inno TỰ BỎ QUA trong chế độ im lặng
        // qua cờ "skipifsilent", xem OneForAll.iss).
        const QStringList args{"/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART",
                                "/CLOSEAPPLICATIONS", "/RESTARTAPPLICATIONS"};
        if (!QProcess::startDetached(m_installerPath, args))
        {
            Logger::instance().warning("Update", "Không khởi chạy được trình cài đặt: " + m_installerPath);
            emit downloadFailed("Không khởi chạy được trình cài đặt.");
            return;
        }

        emit aboutToRestart();
    });
}

UpdateInstaller::~UpdateInstaller() = default;

void UpdateInstaller::downloadAndInstall(const QString& downloadUrl)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/OneForAll_Update";
    QDir().mkpath(dir);
    m_installerPath = dir + "/OneForAll_Setup.exe";

    // Tải LẠI TỪ ĐẦU mỗi lần (xóa file cũ nếu còn sót từ lần kiểm tra trước) - tránh FileDownloader hiểu
    // nhầm thành "tiếp tục tải dở" một file installer của phiên bản KHÁC còn sót lại cùng tên.
    QFile::remove(m_installerPath);

    const int id = m_downloader->enqueue(downloadUrl, m_installerPath);
    m_downloader->start(id);
}
