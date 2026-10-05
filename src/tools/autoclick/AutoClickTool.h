#pragma once

#include "../../core/Tool.h"

class AutoClickTool : public ITool
{
public:
    AutoClickTool() = default;
    ~AutoClickTool() override = default;

    QString id() const override { return "autoclick"; }
    QString name() const override { return "Auto Click"; }
    QString description() const override;
    QString iconPath() const override { return ":/icons/autoclicker.jpg"; }
    QIcon icon() const override;
    bool isAvailable() const override { return true; }

    QWidget* createWindow() override;
};
