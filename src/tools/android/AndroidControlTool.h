#pragma once

#include "../../core/Tool.h"
#include <QPointer>

class AndroidControlTool : public ITool
{
public:
    AndroidControlTool() = default;
    ~AndroidControlTool() override = default;

    QString id() const override { return "android"; }
    QString name() const override { return "Android Phone Control"; }
    QString description() const override;
    QString iconPath() const override { return ":/icons/pcTOphone.png"; }
    QIcon icon() const override;
    bool isAvailable() const override { return true; }

    QWidget* createWindow() override;

private:
    QPointer<QWidget> m_window; // một cửa sổ duy nhất
};
