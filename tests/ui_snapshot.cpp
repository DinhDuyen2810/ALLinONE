// KIỂM TRA TAY giao diện: dựng từng cửa sổ công cụ trên nền "offscreen" (KHÔNG hiện lên màn hình), duyệt mọi
// tab và chụp ảnh PNG ở cỡ mặc định và cỡ hẹp - để rà bố cục (chữ bị cắt, nút tràn hàng...) bằng mắt.
// Dữ liệu người dùng: thư mục TẠM (TestDataDir); có chép danh sách máy chủ VPN Gate đã tích lũy (nếu có) để
// bảng có dữ liệu thật. cmake --build build --target ui_snapshot && build\ui_snapshot.exe <thư mục ảnh>

#include "OffscreenPlatform.h"
#include "TestDataDir.h"

#include "core/AppPaths.h"
#include "ui/AppTheme.h"
#include "tools/android/AndroidControlWindow.h"
#include "tools/connect/ConnectWindow.h"
#include "tools/diskcleanup/DiskCleanupWindow.h"
#include "tools/downloader/DownloaderWindow.h"
#include "tools/qr/QRWindow.h"
#include "tools/security/SecurityGatewayWindow.h"
#include "tools/vpn/AddVpnProfileDialog.h"
#include "tools/vpn/VpnControlWindow.h"
#include "tools/wifi/WifiWindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFont>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>

#include <cstdio>
#include <functional>

namespace
{
void settle(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

QString slug(QString s)
{
    s = s.trimmed();
    QString out;
    for (const QChar c : s)
        out += (c.isLetterOrNumber() && c.unicode() < 128) ? c.toLower() : QChar('_');
    while (out.contains("__"))
        out.replace("__", "_");
    return out.left(40);
}

void snap(QWidget* w, const QString& tool, const QString& dir)
{
    w->setAttribute(Qt::WA_DontShowOnScreen, true); // không bao giờ hiện lên màn hình (kể cả ở nền Windows thật)
    w->show();
    settle(1500);
    const QSize def = w->size();
    QList<QSize> sizes{def, QSize(820, 640)};
    auto* tabs = w->findChild<QTabWidget*>();
    const int count = tabs ? tabs->count() : 1;
    for (int t = 0; t < count; ++t)
    {
        if (tabs)
            tabs->setCurrentIndex(t);
        const QString tabName = tabs ? slug(tabs->tabText(t)) : QStringLiteral("main");
        for (const QSize& s : sizes)
        {
            w->resize(s);
            settle(400);
            const QString file = QString("%1/%2_%3_%4x%5.png").arg(dir, tool).arg(t).arg(s.width()).arg(s.height());
            w->grab().save(file);
            std::printf("%s  (%s)\n", qPrintable(file), qPrintable(tabName));
        }
        w->resize(def);
    }
    w->hide();
}
} // namespace

int main(int argc, char** argv)
{
    // UI_SNAPSHOT_NATIVE=1: dùng nền Windows THẬT (vẽ đúng ô tích/thanh trượt gốc như ứng dụng) nhưng cửa sổ có
    // WA_DontShowOnScreen - được dựng và vẽ mà KHÔNG hiện lên màn hình người dùng. Mặc định: offscreen.
    const bool native = !qEnvironmentVariableIsEmpty("UI_SNAPSHOT_NATIVE");
    if (!native && !TestPlatform::forceOffscreen(argv[0]))
        return 2;
    // Nền offscreen không tự nạp font hệ thống (chữ ra ô vuông) - chỉ cho nó thư mục font của Windows.
    if (!native && qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR", (qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts").toLocal8Bit());
    QApplication app(argc, argv);
    QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));
    // Dùng ĐÚNG giao diện của ứng dụng thật: theme chung (Fusion + bảng màu sáng + stylesheet toàn ứng dụng).
    // UI_SNAPSHOT_OLD=1: chụp kiểu CŨ (kiểu gốc Windows, không theme) để so sánh trước/sau.
    if (qEnvironmentVariableIsEmpty("UI_SNAPSHOT_OLD"))
        AppTheme::apply(app);
    else if (argc > 3)
        QApplication::setStyle(QString::fromLocal8Bit(argv[3]));
    else if (!native)
        QApplication::setStyle(QStringLiteral("fusion")); // offscreen không vẽ được kiểu gốc Windows
    std::printf("style: %s\n", qPrintable(QApplication::style()->name()));
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QDir::currentPath() + "/ui_snap";
    QDir().mkpath(outDir);

    const QString realCache = QDir::fromNativeSeparators(qEnvironmentVariable("LOCALAPPDATA")) + "/OneForAll/profiles/vpngate_servers.json";
    TestDataDir dataDir;
    AppPaths::setDataDirOverride(dataDir.path());
    if (QFile::exists(realCache))
        QFile::copy(realCache, AppPaths::profileFile("vpngate_servers.json"));

    const QStringList only = argc > 2 ? QString::fromLocal8Bit(argv[2]).split(',') : QStringList();
    auto want = [&](const QString& n) { return only.isEmpty() || only.contains(n); };

    struct Item { QString name; std::function<QWidget*()> make; };
    const QList<Item> items{
        {"vpn", [] { return new VpnControlWindow(); }},
        {"wifi", [] { return new WifiWindow(); }},
        {"security", [] { return new SecurityGatewayWindow(); }},
        {"android", [] { return new AndroidControlWindow(); }},
        {"downloader", [] { return new DownloaderWindow(); }},
        {"diskcleanup", [] { return new DiskCleanupWindow(); }},
        {"qr", [] { return new QRWindow(); }},
        {"connect", [] { return new ConnectWindow(); }},
    };
    // Hộp thoại "Thêm hồ sơ VPN" ở cả 3 chế độ (Thường / GlobalProtect / FortiClient; FortiClient cả SSL-VPN lẫn IPsec).
    if (want("vpn_dialog"))
    {
        AddVpnProfileDialog dlg;
        dlg.setAttribute(Qt::WA_DontShowOnScreen, true);
        dlg.show();
        settle(300);
        auto* mode = dlg.findChild<QComboBox*>("modeCombo");
        auto* forti = dlg.findChild<QComboBox*>("fortiTypeCombo");
        for (int m = 0; mode && m < mode->count(); ++m)
        {
            mode->setCurrentIndex(m);
            settle(300);
            const QString file = QString("%1/vpn_dialog_mode%2.png").arg(outDir).arg(m);
            dlg.grab().save(file);
            std::printf("%s  (hop thoai che do %d)\n", qPrintable(file), m);
        }
        if (mode && forti)
        {
            mode->setCurrentIndex(2);
            forti->setCurrentIndex(1);
            settle(300);
            const QString file = QString("%1/vpn_dialog_mode2_ipsec.png").arg(outDir);
            dlg.grab().save(file);
            std::printf("%s  (hop thoai FortiClient IPsec)\n", qPrintable(file));
        }
        dlg.close();
    }
    for (const Item& it : items)
    {
        if (!want(it.name))
            continue;
        QWidget* w = it.make();
        snap(w, it.name, outDir);
        w->close();
        delete w;
        settle(200);
    }
    return 0;
}
