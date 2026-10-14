#pragma once

#include "../model/MediaLink.h"
#include "../model/ResolvedMedia.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

/// Phần THUẦN (không mạng, không tiến trình) của bộ phân giải link - tách riêng để test được bằng chuỗi/JSON
/// mẫu lấy từ dữ liệu THẬT (xem tests/downloader_tests.cpp). LinkResolver (bất đồng bộ) ghép các hàm này lại
/// thành quy trình: Google Drive/Docs -> tệp trực tiếp -> yt-dlp -> quét HTML sâu -> trình duyệt ẩn.
namespace LinkResolverCore
{
enum class Site { Generic, Facebook, Instagram, TikTok, YouTube, GoogleDrive, GoogleDocs };
Site siteOf(const QString& url);

// ---------------------------------------------------------------- Nhập liệu
/// Tách một đoạn văn bản người dùng dán (nhiều dòng/nhiều link ngăn bằng khoảng trắng, dấu phẩy) thành danh sách
/// URL http(s) không trùng, theo thứ tự xuất hiện. Dòng dạng "www.x.com/a" hoặc "x.com/a" (không scheme) được
/// thêm "https://". Dấu câu thừa cuối link (",;.)") bị bỏ. Chuỗi không giống link bị bỏ qua.
QStringList extractUrls(const QString& text);

// ---------------------------------------------------------------- Google Drive / Docs
/// Trích ID tệp từ link Google Drive (`/file/d/<ID>`, `open?id=`, `uc?id=`, `drive.usercontent.google.com/download?id=`).
/// Link THƯ MỤC (`/folders/`) KHÔNG phải tệp -> false.
bool parseDriveFileId(const QString& url, QString* fileId);
QString driveDirectUrl(const QString& fileId);
/// Google Docs/Sheets/Slides công khai -> URL xuất PDF/XLSX/PPTX. `suggestedExt` = đuôi tệp xuất.
bool googleDocsExportUrl(const QString& url, QString* exportUrl, QString* suggestedExt);

enum class DriveBlock { None, NoPermission, Quota, NeedsConfirm, SignIn };
/// Trang HTML mà Drive trả về THAY cho tệp. Phân biệt: chủ tệp không cho tải / vượt hạn mức / cần bấm xác nhận
/// (tệp lớn, "không quét được virus") / cần đăng nhập.
DriveBlock classifyDriveHtml(const QString& html);
/// Với DriveBlock::NeedsConfirm: dựng URL tải thật từ biểu mẫu xác nhận (các ô ẩn id/export/confirm/uuid).
QString driveConfirmUrl(const QString& html, const QString& baseUrl);
QString driveBlockMessage(DriveBlock block);

/// Tên tệp rút ra từ tiêu đề trang xem Drive ("Giáo trình.pptx - Google Drive"). `pageable` = false nếu loại tệp KHÔNG phải
/// tài liệu xem theo trang (video, âm thanh, ảnh, tệp nén, chương trình...) nên không chụp từng trang được. `baseName` = tên
/// bỏ đuôi tài liệu quen thuộc (pdf/pptx/docx/xlsx...), giữ nguyên nếu đuôi lạ; rỗng nếu tiêu đề không có tên (trang đăng nhập...).
struct DriveViewerName
{
    QString baseName;
    QString extension; ///< Đuôi gốc, viết thường, không dấu chấm (rỗng nếu không có)
    bool pageable{true};
};
DriveViewerName driveViewerName(const QString& pageTitle);

// ---------------------------------------------------------------- Loại link
/// Hồ sơ/kênh/danh sách phát (nhiều bài đăng): YouTube @kênh/channel/playlist, TikTok @user, Instagram trang cá
/// nhân, Facebook trang/hồ sơ (kể cả /videos, /reels).
bool isLikelyProfileOrCollectionUrl(const QString& url);
/// Bài đăng ẢNH mà yt-dlp không hỗ trợ: Facebook /photo, /photos/, fbid=; TikTok /photo/.
bool isPhotoPostUrl(const QString& url);
bool isManifestUrl(const QString& url); ///< .m3u8 / .mpd (luồng HLS/DASH - tải bằng yt-dlp)

// ---------------------------------------------------------------- Phân tích HTML
QString htmlTitle(const QString& html);
/// Giá trị `content` của thẻ <meta property|name="key"> (đã giải mã thực thể HTML); rỗng nếu không có.
QString metaContent(const QString& html, const QString& key);

struct DeepScan
{
    QList<MediaLink> media; ///< Ảnh/video/âm thanh/tài liệu/luồng HLS-DASH tìm thấy (URL tuyệt đối, không trùng)
    QStringList iframes;    ///< Khung nhúng (trình phát) - nơi gọi có thể tải tiếp 1 mức để quét sâu hơn
    QString title;
};
/// Quét HTML ở mức SÂU hơn PageMediaScanner: ngoài thẻ img/video/audio/source/a còn đọc thẻ meta (og:video, og:image,
/// twitter:player:stream), CHUỖI THÔ trong script/JSON (vd `"file":"https:\/\/cdn\/x\/index.m3u8"`, URL lồng trong
/// `player/?url=https://.../index.m3u8`, URL mã hóa phần trăm) và liệt kê iframe. Đây là chỗ bắt được link video của
/// trình phát JavaScript (JW Player...) - trang cấm chuột phải vẫn lộ link trong mã nguồn.
DeepScan deepScanHtml(const QString& html, const QString& baseUrl);

bool looksLikeChallengePage(const QString& html); ///< Cloudflare "Just a moment..." và tương tự
bool looksLikeLoginWall(const QString& html);

// ---------------------------------------------------------------- Bài đăng trong trang hồ sơ
/// Một bài đăng nhìn thấy trong DOM của trang hồ sơ: `photo` = bài ảnh (yt-dlp không hỗ trợ - phải dựng bằng trình duyệt
/// ẩn để lấy ảnh đầy đủ), ngược lại là video/reel (yt-dlp tải được). `id` = mã bài (fbid, mã video, mã TikTok...).
struct PostRef
{
    QString url;
    QString id;
    bool photo{false};
};
/// Các liên kết bài đăng (cùng trang, không trùng, theo thứ tự xuất hiện) trong DOM đã dựng của một trang hồ sơ:
///  - Facebook: /photo/?fbid=..., /photo.php?fbid=... (ảnh); /posts/<mã> (bài, có thể có ảnh); /<tên>/videos/<id>, /reel/<id>, /watch/?v=<id> (video).
///  - TikTok: /@<người dùng>/photo/<id> (ảnh), /@<người dùng>/video/<id> (video) - chỉ của CHÍNH hồ sơ đang xem.
///  - Instagram: /p/<mã>, /reel/<mã>, /tv/<mã> (đưa cho yt-dlp).
QList<PostRef> postLinksFromDom(const QString& dom, const QString& pageUrl, int maxCount = 80);
/// Mã số bài TikTok trong .../video/<id> hoặc .../photo/<id>; rỗng nếu không phải.
QString tiktokPostId(const QString& url);
/// .../@user/video/<id> -> .../@user/photo/<id>; rỗng nếu không phải link /video/ của TikTok.
QString tiktokPhotoUrlFromVideoUrl(const QString& url);

// ---------------------------------------------------------------- Trình duyệt ẩn (Edge/Chrome không giao diện)
/// URL media (m3u8/mpd/mp4/webm/mp3/m4a/flv) mà trang đã yêu cầu, lấy từ tệp NetLog của trình duyệt. Luồng
/// (manifest .m3u8/.mpd) xếp TRƯỚC. Không trả các đoạn .ts/.m4s.
/// Tệp media của GIAO DIỆN trang (video nền đăng nhập, âm thanh thông báo...) chứ không phải nội dung: host/đường dẫn
/// dạng "static", "login_static"... - loại khỏi kết quả trình duyệt ẩn.
bool isJunkMediaUrl(const QString& url);
QStringList mediaUrlsFromNetLog(const QByteArray& netLog, int maxCount = 40);
/// Ảnh "nội dung" trong DOM đã dựng (bỏ avatar/biểu tượng/ảnh nhỏ), có luật riêng cho Facebook và TikTok.
QList<MediaLink> photosFromDom(const QString& dom, const QString& pageUrl, int maxCount = 60);

// ---------------------------------------------------------------- yt-dlp
/// Đối số cho lượt PHÂN TÍCH (không tải): `--dump-single-json --flat-playlist ...`. `extra` = đối số cookie...
QStringList ytDlpAnalyzeArguments(const QString& url, int maxItems, const QStringList& extra);
/// JSON một lần `--dump-single-json` -> kết quả. Video đơn -> 1 mục; danh sách phát/hồ sơ -> nhiều mục (cắt theo
/// maxItems, đánh dấu truncated). JSON hỏng/rỗng -> error.
ResolveResult parseYtDlpJson(const QByteArray& json, const QString& sourceUrl, int maxItems);
/// Dịch lỗi yt-dlp (stderr) sang tiếng Việt dễ hiểu. `unsupported` = true nếu là "Unsupported URL" (nơi gọi nên thử
/// cách khác thay vì dừng).
QString translateYtDlpError(const QString& stderrText, bool* unsupported = nullptr);

// ---------------------------------------------------------------- Khác
QString fileNameFromContentDisposition(const QByteArray& header); ///< filename*=UTF-8''... hoặc filename="..."; đã làm sạch
QString friendlyTypeLabel(const QString& contentType, const QString& url); ///< "Ảnh"/"Video"/"Âm thanh"/"Tài liệu"/"Tệp"
} // namespace LinkResolverCore
