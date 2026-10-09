#include "DuplicateFinder.h"

#include "FsSafety.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QFile>
#include <QHash>
#include <QPair>
#include <QSet>

#include <algorithm>

namespace
{
/// Hash SHA-256 nội dung tệp, đọc theo khối để không nạp cả tệp lớn vào bộ nhớ. Kiểm tra cờ dừng
/// định kỳ vì tệp có thể rất lớn. *ok=false nếu không mở/đọc được (quyền truy cập...) hoặc bị dừng
/// giữa chừng - khi đó kết quả không được coi là một trùng lặp đáng tin.
QString hashFile(const QString& path, const std::atomic_bool& stopFlag, bool* ok)
{
    *ok = false;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};

    QCryptographicHash hasher(QCryptographicHash::Sha256);
    char buffer[1 << 16]; // 64 KB mỗi lần đọc
    int counter = 0;
    while (!f.atEnd())
    {
        if ((++counter & 0x3F) == 0 && stopFlag.load())
            return {};
        const qint64 n = f.read(buffer, sizeof(buffer));
        if (n < 0)
            return {}; // lỗi đọc giữa chừng
        hasher.addData(QByteArrayView(buffer, static_cast<qsizetype>(n)));
    }
    *ok = true;
    return QString::fromLatin1(hasher.result().toHex());
}
} // namespace

DuplicateFinder::DuplicateFinder(QObject* parent)
    : QThread(parent)
{
}

DuplicateFinder::~DuplicateFinder()
{
    // Chờ KHÔNG giới hạn - xem CleanupScanner::~CleanupScanner (cờ dừng được kiểm tra ở từng mục/từng
    // 4MB khi băm).
    requestStop();
    wait();
}

void DuplicateFinder::requestStop()
{
    m_stopRequested = true;
}

void DuplicateFinder::startScan(QThread::Priority priority)
{
    if (isRunning())
        return;
    m_stopRequested = false;
    start(priority);
}

bool DuplicateFinder::fileUnchanged(const QString& path, qint64 sizeBytes, qint64 lastWriteTime)
{
    const FsSafety::RawInfo info = FsSafety::rawInfo(path);
    return info.exists && !info.isDir && !info.isLink && info.sizeBytes == sizeBytes && info.lastWriteTime == lastWriteTime;
}

namespace
{
struct Candidate
{
    QString path;
    qint64 lastWriteTime{0};
};
} // namespace

void DuplicateFinder::run()
{
    // Bước 1: liệt kê tệp + nhóm theo kích thước (rẻ, không đọc nội dung). FsSafety::walkFiles bỏ qua
    // symlink/lối tắt và không đi vào junction/symlink thư mục - xem ghi chú ở DuplicateFinder.h.
    QHash<qint64, QList<Candidate>> bySize;
    qint64 filesScanned = 0;
    if (!m_rootPath.isEmpty())
    {
        FsSafety::walkFiles(m_rootPath, m_stopRequested, [&](const FsSafety::WalkEntry& file) {
            if (file.sizeBytes >= m_minSizeBytes)
                bySize[file.sizeBytes].push_back({file.path, file.lastWriteTime});

            // Cứ mỗi 200 tệp thì báo tiến độ (kèm đường dẫn hiện tại) - quét cả ổ đĩa hệ thống có thể gặp
            // hàng trăm nghìn tệp, cần phản hồi đủ dày để không bị tưởng nhầm là treo.
            if ((++filesScanned % 200) == 0)
                emit progressTick(filesScanned, 0, file.path);
        });
    }

    if (m_stopRequested)
    {
        emit scanStopped();
        return;
    }

    // Bước 2: trong mỗi nhóm kích thước có >= 2 tệp, hash nội dung để xác nhận trùng thật (không
    // chỉ trùng kích thước ngẫu nhiên). GOM VÀO BỘ NHỚ, KHÔNG phát tín hiệu từng nhóm một - thư mục
    // như cache trình duyệt có thể tạo ra hàng nghìn "nhóm trùng lặp" (các khối cache cùng cỡ cố
    // định), nếu luồng giao diện phải dựng hàng nghìn mục cây liên tiếp theo từng tín hiệu riêng lẻ,
    // có nguy cơ Windows đánh dấu cửa sổ "Không phản hồi" (đã xác nhận bằng kiểm tra thật). Thay vào
    // đó, đợi quét xong, sắp xếp theo dung lượng lãng phí giảm dần rồi CHỈ PHÁT MỘT LẦN - đúng mẫu
    // LargeFileScanner đã được đo thực tế là không gây treo UI dù quét hàng trăm nghìn tệp.
    QList<DuplicateGroup> allGroups;
    qint64 wastedTotal = 0;
    qint64 filesHashed = 0;
    for (auto sizeIt = bySize.constBegin(); sizeIt != bySize.constEnd(); ++sizeIt)
    {
        if (sizeIt.value().size() < 2)
            continue;

        // Khử trùng theo danh tính tệp vật lý TRƯỚC khi băm: mỗi tệp trên đĩa chỉ được đại diện bởi MỘT
        // đường dẫn. Không lấy được danh tính (không mở được tệp) thì loại luôn - thà bỏ sót một bản
        // trùng còn hơn đề nghị xóa một tệp chưa chắc là bản sao.
        QList<Candidate> distinctFiles;
        QSet<QPair<quint32, quint64>> seenIdentities;
        for (const Candidate& candidate : sizeIt.value())
        {
            FsSafety::FileIdentity id;
            if (!FsSafety::fileIdentity(candidate.path, &id))
                continue;
            const QPair<quint32, quint64> key(id.volumeSerial, id.fileIndex);
            if (seenIdentities.contains(key))
                continue;
            seenIdentities.insert(key);
            distinctFiles.push_back(candidate);
        }
        if (distinctFiles.size() < 2)
            continue;

        QHash<QString, QList<Candidate>> byHash;
        for (const Candidate& candidate : distinctFiles)
        {
            const QString& path = candidate.path;
            if (m_stopRequested)
            {
                emit scanStopped();
                return;
            }

            bool ok = false;
            const QString hash = hashFile(path, m_stopRequested, &ok);
            if (m_stopRequested)
            {
                emit scanStopped();
                return;
            }
            if (!ok)
                continue; // không đọc được - bỏ qua, không coi là trùng lặp

            byHash[hash].push_back(candidate);
            if ((++filesHashed & 0x1F) == 0)
                emit progressTick(filesScanned, filesHashed, path);
        }

        for (auto hashIt = byHash.constBegin(); hashIt != byHash.constEnd(); ++hashIt)
        {
            if (hashIt.value().size() < 2)
                continue;
            DuplicateGroup group;
            for (const Candidate& member : hashIt.value())
            {
                group.paths << member.path;
                group.lastWriteTimes << member.lastWriteTime;
            }
            group.sizeEachBytes = sizeIt.key();
            group.hashHex = hashIt.key();
            wastedTotal += group.wastedBytes();
            allGroups.push_back(group);
        }
    }

    std::sort(allGroups.begin(), allGroups.end(), [](const DuplicateGroup& a, const DuplicateGroup& b) {
        return a.wastedBytes() > b.wastedBytes();
    });
    const int totalGroupsFound = allGroups.size();
    if (allGroups.size() > m_maxGroups)
        allGroups.resize(m_maxGroups);

    emit scanFinished(allGroups, wastedTotal, totalGroupsFound);
}
