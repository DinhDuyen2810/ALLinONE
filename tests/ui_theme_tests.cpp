// Kiểm thử giao diện chung (AppTheme): kiểu Fusion, stylesheet không lỗi cú pháp, con trỏ ngón tay trỏ cho thứ bấm được
// (và trả về mũi tên khi bị vô hiệu), nút ĐỔI hình vẽ khi rê chuột / nhấn (hover/pressed thật qua QTest), nút nhấn mạnh
// (variant=primary) đổi màu theo trạng thái, ô tích vẽ được dấu tích (SVG tải được). Chạy trên nền offscreen: không bao giờ
// hiện cửa sổ lên màn hình.   cmake --build build --target ui_theme_tests && build\ui_theme_tests.exe
#include "OffscreenPlatform.h"

#include "ui/AppTheme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFont>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QStyle>
#include <QStyleFactory>
#include <QTabBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QTest>
#include <QToolButton>

#include <cstdio>

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

namespace
{
QStringList g_warnings;
void captureWarnings(QtMsgType, const QMessageLogContext&, const QString& msg)
{
    g_warnings << msg;
}

QImage shot(QWidget* w)
{
    QApplication::processEvents();
    return w->grab().toImage().convertToFormat(QImage::Format_RGB32);
}

QRgb centerPixel(const QImage& img) { return img.pixel(img.width() / 2, 6); } // hàng gần mép trên: tránh chữ giữa nút

bool near(QRgb a, QRgb b, int tol = 6)
{
    return qAbs(qRed(a) - qRed(b)) <= tol && qAbs(qGreen(a) - qGreen(b)) <= tol && qAbs(qBlue(a) - qBlue(b)) <= tol;
}

void settleCursor(QWidget& w)
{
    w.show();
    QApplication::processEvents();
}
} // namespace

int main(int argc, char** argv)
{
    if (!TestPlatform::forceOffscreen(argv[0]))
        return 2;
    if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR"))
        qputenv("QT_QPA_FONTDIR", (qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts").toLocal8Bit());
    QApplication app(argc, argv);
    QApplication::setFont(QFont(QStringLiteral("Segoe UI"), 9));

    qInstallMessageHandler(captureWarnings);
    AppTheme::apply(app);

    // 1) Kiểu Fusion + stylesheet được nạp.
    // Khi có stylesheet toàn ứng dụng, style() là lớp bao QStyleSheetStyle đặt TRÊN kiểu nền Fusion do apply() chọn.
    CHECK(QString::fromLatin1(app.style()->metaObject()->className()) == QStringLiteral("QStyleSheetStyle"));
    CHECK(QStyleFactory::keys().contains(QStringLiteral("Fusion"), Qt::CaseInsensitive));
    CHECK(!app.styleSheet().isEmpty());
    CHECK(app.styleSheet() == AppTheme::styleSheet());

    // 2) Con trỏ ngón tay trỏ cho thứ bấm được; vô hiệu thì mũi tên; nhãn không bị đổi.
    QWidget host;
    host.setAttribute(Qt::WA_DontShowOnScreen, true);
    QPushButton plain(QStringLiteral("Nut"), &host);
    QToolButton tool(&host);
    QCheckBox check(QStringLiteral("Tich"), &host);
    QRadioButton radio(QStringLiteral("Radio"), &host);
    QComboBox combo(&host);
    combo.addItems({QStringLiteral("a"), QStringLiteral("b")});
    QTabBar tabs(&host);
    tabs.addTab(QStringLiteral("Tab 1"));
    QLabel label(QStringLiteral("Nhan"), &host);
    settleCursor(host);
    CHECK(plain.cursor().shape() == Qt::PointingHandCursor);
    CHECK(tool.cursor().shape() == Qt::PointingHandCursor);
    CHECK(check.cursor().shape() == Qt::PointingHandCursor);
    CHECK(radio.cursor().shape() == Qt::PointingHandCursor);
    CHECK(combo.cursor().shape() == Qt::PointingHandCursor);
    CHECK(tabs.cursor().shape() == Qt::PointingHandCursor);
    CHECK(label.cursor().shape() == Qt::ArrowCursor);
    plain.setEnabled(false);
    QApplication::processEvents();
    CHECK(plain.cursor().shape() == Qt::ArrowCursor);
    plain.setEnabled(true);
    QApplication::processEvents();
    CHECK(plain.cursor().shape() == Qt::PointingHandCursor);

    // 3) Nút thường: hình vẽ ĐỔI khi rê chuột và khi nhấn (hover/pressed thật).
    QWidget stage; // KHÔNG đặt WA_DontShowOnScreen: cần cửa sổ nền thật để nhận sự kiện chuột (nền offscreen không hiện gì ra màn hình)
    stage.resize(260, 120);
    QPushButton btn(QStringLiteral("Bam vao day"), &stage);
    btn.setGeometry(20, 20, 220, 36);
    QPushButton primary(QStringLiteral("Chinh"), &stage);
    primary.setProperty("variant", "primary");
    primary.setGeometry(20, 68, 220, 36);
    stage.show();
    QApplication::processEvents();
    QTest::mouseMove(&stage, QPoint(5, 5)); // chuột ở ngoài hai nút
    const QImage btnNormal = shot(&btn);
    QTest::mouseMove(&btn, btn.rect().center());
    const QImage btnHover = shot(&btn);
    CHECK(btnHover != btnNormal);
    CHECK(!near(centerPixel(btnHover), centerPixel(btnNormal), 2)); // nền đổi, không chỉ chữ
    QTest::mousePress(&btn, Qt::LeftButton, Qt::NoModifier, btn.rect().center());
    const QImage btnPressed = shot(&btn);
    CHECK(btnPressed != btnHover);
    QTest::mouseRelease(&btn, Qt::LeftButton, Qt::NoModifier, btn.rect().center());
    QTest::mouseMove(&stage, QPoint(5, 5));
    QApplication::processEvents();
    CHECK(shot(&btn) == btnNormal); // rời chuột thì về như cũ

    // 4) Nút nhấn mạnh: nền xanh, đậm hơn khi rê.
    QTest::mouseMove(&stage, QPoint(5, 5));
    const QImage pNormal = shot(&primary);
    QTest::mouseMove(&primary, primary.rect().center());
    const QImage pHover = shot(&primary);
    CHECK(near(centerPixel(pNormal), qRgb(0x09, 0x69, 0xda)));
    CHECK(near(centerPixel(pHover), qRgb(0x08, 0x54, 0xb0)));

    // 5) Ô tích: chưa chọn không có nền xanh; chọn rồi có hộp xanh VÀ dấu tích trắng bên trong (SVG tải được).
    QCheckBox box(QStringLiteral("Chon"));
    box.show();
    QApplication::processEvents();
    auto blueCount = [](const QImage& img) {
        int n = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < qMin(img.width(), 26); ++x)
                if (near(img.pixel(x, y), qRgb(0x09, 0x69, 0xda), 8)) ++n;
        return n;
    };
    // Điểm trắng nằm GIỮA hai điểm xanh cùng hàng = dấu tích nằm trong hộp xanh.
    auto tickCount = [](const QImage& img) {
        int n = 0;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 1; x < qMin(img.width(), 26) - 1; ++x)
            {
                const QRgb c = img.pixel(x, y);
                if (qRed(c) > 235 && qGreen(c) > 235 && qBlue(c) > 235)
                {
                    bool l = false, r = false;
                    for (int k = 1; k <= 6 && x - k >= 0; ++k) l |= near(img.pixel(x - k, y), qRgb(0x09, 0x69, 0xda), 8);
                    for (int k = 1; k <= 6 && x + k < qMin(img.width(), 26); ++k) r |= near(img.pixel(x + k, y), qRgb(0x09, 0x69, 0xda), 8);
                    if (l && r) ++n;
                }
            }
        return n;
    };
    const QImage off = shot(&box);
    box.setChecked(true);
    const QImage on = shot(&box);
    CHECK(blueCount(off) == 0);
    CHECK(blueCount(on) > 120);   // hộp xanh ~16x16
    CHECK(tickCount(on) >= 6);    // dấu tích trắng nằm trong hộp
    CHECK(tickCount(off) == 0);

    // 5b) Ô tích TRONG BẢNG (Downloader tích chọn từng dòng): cũng đổ xanh + có dấu tích khi chọn.
    QTableWidget table(2, 1);
    auto* itemOn = new QTableWidgetItem(QStringLiteral("dong 1"));
    itemOn->setFlags(itemOn->flags() | Qt::ItemIsUserCheckable);
    itemOn->setCheckState(Qt::Checked);
    auto* itemOff = new QTableWidgetItem(QStringLiteral("dong 2"));
    itemOff->setFlags(itemOff->flags() | Qt::ItemIsUserCheckable);
    itemOff->setCheckState(Qt::Unchecked);
    table.setItem(0, 0, itemOn);
    table.setItem(1, 0, itemOff);
    table.horizontalHeader()->hide();
    table.verticalHeader()->hide();
    table.resize(220, 90);
    table.show();
    const QImage tableImg = shot(&table);
    CHECK(blueCount(tableImg) > 80);  // hộp xanh của dòng đã tích
    CHECK(tickCount(tableImg) >= 6);  // dấu tích trắng trong hộp

    // Tùy chọn: UI_THEME_SAVE=<thư mục> lưu ảnh mẫu (nút các trạng thái, ô tích, hộp thoại) để xem bằng mắt.
    const QString saveDir = qEnvironmentVariable("UI_THEME_SAVE");
    if (!saveDir.isEmpty())
    {
        btn.setText(QStringLiteral("Binh thuong"));
        QTest::mouseMove(&stage, QPoint(5, 5));
        shot(&stage).save(saveDir + "/theme_buttons_normal.png");
        QTest::mouseMove(&btn, btn.rect().center());
        shot(&stage).save(saveDir + "/theme_buttons_hover.png");
        QTest::mouseMove(&primary, primary.rect().center());
        shot(&stage).save(saveDir + "/theme_buttons_primary_hover.png");
        QMessageBox box2(QMessageBox::Question, QStringLiteral("Xac nhan"), QStringLiteral("Ban co muon ngat ket noi VPN?"),
                         QMessageBox::Yes | QMessageBox::No);
        box2.setDefaultButton(QMessageBox::Yes);
        box2.show();
        QApplication::processEvents();
        box2.grab().save(saveDir + "/theme_messagebox.png");
        box2.close();
    }

    // 6) Stylesheet toàn ứng dụng không phát cảnh báo cú pháp/thuộc tính lạ.
    bool badWarning = false;
    for (const QString& w : g_warnings)
        if (w.contains(QStringLiteral("Could not parse")) || w.contains(QStringLiteral("Unknown property")) ||
            w.contains(QStringLiteral("Cannot open file")) || w.contains(QStringLiteral("Could not create pixmap")))
        {
            badWarning = true;
            std::printf("  canh bao Qt: %s\n", qPrintable(w));
        }
    CHECK(!badWarning);

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
