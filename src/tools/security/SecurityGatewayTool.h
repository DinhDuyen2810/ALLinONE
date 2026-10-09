#pragma once

#include "../../core/Tool.h"
#include <QPointer>

class SecurityGatewayTool : public ITool
{
public:
    SecurityGatewayTool() = default;
    ~SecurityGatewayTool() override = default;

    QString id() const override { return "security"; }
    QString name() const override { return "Security Gateway"; }
    QString description() const override;
    QString iconPath() const override { return ":/icons/gateway.png"; }
    QIcon icon() const override;
    bool isAvailable() const override { return true; }

    QWidget* createWindow() override;
    void stopBackgroundWorkForQuit() override;

private:
    QPointer<QWidget> m_window; // một cửa sổ duy nhất
};
