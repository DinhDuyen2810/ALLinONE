#pragma once

#include "../../core/Tool.h"
#include <QPointer>

class VpnControlTool : public ITool
{
public:
    VpnControlTool() = default;
    ~VpnControlTool() override = default;

    QString id() const override { return "vpn"; }
    QString name() const override { return "VPN & Location"; }
    QString description() const override;
    QString iconPath() const override { return ":/icons/vpn.jpg"; }
    QIcon icon() const override;
    bool isAvailable() const override { return true; }

    QWidget* createWindow() override;

private:
    QPointer<QWidget> m_window; // một cửa sổ duy nhất
};
