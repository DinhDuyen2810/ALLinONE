#pragma once

#include "../../core/Tool.h"
#include <QPointer>

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

private:
    QPointer<QWidget> m_window; // chỉ cho phép một cửa sổ Auto Click để tránh nhiều runner tranh chấp chuột
};
