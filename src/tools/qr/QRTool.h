#pragma once

#include "../../core/Tool.h"
#include <QPointer>

class QRTool : public ITool
{
public:
    QRTool() = default;
    ~QRTool() override = default;

    QString id() const override { return "qr"; }
    QString name() const override { return "QR Tools"; }
    QString description() const override;
    QString iconPath() const override { return ":/icons/QR.jpg"; }
    QIcon icon() const override;
    bool isAvailable() const override { return true; }

    QWidget* createWindow() override;

private:
    QPointer<QWidget> m_window; // một cửa sổ duy nhất
};
