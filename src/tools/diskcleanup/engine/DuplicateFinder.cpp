#include "DuplicateFinder.h"

#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>

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
    requestStop();
    wait(5000);
}

void DuplicateFinder::requestStop()
{
    m_stopRequested = true;
}

void DuplicateFinder::run()
{
    m_stopRequested = false;

    // Bước 1: liệt kê tệp + nhóm theo kích thước (rẻ, không đọc nội dung). Bỏ qua symlink/lối tắt
    // để không vô tình đệ quy lạc sang thư mục đích của một lối tắt - xem CleanupScanner.cpp.
    QHash<qint64, QStringList> bySize;
    qint64 filesScanned = 0;
    QDirIterator it(m_rootPath, QDir::Files | QDir::System | QDir::Hidden | QDir::NoSymLinks,
                    QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        it.next();
        const QFileInfo info = it.fileInfo();
        const qint64 size = info.size();
        if (size >= m_minSizeBytes)
            bySize[size] << info.absoluteFilePath();

        // Cứ mỗi 200 tệp thì kiểm tra dừng + báo tiến độ (kèm đường dẫn hiện tại) - quét cả ổ đĩa hệ
        // thống có thể gặp hàng trăm nghìn tệp, cần phản hồi đủ dày để không bị tưởng nhầm là treo.
        if ((++filesScanned % 200) == 0)
        {
            if (m_stopRequested)
            {
                emit scanStopped();
                return;
            }
            emit progressTick(filesScanned, 0, info.absoluteFilePath());
        }
    }

    if (m_stopRequested)
    {
        emit scanStopped();
        return;
    }

    // Bước 2: trong mỗi nhóm kích thước có >= 2 tệp, hash nội dung để xác nhận trùng thật (không
    // chỉ trùng kích thước ngẫu nhiên).
    int groupCount = 0;
    qint64 wastedTotal = 0;
    qint64 filesHashed = 0;
    for (auto sizeIt = bySize.constBegin(); sizeIt != bySize.constEnd(); ++sizeIt)
    {
        if (sizeIt.value().size() < 2)
            continue;

        QHash<QString, QStringList> byHash;
        for (const QString& path : sizeIt.value())
        {
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

            byHash[hash] << path;
            if ((++filesHashed & 0x1F) == 0)
                emit progressTick(filesScanned, filesHashed, path);
        }

        for (auto hashIt = byHash.constBegin(); hashIt != byHash.constEnd(); ++hashIt)
        {
            if (hashIt.value().size() < 2)
                continue;
            DuplicateGroup group;
            group.paths = hashIt.value();
            group.sizeEachBytes = sizeIt.key();
            group.hashHex = hashIt.key();
            emit groupFound(group);
            ++groupCount;
            wastedTotal += group.wastedBytes();
        }
    }

    emit scanFinished(groupCount, wastedTotal);
}
