#pragma once

#include "../../core/Tool.h"
#include <QPointer>

class DiskCleanupTool : public ITool
{
public:
    DiskCleanupTool() = default;
    ~DiskCleanupTool() override = default;

    QString id() const override { return "disk"; }
    QString name() const override { return "Disk Cleanup"; }
    QString description() const override;
    QString iconPath() const override { return ":/icons/cleaner.png"; }
    QIcon icon() const override;
    bool isAvailable() const override { return true; }

    QWidget* createWindow() override;

private:
    QPointer<QWidget> m_window; // một cửa sổ duy nhất
};
