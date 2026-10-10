#include "UpdateInstaller.h"

#include "tools/downloader/engine/FileDownloader.h"
#include "tools/downloader/model/DownloadItem.h"
#include "core/AppPaths.h"
#include "core/Logger.h"
#include "core/PowerShellRunner.h"
#include "core/ToolManager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace
{
// AppId trong installer/OneForAll.iss + hậu tố "_is1" mà Inno Setup luôn thêm cho khóa gỡ cài đặt.
const char* const kInnoUninstallSubKey =
    "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\{C82B22A6-50DC-4B99-A2A4-056B0A66BE55}_is1";

QString normalizedDir(const QString& path)
{
    if (path.trimmed().isEmpty())
        return QString();
    return QDir::cleanPath(QDir::fromNativeSeparators(path.trimmed())).toLower();
}
} // namespace

UpdateInstaller::UpdateInstaller(QObject* parent)
    : QObject(parent)
{
    m_downloader = new FileDownloader(this);

    connect(m_downloader, &FileDownloader::itemUpdated, this, [this](int id) {
        const DownloadItem* it = m_downloader->item(id);
        if (it)
            emit progress(it->receivedBytes, it->totalBytes > 0 ? it->totalBytes : 0);
    });

    connect(m_downloader, &FileDownloader::itemFinished, this,
            [this](int /*id*/, bool success) { onDownloadFinished(success); });
}

UpdateInstaller::~UpdateInstaller() = default;

UpdateInstaller::InstallKind UpdateInstaller::detectInstallKind()
{
    const QSettings msiMarker("HKEY_CURRENT_USER\\Software\\OneForAll", QSettings::NativeFormat);
    const bool msiMarkerPresent = msiMarker.value("installed").toInt() == 1;

    QString innoDir;
    bool innoKeyPresent = false;
    for (const char* root : {"HKEY_CURRENT_USER\\", "HKEY_LOCAL_MACHINE\\"})
    {
        const QSettings key(QString::fromLatin1(root) + QString::fromLatin1(kInnoUninstallSubKey), QSettings::NativeFormat);
        const QString location = key.value("InstallLocation").toString();
        if (!location.isEmpty())
        {
            innoKeyPresent = true;
            innoDir = location;
            break;
        }
    }

    const QString localAppData = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return UpdateInstallerInternal::classifyInstall(msiMarkerPresent, innoKeyPresent,
                                                    QCoreApplication::applicationDirPath(),
                                                    localAppData + "/One for ALL", innoDir);
}

QString UpdateInstaller::consumePreviousUpdateFailure()
{
    const QString markerPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) +
                               "/OneForAll_Update/update_failed.marker";
    QFile f(markerPath);
    if (!f.exists())
        return QString();
    QString reason;
    if (f.open(QIODevice::ReadOnly | QIODevice::Text))
        reason = QString::fromUtf8(f.readAll()).trimmed();
    f.remove(); // không báo lặp lại mãi ở các lần mở sau - chỉ báo đúng một lần cho lần thất bại này
    return reason.isEmpty() ? QStringLiteral("Không rõ lý do - xem %TEMP%\\OneForAll_Update\\update_helper.log")
                            : reason;
}

void UpdateInstaller::downloadAndInstall(const UpdateInfo& info)
{
    m_kind = detectInstallKind();

    QString url = info.downloadUrl;
    QString fileName = "OneForAll_Setup.exe";
    m_expectedSha256 = info.sha256;
    if (m_kind == InstallKind::Msi)
    {
        if (info.msiUrl.isEmpty())
        {
            // Bản phát hành cũ chỉ đính kèm .exe. KHÔNG rơi về chạy .exe: sẽ cài thêm một bản thứ hai
            // cạnh bản MSI đang chạy thay vì nâng cấp nó.
            emit downloadFailed("Bản phát hành này không kèm tệp OneForAll_Setup.msi nên không tự cập nhật "
                                "được cho bản đã cài bằng .msi. Vui lòng tải bản mới từ trang GitHub của dự án.");
            return;
        }
        url = info.msiUrl;
        fileName = "OneForAll_Setup.msi";
        m_expectedSha256 = info.msiSha256;
    }

    if (!UpdateCheckerInternal::isTrustedDownloadUrl(url))
    {
        Logger::instance().warning("Update", "Từ chối URL tải bản cập nhật không đáng tin: " + url);
        emit downloadFailed("Địa chỉ tải bản cập nhật không hợp lệ - đã hủy.");
        return;
    }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/OneForAll_Update";
    QDir().mkpath(dir);
    m_installerPath = dir + "/" + fileName;

    // Tải LẠI TỪ ĐẦU mỗi lần (xóa file cũ nếu còn sót từ lần kiểm tra trước) - tránh FileDownloader hiểu
    // nhầm thành "tiếp tục tải dở" một file installer của phiên bản KHÁC còn sót lại cùng tên.
    QFile::remove(m_installerPath);
    QFile::remove(m_installerPath + ".part");

    const int id = m_downloader->enqueue(url, m_installerPath);
    m_downloader->start(id);
}

void UpdateInstaller::onDownloadFinished(bool success)
{
    if (!success)
    {
        Logger::instance().warning("Update", "Tải trình cài đặt bản mới thất bại.");
        emit downloadFailed("Tải trình cài đặt thất bại - kiểm tra lại kết nối mạng.");
        return;
    }

    Logger::instance().info("Update", "Đã tải xong trình cài đặt bản mới: " + m_installerPath);

    // Tệp này sắp được CHẠY - đối chiếu SHA-256 với "digest" GitHub công bố cho đúng asset đó. Lệch tức
    // là tệp hỏng khi truyền hoặc không phải tệp của bản phát hành: xóa, không chạy.
    if (!m_expectedSha256.isEmpty())
    {
        const QString actual = UpdateInstallerInternal::sha256OfFile(m_installerPath);
        if (actual != m_expectedSha256)
        {
            Logger::instance().error("Update", "SHA-256 của trình cài đặt không khớp (mong đợi " + m_expectedSha256 +
                                                   ", thực tế " + (actual.isEmpty() ? QString("không đọc được") : actual) + ").");
            QFile::remove(m_installerPath);
            emit downloadFailed("Tệp cài đặt tải về không khớp mã kiểm tra (SHA-256) của bản phát hành - đã "
                                "xóa, không cài. Vui lòng thử lại sau.");
            return;
        }
    }
    else
    {
        Logger::instance().info("Update", "GitHub không trả digest cho asset - bỏ qua bước đối chiếu SHA-256.");
    }

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

    if (m_kind == InstallKind::Portable)
    {
        // Không phải bản đã cài: mở trình cài đặt ở chế độ tương tác bình thường để người dùng tự chọn
        // nơi cài - không âm thầm cài vào vị trí mặc định rồi mở lại bản portable cũ.
        if (!QProcess::startDetached(m_installerPath, {}))
        {
            Logger::instance().warning("Update", "Không khởi chạy được trình cài đặt: " + m_installerPath);
            emit downloadFailed("Không khởi chạy được trình cài đặt.");
            return;
        }
        emit aboutToRestart();
        return;
    }

    const QString script = UpdateInstallerInternal::buildHelperScript(
        m_kind, m_installerPath, QCoreApplication::applicationFilePath(), QCoreApplication::applicationPid());

    QProcess helper;
    helper.setProgram(AppPaths::powershellExecutable());
    helper.setArguments({"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-WindowStyle", "Hidden",
                         "-EncodedCommand", PowerShellRunner::encodedCommand(script)});
#ifdef Q_OS_WIN
    // Không để cửa sổ console của powershell.exe lóe lên giữa lúc ứng dụng đang đóng.
    helper.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    if (!helper.startDetached())
    {
        Logger::instance().warning("Update", "Không khởi chạy được tiến trình trợ giúp cài đặt bản mới.");
        emit downloadFailed("Không khởi chạy được trình cài đặt.");
        return;
    }

    emit aboutToRestart();
}

namespace UpdateInstallerInternal
{

UpdateInstaller::InstallKind classifyInstall(bool msiMarkerPresent, bool innoKeyPresent, const QString& appDir,
                                             const QString& msiInstallDir, const QString& innoInstallDir)
{
    const QString app = normalizedDir(appDir);
    if (app.isEmpty())
        return UpdateInstaller::InstallKind::Portable;

    if (innoKeyPresent && !innoInstallDir.isEmpty() && normalizedDir(innoInstallDir) == app)
        return UpdateInstaller::InstallKind::InnoSetup;
    if (msiMarkerPresent && normalizedDir(msiInstallDir) == app)
        return UpdateInstaller::InstallKind::Msi;
    return UpdateInstaller::InstallKind::Portable;
}

QString buildHelperScript(UpdateInstaller::InstallKind kind, const QString& installerPath,
                          const QString& appExePath, qint64 appPid)
{
    const auto q = [](const QString& path) {
        return "'" + PowerShellRunner::quoteLiteral(QDir::toNativeSeparators(path)) + "'";
    };

    // Script này chạy TÁCH RỜI, SAU KHI ứng dụng đã thoát (qApp->quit()) - không còn QObject/Logger nào
    // ghi lại được chuyện gì xảy ra tiếp theo. Trước đây hoàn toàn "mù": nếu trình cài đặt âm thầm thất
    // bại (vd UAC bị hủy vì không ai bấm kịp khi script chạy ẩn, file đích tạm thời bị khóa, đĩa đầy...),
    // không có cách nào biết - người dùng chỉ thấy "tải 100% rồi vẫn bản cũ, mở lại lại hỏi cập nhật" mà
    // không rõ vì sao (xem CLAUDE.md mục 5/PROJECT_OVERVIEW.md mục 4t, v1.19.5). Giờ tự ghi nhật ký riêng
    // + để lại dấu vết lỗi cho chính ứng dụng (mở lại) tự đọc và báo rõ ràng thay vì im lặng hỏi lại từ đầu.
    const QString updateDir = QFileInfo(installerPath).absolutePath();
    const QString logPath = updateDir + "/update_helper.log";
    const QString markerPath = updateDir + "/update_failed.marker";

    QString script;
    script += "$ErrorActionPreference='SilentlyContinue'\n";
    script += QString("$logPath = %1\n").arg(q(logPath));
    script += QString("$markerPath = %1\n").arg(q(markerPath));
    script += "function Log($msg) { Add-Content -Path $logPath -Value \"$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') $msg\" -ErrorAction SilentlyContinue }\n";
    script += "Remove-Item $markerPath -ErrorAction SilentlyContinue\n";
    script += QString("Log 'Cho PID %1 thoat (toi da 60s)...'\n").arg(appPid);
    // Chờ CHÍNH tiến trình ứng dụng thoát (tối đa 60 giây) - trình cài đặt không thể ghi đè exe/DLL đang
    // bị giữ, và Inno Setup tự hủy trong im lặng nếu còn thấy AppMutex.
    script += QString("Wait-Process -Id %1 -Timeout 60\n").arg(appPid);
    // 500ms trước đây đôi khi KHÔNG ĐỦ: Windows không đảm bảo named kernel object (AppMutex) được dọn sạch
    // NGAY lúc Wait-Process trả về - Inno Setup kiểm tra Mutex đó ngay sau, thấy "còn" là tự hủy cài đặt
    // trong im lặng (/SUPPRESSMSGBOXES không báo gì). Tăng lên 1500ms cho an toàn hơn.
    script += "Start-Sleep -Milliseconds 1500\n";
    script += "Log 'Bat dau chay trinh cai dat...'\n";

    if (kind == UpdateInstaller::InstallKind::Msi)
    {
        // msiexec nhận đường dẫn gói trong dấu nháy KÉP của chính nó; /qn: không giao diện; /norestart:
        // không tự khởi động lại MÁY. MajorUpgrade trong Product.wxs tự gỡ bản cũ.
        const QString msiNative = QDir::toNativeSeparators(installerPath);
        script += QString("$p = Start-Process -FilePath %1 -ArgumentList @('/i', %2, '/qn', '/norestart') -Wait -PassThru\n")
                      .arg(q(AppPaths::systemExecutable("msiexec.exe")),
                           "'\"" + PowerShellRunner::quoteLiteral(msiNative) + "\"'");
    }
    else
    {
        // /VERYSILENT: không hiện wizard. /SUPPRESSMSGBOXES: không hỏi gì giữa chừng. /NORESTART: không
        // tự khởi động lại MÁY. /CLOSEAPPLICATIONS: nhờ Restart Manager đóng một bản sao KHÁC của ứng
        // dụng (nếu người dùng mở nhiều cửa sổ) đang giữ tệp.
        script += QString("$p = Start-Process -FilePath %1 -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CLOSEAPPLICATIONS') -Wait -PassThru\n")
                      .arg(q(installerPath));
    }

    // Exit code 0 = thành công; 3010 = thành công nhưng cần khởi động lại MÁY (hiếm khi xảy ra với app
    // này, không có file hệ thống bị khóa) - mọi giá trị khác coi là thất bại, để lại dấu vết cho ứng
    // dụng tự đọc. UAC bị hủy (installer cần quyền cao hơn hiện có mà không ai bấm "Yes" kịp vì script
    // chạy ẩn) trả ERROR_CANCELLED (1223) - trường hợp hay gặp nhất nếu còn tái diễn.
    script += "Log \"Trinh cai dat ket thuc, ExitCode=$($p.ExitCode)\"\n";
    script += "if ($p.ExitCode -ne 0 -and $p.ExitCode -ne 3010) {\n";
    script += "  Log 'CAI DAT THAT BAI - ghi marker loi'\n";
    script += "  Set-Content -Path $markerPath -Value \"ExitCode=$($p.ExitCode)\" -ErrorAction SilentlyContinue\n";
    script += "} else {\n";
    script += "  Log 'Cai dat thanh cong'\n";
    script += "}\n";

    // Mở lại ứng dụng dù cài thành công hay thất bại: thành công thì là bản mới; thất bại thì bản cũ vẫn
    // còn nguyên và sẽ tự hỏi cập nhật lại - người dùng không bị bỏ lại với một ứng dụng "biến mất". Nếu
    // thất bại, ứng dụng (mở lại, dù bản nào) tự đọc update_failed.marker lúc khởi động để báo rõ lý do.
    const QString appDir = QFileInfo(appExePath).absolutePath();
    script += "Log 'Mo lai ung dung...'\n";
    script += QString("Start-Process -FilePath %1 -WorkingDirectory %2\n").arg(q(appExePath), q(appDir));
    script += "Log 'Hoan tat.'\n";
    return script;
}

QString sha256OfFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return QString();
    return QString::fromLatin1(hash.result().toHex()).toLower();
}

} // namespace UpdateInstallerInternal
