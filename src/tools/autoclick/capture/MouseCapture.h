#pragma once

#include <QObject>
#include <QWidget>

class MouseCapture : public QObject
{
    Q_OBJECT

public:
    explicit MouseCapture(QWidget* parentWindow, QObject* parent = nullptr);

    void startCapture();

signals:
    void pointCaptured(int x, int y);
    void captureCancelled();

private:
    QWidget* m_parentWindow;
};
