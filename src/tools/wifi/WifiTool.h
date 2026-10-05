#pragma once

#include "../../core/Tool.h"
#include <QPointer>

class WifiTool : public ITool
{
public:
    WifiTool() = default;
    ~WifiTool() override = default;

    QString id() const override { return "wifi"; }
    QString name() const override { return "WiFi Connection"; }
    QString description() const override;
    QString iconPath() const override { return ":/icons/wifi.png"; }
    QIcon icon() const override;
    bool isAvailable() const override { return true; }

    QWidget* createWindow() override;

private:
    QPointer<QWidget> m_window; // một cửa sổ duy nhất
};
