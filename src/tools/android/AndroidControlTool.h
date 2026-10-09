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
    /// Dừng phiên scrcpy + daemon adb đóng gói kèm khi ứng dụng thoát mà cửa sổ Android Phone Control còn
    /// mở (closeEvent của nó không chạy trên đường thoát qApp->quit()).
    void stopBackgroundWorkForQuit() override;

private:
    QPointer<QWidget> m_window; // một cửa sổ duy nhất
};
