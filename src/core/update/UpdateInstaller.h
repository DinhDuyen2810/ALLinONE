#pragma once

#include <QObject>
#include <QString>

#include "UpdateChecker.h"

class FileDownloader;

/// Tải về + cài bản mới - dùng lại FileDownloader đã có sẵn (hỗ trợ tiếp tục tải dở qua HTTP Range, đã
/// test kỹ ở Downloader) thay vì viết lại logic tải HTTP.
///
/// Sau khi tải xong: kiểm SHA-256 theo "digest" của GitHub (nếu có), rồi khởi chạy một tiến trình trợ
/// giúp TÁCH RỜI (powershell.exe ẩn) làm đúng ba việc theo thứ tự: chờ chính tiến trình OneForAll.exe
/// này thoát hẳn → chạy trình cài đặt ở chế độ im lặng và chờ nó xong → mở lại ứng dụng. Nơi gọi phải
/// tự qApp->quit() khi nhận aboutToRestart().
///
/// Vì sao không còn dựa vào /RESTARTAPPLICATIONS của Inno Setup như bản đầu: (1) Inno chỉ mở lại được
/// ứng dụng ĐÃ đăng ký RegisterApplicationRestart và do chính Restart Manager đóng - ứng dụng này không
/// đăng ký, lại tự thoát trước, nên thực tế KHÔNG được mở lại; (2) trình cài đặt khởi động song song với
/// lúc ứng dụng đang thoát - nếu nó kiểm tra AppMutex trước khi ứng dụng kịp thoát thì ở chế độ
/// /SUPPRESSMSGBOXES nó tự hủy cài đặt trong im lặng. Chờ PID thoát rồi mới chạy trình cài đặt loại bỏ
/// cả hai vấn đề và dùng chung được cho cả bản .exe lẫn .msi.
class UpdateInstaller : public QObject
{
    Q_OBJECT

public:
    /// Ứng dụng đang chạy được cài bằng gì - quyết định tải asset nào và chạy nó ra sao.
    enum class InstallKind
    {
        Portable,  ///< Không tìm thấy dấu vết cài đặt (chạy từ thư mục giải nén/bản dev) - mở trình cài đặt .exe ở chế độ TƯƠNG TÁC
        InnoSetup, ///< Cài bằng OneForAll_Setup.exe
        Msi        ///< Cài bằng OneForAll_Setup.msi
    };

    explicit UpdateInstaller(QObject* parent = nullptr);
    ~UpdateInstaller() override;

    void downloadAndInstall(const UpdateInfo& info);

    static InstallKind detectInstallKind();

    /// Đọc + XÓA dấu vết lỗi của LẦN CẬP NHẬT TRƯỚC (nếu tiến trình trợ giúp chạy xong nhưng trình cài đặt
    /// thất bại sau khi đã tải xong thành công - xem buildHelperScript) - trả về chuỗi rỗng nếu không có
    /// gì (lần mở trước không cập nhật, hoặc cập nhật thành công). Gọi MỘT LẦN lúc khởi động (trước đây
    /// hoàn toàn im lặng trong trường hợp này - người dùng chỉ thấy "mở lại vẫn bản cũ, lại hỏi cập nhật"
    /// không rõ vì sao, xem PROJECT_OVERVIEW.md mục 4t). Gọi hai lần liên tiếp thì lần thứ 2 luôn rỗng.
    static QString consumePreviousUpdateFailure();

signals:
    /// bytesTotal có thể là 0 nếu GitHub không trả kích thước trước - UI nên hiện dạng không xác định.
    void progress(qint64 bytesReceived, qint64 bytesTotal);
    void downloadFailed(QString error);
    /// Phát ra NGAY SAU KHI đã khởi chạy thành công tiến trình trợ giúp/trình cài đặt - nơi gọi PHẢI tự
    /// đóng ứng dụng (qApp->quit()) ngay: tiến trình trợ giúp đang chờ đúng PID này thoát rồi mới cài.
    void aboutToRestart();

private:
    void onDownloadFinished(bool success);

    FileDownloader* m_downloader{nullptr};
    QString m_installerPath;
    QString m_expectedSha256;
    InstallKind m_kind{InstallKind::Portable};
};

namespace UpdateInstallerInternal
{
/// Phân loại THUẦN (test được, không đọc registry/đĩa): `msiMarkerPresent` = có giá trị
/// HKCU\Software\OneForAll\installed do Product.wxs ghi; `innoKeyPresent` = có khóa gỡ cài đặt của Inno
/// Setup (AppId trong OneForAll.iss, ở HKCU hoặc HKLM); `appDir` = thư mục chứa exe đang chạy;
/// `msiInstallDir` = %LOCALAPPDATA%\One for ALL (INSTALLFOLDER cố định của Product.wxs); `innoInstallDir`
/// = InstallLocation ghi trong khóa gỡ cài đặt của Inno (rỗng nếu không có). Chỉ coi là "đã cài" khi exe
/// đang chạy NẰM ĐÚNG trong thư mục cài tương ứng - một bản dev/portable chạy trên máy có cài sẵn bản khác
/// không được âm thầm ghi đè bản đó rồi mở lại chính bản cũ.
UpdateInstaller::InstallKind classifyInstall(bool msiMarkerPresent, bool innoKeyPresent, const QString& appDir,
                                             const QString& msiInstallDir, const QString& innoInstallDir);

/// Script PowerShell của tiến trình trợ giúp (xem mô tả lớp). Mọi đường dẫn được đặt trong chuỗi nháy đơn
/// qua PowerShellRunner::quoteLiteral.
QString buildHelperScript(UpdateInstaller::InstallKind kind, const QString& installerPath,
                          const QString& appExePath, qint64 appPid);

/// SHA-256 (hex thường) của một tệp, rỗng nếu không đọc được.
QString sha256OfFile(const QString& path);
} // namespace UpdateInstallerInternal
