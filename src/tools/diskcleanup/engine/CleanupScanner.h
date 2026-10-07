#pragma once

#include <QList>
#include <QThread>
#include <atomic>

#include "../model/CleanupCategory.h"
#include "../model/CleanupItem.h"

/**
 * @brief Quét các hạng mục dọn dẹp trên một QThread riêng (giống ActionRunner của Auto Click) để
 * không làm đơ giao diện khi duyệt nhiều thư mục/tệp. Chỉ đọc (liệt kê + tính kích thước), không xóa
 * gì - việc xóa do CleanupExecutor đảm nhiệm sau khi người dùng xác nhận.
 */
class CleanupScanner : public QThread
{
    Q_OBJECT

public:
    explicit CleanupScanner(QObject* parent = nullptr);
    ~CleanupScanner() override;

    void setCategories(const QList<CleanupCategory>& categories);
    void requestStop();

signals:
    void categoryStarted(QString categoryName);
    void categoryFinished(CleanupCategoryId id, qint64 totalBytes, int itemCount);
    void itemFound(CleanupItem item);
    void scanFinished(qint64 totalBytes, int totalItems);
    void scanStopped();

protected:
    void run() override;

private:
    void scanCategory(const CleanupCategory& category, qint64* outBytes, int* outCount);

    QList<CleanupCategory> m_categories;
    std::atomic_bool m_stopRequested{false};
};
