#include "CleanupExecutor.h"

#include "CleanupSystem.h"
#include "FsSafety.h"
#include "RecycleBinOps.h"
#include "SafePurge.h"

#include <QDir>
#include <QElapsedTimer>
#include <QHash>
#include <QSet>

CleanupTask CleanupTask::forItem(const CleanupCategory& category, const CleanupItem& item)
{
    CleanupTask t;
    t.path = item.path;
    t.sizeBytes = item.sizeBytes;
    t.minAgeSeconds = category.minAgeSeconds; // được kiểm LẠI ngay trước khi xóa
    if (category.kind == CleanupKind::RecycleBin)
        t.mode = Mode::EmptyRecycleBin;
    else if (category.kind == CleanupKind::Hibernation)
        t.mode = Mode::TurnOffHibernation;
    else if (category.regenerable && !item.root.isEmpty())
    {
        t.mode = Mode::Purge;
        t.trustedRoot = item.root;
    }
    else
        t.mode = Mode::Shell;
    return t;
}

CleanupExecutor::CleanupExecutor(QObject* parent)
    : QThread(parent)
{
}

CleanupExecutor::~CleanupExecutor()
{
    requestStop();
    wait();
}

void CleanupExecutor::setItems(const QStringList& paths, const QList<qint64>& sizes, const QList<int>& minAgeSeconds)
{
    m_tasks.clear();
    m_stopRequested = false;
    for (int i = 0; i < paths.size(); ++i)
    {
        CleanupTask t;
        t.path = paths[i];
        t.sizeBytes = i < sizes.size() ? sizes[i] : 0;
        t.minAgeSeconds = i < minAgeSeconds.size() ? minAgeSeconds[i] : 0;
        t.mode = CleanupTask::Mode::Shell;
        m_tasks << t;
    }
}

namespace
{
/// Thời điểm sửa MỚI NHẤT của một mục, tính như CleanupScanner: với thư mục là tệp mới nhất bên trong
/// (không đi vào junction/symlink thư mục) hoặc chính thư mục đó, cái nào mới hơn.
qint64 newestWriteTime(const QString& path, const FsSafety::RawInfo& info)
{
    qint64 newest = info.lastWriteTime;
    if (info.isDir)
    {
        const std::atomic_bool neverStop{false};
        FsSafety::walkFiles(path, neverStop, [&newest](const FsSafety::WalkEntry& e) {
            if (e.lastWriteTime > newest)
                newest = e.lastWriteTime;
        });
    }
    return newest;
}

bool tooRecent(const QString& path, const FsSafety::RawInfo& info, int minAgeSeconds)
{
    return minAgeSeconds > 0 &&
           newestWriteTime(path, info) > FsSafety::nowFileTime() - static_cast<qint64>(minAgeSeconds) * 10000000LL;
}
} // namespace

void CleanupExecutor::run()
{
    if (m_tasks.isEmpty())
    {
        emit executionFinished(true, QString(), 0, 0, 0);
        return;
    }

    qint64 totalPlanned = 0;
    for (const CleanupTask& t : m_tasks)
        totalPlanned += qMax<qint64>(0, t.sizeBytes);
    qint64 done = 0;
    QElapsedTimer progressClock;
    progressClock.start();
    auto reportProgress = [&](bool force) {
        if (force || progressClock.elapsed() >= 200)
        {
            emit progress(qMin(done, totalPlanned), totalPlanned);
            progressClock.restart();
        }
    };

    qint64 freedBytes = 0;
    qint64 recycledBytes = 0;
    int freedCount = 0;
    int refusedCount = 0;
    int recentCount = 0;
    int purgeInUse = 0;
    int purgeFailed = 0;
    QStringList notes;
    bool anythingToDo = false;

    // ---- Bước 0: làm trống Thùng rác TRƯỚC (không xóa luôn những thứ chính lượt này vừa đưa vào Thùng rác) ----
    for (const CleanupTask& t : m_tasks)
    {
        if (t.mode != CleanupTask::Mode::EmptyRecycleBin)
            continue;
        anythingToDo = true;
        const RecycleBinOps::Status before = RecycleBinOps::queryStatus();
        QString err;
        if (!RecycleBinOps::empty(&err))
            notes << err;
        const RecycleBinOps::Status after = RecycleBinOps::queryStatus();
        freedBytes += qMax<qint64>(0, before.totalBytes - after.totalBytes);
        freedCount += static_cast<int>(qMax<qint64>(0, before.itemCount - after.itemCount));
        done += t.sizeBytes;
        reportProgress(true);
    }

    // ---- Bước 1: tắt ngủ đông bằng powercfg (nâng quyền - UAC) ----
    for (const CleanupTask& t : m_tasks)
    {
        if (t.mode != CleanupTask::Mode::TurnOffHibernation)
            continue;
        anythingToDo = true;
        QString err;
        if (CleanupSystem::turnOffHibernation(&err))
        {
            if (!FsSafety::existsNoFollow(t.path))
            {
                freedBytes += t.sizeBytes;
                ++freedCount;
            }
            else
            {
                notes << "Đã tắt ngủ đông nhưng hiberfil.sys vẫn còn - Windows sẽ gỡ sau khi khởi động lại.";
            }
        }
        else
        {
            notes << "Không tắt được ngủ đông: " + err;
        }
        done += t.sizeBytes;
        reportProgress(true);
    }

    // ---- Bước 2: dữ liệu tự tạo lại - xóa HẲN từng tệp (SafePurge) ----
    QSet<QString> seen;
    QHash<QString, bool> rootVerdicts; // thư mục gốc tin cậy -> có an toàn không (kiểm một lần cho mỗi gốc)
    const QStringList protectedDirs = FsSafety::systemProtectedDirs();
    for (const CleanupTask& t : m_tasks)
    {
        if (t.mode != CleanupTask::Mode::Purge)
            continue;
        if (m_stopRequested)
            break;
        anythingToDo = true;
        const QString path = QDir::cleanPath(QDir::fromNativeSeparators(t.path));
        const QString root = QDir::cleanPath(QDir::fromNativeSeparators(t.trustedRoot));
        const QString key = path.toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);

        // Gốc tin cậy phải là một thư mục/tệp dọn dẹp hợp lệ (không rỗng, không phải gốc ổ đĩa, không trùng/chứa
        // thư mục được bảo vệ) và mục phải nằm trong nó theo chuỗi - SafePurge còn kiểm lại theo đường dẫn THẬT.
        if (!rootVerdicts.contains(root.toLower()))
            rootVerdicts.insert(root.toLower(), !root.isEmpty() && FsSafety::unsafeCleanupRootReason(root, protectedDirs).isEmpty());
        if (!rootVerdicts.value(root.toLower()) || !SafePurge::isInside(path, root))
        {
            ++refusedCount;
            done += t.sizeBytes;
            continue;
        }
        const FsSafety::RawInfo info = FsSafety::rawInfo(path);
        if (!info.exists)
        {
            // Đã tự biến mất từ lúc quét - mục tiêu đạt được.
            freedBytes += t.sizeBytes;
            ++freedCount;
            done += t.sizeBytes;
            continue;
        }
        if (info.isLink)
        {
            ++refusedCount;
            done += t.sizeBytes;
            continue;
        }
        if (tooRecent(path, info, t.minAgeSeconds))
        {
            ++recentCount;
            done += t.sizeBytes;
            continue;
        }
        const qint64 doneBefore = done;
        const SafePurge::Result r = SafePurge::purge(path, root, m_stopRequested, [&](qint64 bytes) {
            done += bytes;
            reportProgress(false);
        });
        done = doneBefore + t.sizeBytes;
        reportProgress(false);
        if (r.rootRefused)
        {
            ++refusedCount;
            continue;
        }
        freedBytes += r.freedBytes;
        freedCount += r.deletedFiles;
        purgeInUse += r.inUseFiles;
        purgeFailed += r.failedFiles;
        refusedCount += r.refusedFiles;
    }

    // ---- Bước 3: dữ liệu của người dùng - qua Windows Shell (Thùng rác hoặc xóa vĩnh viễn) ----
    //  - Mục ĐÃ TỰ BIẾN MẤT từ lúc quét tới giờ (rất thường gặp với tệp tạm/cache - Windows hoặc ứng dụng
    //    khác liên tục tạo/xóa chúng) coi là ĐÃ ĐẠT MỤC TIÊU, không đưa vào yêu cầu xóa (và không gây
    //    SHFileOperationW báo lỗi ERROR_FILE_NOT_FOUND cho CẢ LÔ chỉ vì 1 mục không còn đó). Kiểm tra bằng
    //    FsSafety (không đi theo liên kết): QFileInfo::exists() trả về false cho một lối tắt .lnk CÒN ĐÓ
    //    nhưng đích đã mất - lối tắt hỏng trong "Tệp gần đây" từng được tính "đã giải phóng" mà không hề xóa.
    //  - Đường dẫn LẶP (hai hạng mục cùng trỏ vào một thư mục, vd "Tệp tạm hệ thống" và "Tệp nhật ký" đều
    //    quét %WINDIR%\Temp) chỉ xóa/tính MỘT lần.
    //  - Đường dẫn rỗng/tương đối/gốc ổ đĩa, đường dẫn mà Windows hiểu thành một đối tượng khác (tên có dấu
    //    chấm/khoảng trắng cuối...) và junction/symlink THƯ MỤC không bao giờ được xóa từ đây.
    //  - Chế độ Thùng rác: mục KHÔNG vào Thùng rác được (quá lớn, Thùng rác tắt, ổ không có Thùng rác)
    //    được GIỮ NGUYÊN - Shell sẽ hủy hẳn chúng nếu cứ đưa vào (xem RecycleBinOps::moveToRecycleBin).
    QStringList toDelete;
    QList<qint64> toDeleteSizes;
    int notRecyclableCount = 0;
    QString notRecyclableExample;
    for (const CleanupTask& t : m_tasks)
    {
        if (t.mode != CleanupTask::Mode::Shell)
            continue;
        anythingToDo = true;
        const QString path = QDir::cleanPath(t.path);
        const QString key = path.toLower();
        if (seen.contains(key))
            continue;
        seen.insert(key);

        const qint64 size = t.sizeBytes;
        // unsafeShellPathReason (không chỉ unsafeCleanupRootReason): còn chặn cả tên mà Shell hiểu thành
        // một tệp KHÁC - vd "thư mục/tệp." (dấu chấm cuối) làm Shell xóa "thư mục/tệp".
        if (!FsSafety::unsafeShellPathReason(path).isEmpty())
        {
            ++refusedCount;
            continue;
        }
        const FsSafety::RawInfo info = FsSafety::rawInfo(path);
        if (!info.exists)
        {
            freedBytes += size;
            ++freedCount;
            continue;
        }
        if (info.isDir && info.isLink)
        {
            ++refusedCount;
            continue;
        }
        // Thư mục của người dùng (vd một thư mục "mồ côi" trong AppData) không bao giờ được là/chứa một thư mục
        // được bảo vệ (hồ sơ, Downloads, Program Files...) - lớp chặn cuối ngay trước khi giao cho Shell.
        if (info.isDir && !FsSafety::unsafeCleanupRootReason(path, protectedDirs).isEmpty())
        {
            ++refusedCount;
            continue;
        }
        // Kiểm lại tuổi NGAY TRƯỚC KHI XÓA (máy quét chỉ lọc tại thời điểm quét): tệp tạm vừa được ghi sau
        // khi quét nhiều khả năng thuộc một chương trình/trình cài đặt đang chạy - giữ lại.
        if (tooRecent(path, info, t.minAgeSeconds))
        {
            ++recentCount;
            continue;
        }
        if (!m_permanent)
        {
            // Tệp có thể đã LỚN LÊN từ lúc quét - so với kích thước hiện tại, không chỉ kích thước cũ.
            const QString reason = RecycleBinOps::notRecyclableReason(path, info.isDir ? size : qMax(size, info.sizeBytes));
            if (!reason.isEmpty())
            {
                ++notRecyclableCount;
                if (notRecyclableExample.isEmpty())
                    notRecyclableExample = QDir::toNativeSeparators(path) + " - " + reason;
                continue;
            }
        }
        toDelete << path;
        toDeleteSizes << size;
    }

    QString error;
    bool ok = true;
    if (!toDelete.isEmpty())
        ok = m_permanent ? RecycleBinOps::permanentlyDelete(toDelete, &error) : RecycleBinOps::moveToRecycleBin(toDelete, &error);

    // SHFileOperationW không cho biết CHÍNH XÁC mục nào thất bại khi có lỗi giữa chừng (có thể đã xóa được phần
    // lớn trước khi gặp 1 mục lỗi) - kiểm tra lại THẬT xem mục nào không còn tồn tại nữa (đã xóa thành công)
    // thay vì đoán mò theo mã trả về, để báo cáo số liệu chính xác dù thất bại một phần.
    int failedCount = 0;
    for (int i = 0; i < toDelete.size(); ++i)
    {
        if (!FsSafety::existsNoFollow(toDelete[i]))
        {
            freedBytes += toDeleteSizes[i];
            if (!m_permanent)
                recycledBytes += toDeleteSizes[i];
            ++freedCount;
        }
        else
        {
            ++failedCount;
        }
    }
    done = totalPlanned;
    reportProgress(true);

    // Thành công = có giải phóng được gì đó. Chỉ coi là thất bại thật sự khi KHÔNG có mục nào được giải
    // phóng dù có mục cần xóa - tránh dọa người dùng bằng hộp thoại lỗi đỏ cho một trường hợp thường gặp
    // và vô hại (vài tệp tạm tự mất giữa lúc quét và lúc xóa).
    const int leftCount = failedCount + refusedCount + notRecyclableCount + recentCount + purgeInUse + purgeFailed;
    const bool success = freedCount > 0 || freedBytes > 0 || leftCount == 0 || !anythingToDo;
    if (purgeInUse > 0)
        notes << QString("%1 tệp đang được ứng dụng khác mở nên được GIỮ LẠI (không ép xóa) - đóng ứng dụng đó rồi quét "
                         "và dọn lại nếu muốn dọn hết.")
                     .arg(purgeInUse);
    if (purgeFailed > 0)
        notes << QString("%1 tệp không xóa được (thường do thiếu quyền - các hạng mục của Windows cần chạy ứng dụng với "
                         "quyền Quản trị).")
                     .arg(purgeFailed);
    if (failedCount > 0)
    {
        // Luôn kèm SỐ mục: một mục lỗi không còn chặn các mục sau nó (xem RecycleBinOps), nên thông báo
        // lỗi của Shell chỉ nói về mục lỗi ĐẦU TIÊN.
        QString note = QString("%1 mục không xóa được (có thể đang được chương trình khác sử dụng).").arg(failedCount);
        if (!ok && !error.isEmpty())
            note += " Lỗi đầu tiên: " + error;
        notes << note;
    }
    if (notRecyclableCount > 0)
    {
        notes << QString("%1 mục được GIỮ NGUYÊN vì không thể đưa vào Thùng rác để khôi phục (vd: %2). Bật "
                         "\"Xóa vĩnh viễn\" nếu thật sự muốn xóa các mục này.")
                     .arg(notRecyclableCount)
                     .arg(notRecyclableExample);
    }
    if (recentCount > 0)
        notes << QString("%1 mục được GIỮ LẠI vì vừa được sửa sau khi quét (có thể đang được một chương trình khác "
                         "sử dụng) - quét lại sau vài phút nếu vẫn muốn dọn.")
                     .arg(recentCount);
    if (refusedCount > 0)
        notes << QString("%1 mục bị bỏ qua vì lý do an toàn (liên kết thư mục, tệp chỉ có trên mây hoặc đường dẫn không hợp "
                         "lệ).")
                     .arg(refusedCount);
    if (m_stopRequested)
        notes << "Đã dừng giữa chừng.";

    emit executionFinished(success, notes.join("\n"), freedBytes, freedCount, recycledBytes);
}
