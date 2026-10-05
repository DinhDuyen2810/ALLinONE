#pragma once

#include <QString>
#include <QIcon>
#include <QWidget>
#include <memory>

/**
 * @brief Interface đại diện cho một Tool độc lập trong One for ALL.
 * Tuân thủ mục 50 trong tài liệu thiết kế OneForAll_AutoClick_Design.md.
 */
class ITool
{
public:
    virtual ~ITool() = default;

    virtual QString id() const = 0;
    virtual QString name() const = 0;
    virtual QString description() const = 0;
    virtual QString iconPath() const = 0;
    virtual QIcon icon() const = 0;
    virtual bool isAvailable() const { return true; }

    /**
     * @brief Tạo cửa sổ riêng cho tool.
     * Cửa sổ này có thể được hiển thị độc lập hoặc nhúng theo nhu cầu.
     */
    virtual QWidget* createWindow() = 0;
};
