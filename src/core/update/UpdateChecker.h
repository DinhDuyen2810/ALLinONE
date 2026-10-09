#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

/// Thông tin một bản phát hành lấy được từ GitHub Releases API.
struct UpdateInfo
{
    QString version;       ///< vd "1.16.0" (đã bỏ tiền tố "v" nếu tag đặt dạng "v1.16.0")
    QString downloadUrl;   ///< URL tải thẳng OneForAll_Setup.exe (asset đính kèm bản phát hành)
    QString releaseNotes;  ///< Nội dung mô tả bản phát hành (Markdown thô từ GitHub, hiển thị thô cũng đọc được)
    qint64 downloadSize{0}; ///< Byte - 0 nếu GitHub không trả kích thước
    QString sha256;        ///< SHA-256 (hex thường) của OneForAll_Setup.exe theo trường "digest" của GitHub - rỗng nếu API không trả

    // Bản .msi (tùy chọn - rỗng nếu bản phát hành không đính kèm). Máy cài bằng .msi PHẢI cập nhật bằng
    // .msi: chạy OneForAll_Setup.exe (Inno Setup) trên một bản cài MSI sẽ cài THÊM một bản thứ hai vào
    // thư mục khác thay vì nâng cấp bản đang chạy - xem UpdateInstaller::detectInstallKind().
    QString msiUrl;
    qint64 msiSize{0};
    QString msiSha256;
};

/// Hỏi GitHub Releases API (repo mã nguồn chính của dự án) xem có bản mới hơn phiên bản đang chạy hay
/// không - endpoint CÔNG KHAI, không cần token (giới hạn 60 lượt/giờ/IP của GitHub dư sức cho tần suất
/// một lần mỗi lúc mở ứng dụng). Theo yêu cầu người dùng: CHỈ hỏi một lần ngay sau khi khởi động (trễ vài
/// giây để không cạnh tranh tài nguyên lúc ứng dụng vừa mở), KHÔNG lặp lại định kỳ trong lúc đang chạy -
/// lần tiếp theo sẽ hỏi lại là lần người dùng MỞ LẠI ứng dụng. Xem UpdateInstaller.h cho bước tải về +
/// cài đặt thật khi có bản mới.
class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject* parent = nullptr);

    /// Hỏi ngay (không chờ), dùng nội bộ cho lần kiểm tra lúc khởi động - để public phòng khi sau này
    /// muốn thêm nút "Kiểm tra cập nhật" thủ công.
    void checkNow();

signals:
    void updateAvailable(UpdateInfo info);
    void upToDate();
    void checkFailed(QString error);

private:
    void onReplyFinished(QNetworkReply* reply);

    QNetworkAccessManager* m_nam{nullptr};
};

namespace UpdateCheckerInternal
{
/// So sánh 2 chuỗi phiên bản dạng "X.Y.Z" (bỏ qua tiền tố "v"/"V" nếu có, phần thiếu/không phải số coi
/// là 0 - vd "1.2" so với "1.2.0" là bằng nhau) - trả về <0 nếu a cũ hơn b, 0 nếu bằng, >0 nếu a mới hơn.
int compareVersions(const QString& a, const QString& b);

/// Phân tích JSON trả về từ GET /repos/{owner}/{repo}/releases/latest của GitHub - tách tag_name, tìm
/// asset có tên KHỚP ĐÚNG TUYỆT ĐỐI "OneForAll_Setup.exe" (không phải chỉ kết thúc bằng "Setup.exe" -
/// tránh khớp nhầm asset khác nếu một bản phát hành lỡ đính kèm nhiều file; "OneForAll_Setup.exe" là tên
/// output CỐ ĐỊNH, biết trước chính xác - xem OutputBaseFilename trong installer/OneForAll.iss), và nội
/// dung mô tả. Trả về UpdateInfo rỗng (version.isEmpty()) nếu JSON không đúng cấu trúc mong đợi hoặc bản
/// phát hành không đính kèm đúng tên asset này - coi như không có gì để tự cập nhật.
UpdateInfo parseLatestRelease(const QByteArray& json);

/// URL tải bản cập nhật có đáng tin không: BẮT BUỘC https và host đúng "github.com" (dạng
/// browser_download_url của GitHub Releases). Tệp tải về sẽ được CHẠY trên máy người dùng - không nhận
/// http thường hay host lạ dù JSON nói gì.
bool isTrustedDownloadUrl(const QString& url);
}
