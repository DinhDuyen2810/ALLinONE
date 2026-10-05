// Kiểm thử giao diện QR (không hiển thị lên màn hình). Build: cmake --build build --target qr_ui_tests
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QCameraDevice>
#include <QMediaDevices>
#include <QPainter>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTemporaryDir>
#include <cstdio>

#include "tools/qr/QRCodec.h"
#include "tools/qr/QRGenerateTab.h"
#include "tools/qr/QRHistoryStore.h"
#include "tools/qr/QRHistoryTab.h"
#include "tools/qr/QRScanTab.h"
#include "tools/qr/QRWindow.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                                \
    do {                                                                           \
        if (cond) { ++g_pass; }                                                    \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); }     \
    } while (0)

static QPushButton* findButton(QWidget* root, const QString& text)
{
    for (QPushButton* b : root->findChildren<QPushButton*>())
        if (b->text() == text)
            return b;
    return nullptr;
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QTemporaryDir tmp;
    CHECK(tmp.isValid());
    QRHistoryStore::instance().setFilePath(tmp.filePath("history.json"));
    QRHistoryStore::instance().clear();

    QRWindow win;
    win.setAttribute(Qt::WA_DontShowOnScreen, true);
    win.show();
    app.processEvents();

    QRGenerateTab* gen = win.generateTab();
    QRScanTab* scan = win.scanTab();

    // ---- 0. Trạng thái ban đầu: chưa có nội dung ----
    CHECK(gen->currentContent().isEmpty());
    CHECK(gen->currentImage().isNull());
    CHECK(win.tabs()->count() == 3);

    // ---- 1. Văn bản tiếng Việt ----
    gen->setRawText(QString::fromUtf8("Xin chào QR – Việt Nam"));
    CHECK(gen->currentContent() == QString::fromUtf8("Xin chào QR – Việt Nam"));
    CHECK(!gen->currentImage().isNull());
    {
        const auto r = QRCodec::decode(gen->currentImage());
        CHECK(r.size() == 1 && r.first().text == gen->currentContent());
    }

    // ---- 2. Đổi kích thước và mức sửa lỗi ----
    {
        auto* size = gen->findChild<QSpinBox*>("sizeSpin");
        auto* ecc = gen->findChild<QComboBox*>("eccCombo");
        CHECK(size && ecc);
        size->setValue(300);
        ecc->setCurrentIndex(3); // H
        gen->refresh();
        CHECK(gen->currentImage().width() <= 300 && gen->currentImage().width() >= 250);
        CHECK(gen->currentMatrix().ecc == QREcc::High);
        size->setValue(512);
        ecc->setCurrentIndex(1);
    }

    // ---- 3. URL tự thêm https:// ----
    {
        auto* type = gen->findChild<QComboBox*>("typeCombo");
        type->setCurrentIndex(1);
        gen->findChild<QLineEdit*>("urlEdit")->setText("example.com/abc");
        gen->refresh();
        CHECK(gen->currentContent() == "https://example.com/abc");
    }

    // ---- 4. WiFi ----
    {
        auto* type = gen->findChild<QComboBox*>("typeCombo");
        type->setCurrentIndex(2);
        gen->refresh();
        CHECK(gen->currentContent().isEmpty()); // thiếu SSID
        gen->findChild<QLineEdit*>("wifiSsid")->setText("Home;WiFi");
        gen->refresh();
        CHECK(gen->currentContent().isEmpty()); // thiếu mật khẩu
        gen->findChild<QLineEdit*>("wifiPass")->setText("pa:ss,word");
        gen->refresh();
        CHECK(gen->currentContent() == "WIFI:T:WPA;S:Home\\;WiFi;P:pa\\:ss\\,word;;");
        const auto r = QRCodec::decode(gen->currentImage());
        CHECK(r.size() == 1 && r.first().text == gen->currentContent());
    }

    // ---- 5. Lưu file các định dạng ----
    {
        QString err;
        const QString png = tmp.filePath("a.png");
        const QString jpg = tmp.filePath("a.jpg");
        const QString bmp = tmp.filePath("a.bmp");
        const QString svg = tmp.filePath("a.svg");
        const QString noext = tmp.filePath("noext");
        CHECK(gen->saveTo(png, &err));
        CHECK(gen->saveTo(jpg, &err));
        CHECK(gen->saveTo(bmp, &err));
        CHECK(gen->saveTo(svg, &err));
        CHECK(gen->saveTo(noext, &err));
        CHECK(QFile::exists(noext + ".png"));
        CHECK(QRCodec::decode(QImage(png)).size() == 1);
        CHECK(QRCodec::decode(QImage(jpg)).size() == 1);
        CHECK(QRCodec::decode(QImage(bmp)).size() == 1);
        QFile f(svg);
        CHECK(f.open(QIODevice::ReadOnly) && f.readAll().contains("<svg"));
        CHECK(!gen->saveTo(tmp.filePath("no_such_dir/x.png"), &err) && !err.isEmpty());
    }

    // ---- 6. Sao chép -> dán vào tab quét ----
    {
        CHECK(gen->copyToClipboard());
        QGuiApplication::clipboard()->clear();
        CHECK(gen->copyToClipboard());
        scan->pasteFromClipboard();
        CHECK(scan->results().size() == 1);
        CHECK(scan->results().first().text == gen->currentContent());
    }

    // ---- 7. Quét ảnh không có mã / nhiều mã ----
    {
        QImage blank(300, 300, QImage::Format_RGB32);
        blank.fill(Qt::white);
        CHECK(scan->scanImage(blank, "blank") == 0);
        CHECK(scan->statusText().contains("Không tìm thấy"));
        CHECK(scan->scanImage(QImage(), "null") == 0);

        QRStyle st;
        st.targetSize = 280;
        QImage canvas(660, 320, QImage::Format_RGB32);
        canvas.fill(Qt::white);
        QPainter p(&canvas);
        p.drawImage(10, 20, QRCodec::render(QRCodec::encode("AAA", QREcc::Medium), st));
        p.drawImage(350, 20, QRCodec::render(QRCodec::encode("https://example.com", QREcc::Medium), st));
        p.end();
        CHECK(scan->scanImage(canvas, "two") == 2);
    }

    // ---- 8. "Tạo lại mã" chuyển sang tab tạo ----
    {
        win.tabs()->setCurrentWidget(scan);
        QPushButton* re = findButton(scan, "🔁 Tạo lại mã");
        CHECK(re && re->isEnabled());
        re->click();
        CHECK(win.tabs()->currentWidget() == gen);
        CHECK(!gen->currentContent().isEmpty());
    }

    // ---- 9. Lịch sử ----
    {
        const auto& entries = QRHistoryStore::instance().entries();
        CHECK(entries.size() >= 4);
        CHECK(win.historyTab()->rowCount() == entries.size());
        const int before = entries.size();
        QRHistoryStore::instance().removeAt(0);
        CHECK(QRHistoryStore::instance().entries().size() == before - 1);
        CHECK(win.historyTab()->rowCount() == before - 1);

        // Lưu và nạp lại từ file
        const QString content = QRHistoryStore::instance().entries().first().content;
        QRHistoryStore::instance().load();
        CHECK(QRHistoryStore::instance().entries().size() == before - 1);
        CHECK(QRHistoryStore::instance().entries().first().content == content);

        QRHistoryStore::instance().clear();
        CHECK(win.historyTab()->rowCount() == 0);
    }

    // ---- 10. Camera: trạng thái nút khớp với thiết bị ----
    {
        const bool hasCam = !QMediaDevices::videoInputs().isEmpty();
        QPushButton* cam = findButton(scan, "📷 Camera");
        CHECK(cam != nullptr);
        CHECK(cam->isEnabled() == hasCam);
        std::printf("camera devices: %lld\n", static_cast<long long>(QMediaDevices::videoInputs().size()));
        scan->stopCamera();
    }

    // Chụp ảnh giao diện từng tab để kiểm tra bằng mắt (đặt QR_UI_SHOTS=thư_mục)
    if (qEnvironmentVariableIsSet("QR_UI_SHOTS"))
    {
        const QString dir = qEnvironmentVariable("QR_UI_SHOTS");
        gen->setRawText("https://example.com/qr-demo");
        QRHistoryStore::instance().add("generate", "URL", "https://example.com/qr-demo");
        QRHistoryStore::instance().add("scan", "WiFi", "WIFI:T:WPA;S:Home;P:12345678;;");
        scan->scanImage(gen->currentImage(), "demo");
        const char* names[] = {"gen", "scan", "hist"};
        for (int i = 0; i < 3; ++i)
        {
            win.tabs()->setCurrentIndex(i);
            app.processEvents();
            win.grab().save(dir + "/" + names[i] + ".png");
        }
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
