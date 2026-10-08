#pragma once

#include "../../core/Tool.h"
#include <QPointer>

class DownloaderTool : public ITool
{
public:
    DownloaderTool() = default;
    ~DownloaderTool() override = default;

    QString id() const override { return "downloader"; }
    QString name() const override { return "Downloader"; }
    QString description() const override;
    QString iconPath() const override { return ":/icons/download.png"; }
    QIcon icon() const override;
    bool isAvailable() const override { return true; }

    QWidget* createWindow() override;
    void stopBackgroundWorkForQuit() override;

private:
    QPointer<QWidget> m_window; // một cửa sổ duy nhất
};
