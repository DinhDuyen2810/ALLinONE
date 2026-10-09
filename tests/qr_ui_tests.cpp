// Kiểm thử giao diện QR (không hiển thị lên màn hình). Build: cmake --build build --target qr_ui_tests
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QKeyEvent>
#include <QLineEdit>
#include <QCameraDevice>
#include <QMediaDevices>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include <cstdio>

#include "core/AppPaths.h"
#include "tools/qr/QRCodec.h"
#include "tools/qr/QRGenerateTab.h"
#include "tools/qr/QRHistoryStore.h"
#include "tools/qr/QRHistoryTab.h"
#include "tools/qr/QRScanTab.h"
#include "tools/qr/QRWindow.h"
#include "tools/qr/ScreenSnipOverlay.h"

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
    // PHẢI đứng trước mọi lời gọi tới QRHistoryStore/Logger: ép toàn bộ dữ liệu vào thư mục tạm để bộ
    // test không đọc/ghi/xóa lịch sử QR + log thật của người dùng trên máy đang chạy test.
    AppPaths::setDataDirOverride(tmp.path());
    CHECK(QRHistoryStore::instance().filePath().startsWith(QDir(tmp.path()).absolutePath()));
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

        // Ô mật khẩu che ký tự khi gõ; nút 👁 bật/tắt hiển thị (không đổi nội dung mã).
        auto* pass = gen->findChild<QLineEdit*>("wifiPass");
        auto* toggle = gen->findChild<QPushButton*>("wifiPassToggle");
        CHECK(pass && toggle);
        CHECK(pass->echoMode() == QLineEdit::Password);
        toggle->click();
        CHECK(pass->echoMode() == QLineEdit::Normal);
        toggle->click();
        CHECK(pass->echoMode() == QLineEdit::Password);
        CHECK(gen->currentContent() == "WIFI:T:WPA;S:Home\\;WiFi;P:pa\\:ss\\,word;;");
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

        // Mã vừa dán là mã WiFi: mật khẩu phải bị CHE mặc định ở bảng trường + ô nội dung gốc, chỉ hiện
        // khi người dùng chủ động tick "Hiện mật khẩu". results() (dùng cho Sao chép/Tạo lại) vẫn đầy đủ.
        auto* fields = scan->findChild<QTableWidget*>("fieldTable");
        auto* raw = scan->findChild<QPlainTextEdit*>("rawText");
        auto* reveal = scan->findChild<QCheckBox*>("revealCheck");
        CHECK(fields && raw && reveal);
        if (fields && raw && reveal)
        {
            auto tableText = [fields] {
                QString all;
                for (int r = 0; r < fields->rowCount(); ++r)
                    all += fields->item(r, 0)->text() + "=" + fields->item(r, 1)->text() + "\n";
                return all;
            };
            CHECK(!reveal->isHidden() && !reveal->isChecked());
            CHECK(tableText().contains("Home;WiFi"));
            CHECK(!tableText().contains("pa:ss,word"));
            CHECK(!raw->toPlainText().contains("ss"));
            reveal->setChecked(true);
            CHECK(tableText().contains("pa:ss,word"));
            CHECK(raw->toPlainText() == gen->currentContent());
            reveal->setChecked(false);
            CHECK(!tableText().contains("pa:ss,word"));
            CHECK(scan->results().first().text.contains("pa\\:ss\\,word"));
        }
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
        // Kết quả không có trường nhạy cảm thì không hiện ô "Hiện mật khẩu".
        auto* reveal = scan->findChild<QCheckBox*>("revealCheck");
        CHECK(reveal && reveal->isHidden());
    }

    // ---- 8. "Tạo lại mã" chuyển sang tab tạo ----
    {
        win.tabs()->setCurrentWidget(scan);
        QPushButton* re = findButton(scan, "🔁 Tạo lại mã");
        CHECK(re && re->isEnabled());
        re->click();
        CHECK(win.tabs()->currentWidget() == gen);
        CHECK(!gen->currentContent().isEmpty());

        // Hồi quy: "Tạo lại mã" phải giữ NGUYÊN VĂN nội dung đã quét. Ô văn bản (QPlainTextEdit) đổi CRLF
        // thành LF và U+00A0 thành dấu cách, nên mã tạo lại từ một vCard (dùng CRLF) từng mang nội dung khác.
        const QString crlf = QString("BEGIN:VCARD\r\nFN:An") + QChar(0x00A0) + "Nguyen\r\nEND:VCARD";
        QRStyle st;
        st.targetSize = 400;
        win.tabs()->setCurrentWidget(scan);
        CHECK(scan->scanImage(QRCodec::render(QRCodec::encode(crlf, QREcc::Medium), st), "crlf") == 1);
        re->click();
        CHECK(gen->currentContent() == crlf);
        {
            const auto r = QRCodec::decode(gen->currentImage());
            CHECK(r.size() == 1 && r.first().text == crlf);
        }
        // Người dùng sửa ô văn bản thì nội dung là thứ đang hiện trong ô.
        QPlainTextEdit* edit = nullptr; // ô của trang "Văn bản" (tab Tạo còn hai QPlainTextEdit khác: Email, SMS)
        for (QPlainTextEdit* e : gen->findChildren<QPlainTextEdit*>())
            if (e->placeholderText().startsWith("Nhập văn bản"))
                edit = e;
        CHECK(edit != nullptr);
        if (edit)
        {
            edit->setPlainText("da sua");
            gen->refresh();
            CHECK(gen->currentContent() == "da sua");
        }
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

    // ---- 9b. Lịch sử: che mật khẩu WiFi mặc định, tooltip không dựng HTML từ nội dung mã ----
    {
        QRHistoryStore& store = QRHistoryStore::instance();
        const QString wifi = "WIFI:T:WPA;S:Home;P:sup3rSecret;;";
        const QString html = "<img src=\"\\\\evil\\x\"><b>dam</b> & co";
        store.add("scan", "WiFi", wifi);
        store.add("scan", "Văn bản", html);

        auto* table = win.historyTab()->findChild<QTableWidget*>();
        auto* reveal = win.historyTab()->findChild<QCheckBox*>("historyRevealCheck");
        CHECK(table && reveal && table->rowCount() == 2);
        if (table && reveal && table->rowCount() == 2)
        {
            // Mới nhất ở đầu: hàng 0 = html, hàng 1 = wifi
            CHECK(!reveal->isChecked());
            CHECK(!table->item(1, 3)->text().contains("sup3rSecret"));
            CHECK(!table->item(1, 3)->toolTip().contains("sup3rSecret"));
            CHECK(table->item(1, 3)->text().contains("S:Home"));
            CHECK(store.entries().at(1).content == wifi); // dữ liệu LƯU vẫn nguyên văn (cho "Tạo lại mã")

            // Tooltip: mọi ký tự '<' của nội dung đã bị escape - không còn thẻ <img>/<b> thật nào.
            const QString tip = table->item(0, 3)->toolTip();
            CHECK(Qt::mightBeRichText(tip));
            CHECK(!tip.contains("<img") && !tip.contains("<b>"));
            CHECK(tip.contains("&lt;img") && tip.contains("&amp;"));
            CHECK(table->item(0, 3)->text().contains("<img")); // ô bảng là văn bản thuần, hiện nguyên văn

            reveal->setChecked(true);
            CHECK(table->item(1, 3)->text().contains("sup3rSecret"));
            reveal->setChecked(false);
            CHECK(!table->item(1, 3)->text().contains("sup3rSecret"));
        }

        // "Tạo lại mã" từ lịch sử phải dùng nội dung THẬT, không phải bản đã che.
        if (table)
        {
            win.tabs()->setCurrentWidget(win.historyTab());
            table->setCurrentCell(1, 0);
            QPushButton* re = findButton(win.historyTab(), "🔁 Tạo lại mã");
            CHECK(re != nullptr);
            if (re)
                re->click();
            CHECK(gen->currentContent() == wifi);
        }
        store.clear();
    }

    // ---- 9c. Tệp lịch sử hỏng được đổi tên .bak chứ không bị ghi đè; ghi thất bại được báo ----
    {
        QRHistoryStore& store = QRHistoryStore::instance();
        const QString original = store.filePath();

        const QString corrupt = tmp.filePath("corrupt.json");
        {
            QFile f(corrupt);
            CHECK(f.open(QIODevice::WriteOnly));
            f.write("{ \"entries\": [ {\"content\": \"du lieu cu\" ");  // JSON dở dang
        }
        store.setFilePath(corrupt);
        CHECK(!store.load());
        CHECK(store.entries().isEmpty());
        CHECK(!QFile::exists(corrupt));
        CHECK(QFile::exists(corrupt + ".bak"));
        store.add("generate", "Văn bản", "muc moi");
        CHECK(store.lastSaveOk());
        {
            QFile bak(corrupt + ".bak");
            CHECK(bak.open(QIODevice::ReadOnly) && bak.readAll().contains("du lieu cu"));
        }
        CHECK(store.load() && store.entries().size() == 1);

        // Hồi quy: gặp tệp hỏng nhiều lần LIỀN NHAU (cùng một giây) - mỗi bản hỏng vẫn phải có tệp .bak riêng.
        // Trước đây lần thứ ba trùng tên "<tệp>.<giờ-phút-giây>.bak" với lần thứ hai, đổi tên thất bại và bản
        // hỏng bị lần add() kế tiếp ghi đè mất.
        for (int round = 0; round < 4; ++round)
        {
            QFile f(corrupt);
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(QByteArray("{ \"entries\": [ hong lan ") + QByteArray::number(round));
            f.close();
            CHECK(!store.load());
            CHECK(!QFile::exists(corrupt));
            store.add("generate", "Văn bản", "sau lan hong");
        }
        {
            const QStringList backups = QDir(tmp.path()).entryList({"corrupt.json*.bak"}, QDir::Files);
            CHECK(backups.size() == 5); // 1 bản ở trên + 4 bản vừa rồi, không bản nào bị mất
            QByteArray all;
            for (const QString& name : backups)
            {
                QFile b(tmp.filePath(name));
                CHECK(b.open(QIODevice::ReadOnly));
                all += b.readAll();
            }
            for (int round = 0; round < 4; ++round)
                CHECK(all.contains(QByteArray("hong lan ") + QByteArray::number(round)));
        }

        // Hồi quy: tệp có NHIỀU hơn 300 mục (sửa tay/bản khác ghi) từng được nạp nguyên vẹn - giới hạn 300 chỉ
        // áp ở add(). Nay load() cũng chỉ giữ 300 mục mới nhất (đứng đầu tệp).
        {
            QByteArray big = "{ \"entries\": [";
            for (int i = 0; i < 1000; ++i)
                big += (i ? "," : "") + QByteArray("{\"source\":\"scan\",\"type\":\"t\",\"content\":\"muc ") + QByteArray::number(i) + "\"}";
            big += "] }";
            QFile f(corrupt);
            CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write(big);
            f.close();
            CHECK(store.load());
            CHECK(store.entries().size() == 300);
            CHECK(store.entries().first().content == "muc 0" && store.entries().last().content == "muc 299");
        }

        // Tệp chưa tồn tại = lịch sử rỗng, không phải lỗi, không sinh .bak.
        store.setFilePath(tmp.filePath("missing.json"));
        CHECK(store.load() && store.entries().isEmpty());
        CHECK(!QFile::exists(tmp.filePath("missing.json.bak")));

        // Đường dẫn không ghi được (thư mục cha là một TỆP): add() phải ghi nhận thất bại + phát saveFailed.
        const QString blocker = tmp.filePath("blocker");
        {
            QFile f(blocker);
            CHECK(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        int failedSignals = 0;
        const auto conn = QObject::connect(&store, &QRHistoryStore::saveFailed, [&failedSignals](const QString&) { ++failedSignals; });
        store.setFilePath(blocker + "/sub/history.json");
        store.add("generate", "Văn bản", "khong ghi duoc");
        CHECK(!store.lastSaveOk());
        CHECK(failedSignals == 1);
        QObject::disconnect(conn);

        store.setFilePath(original);
        store.load();
        store.clear();
        CHECK(store.lastSaveOk());
    }

    // ---- 9d. Lớp phủ chụp màn hình: đóng kiểu nào cũng báo cho nơi gọi đúng MỘT lần ----
    // (không show() lớp phủ lên màn hình; closeEvent vẫn được gửi cho cửa sổ chưa hiện)
    {
        // Hồi quy: đóng không qua chuột/ESC (Alt+F4...) từng không phát tín hiệu nào -> cửa sổ QR Tools
        // (đã bị ẩn trước khi chụp) không bao giờ hiện lại.
        int cancelled = 0, captured = 0;
        auto* overlay = new ScreenSnipOverlay();
        QObject::connect(overlay, &ScreenSnipOverlay::cancelled, [&cancelled] { ++cancelled; });
        QObject::connect(overlay, &ScreenSnipOverlay::captured, [&captured](const QImage&) { ++captured; });
        overlay->close();
        CHECK(cancelled == 1 && captured == 0);

        // ESC: đã phát cancelled() rồi thì closeEvent() không phát thêm lần nữa.
        cancelled = 0;
        auto* overlay2 = new ScreenSnipOverlay();
        QObject::connect(overlay2, &ScreenSnipOverlay::cancelled, [&cancelled] { ++cancelled; });
        QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(overlay2, &esc);
        CHECK(cancelled == 1);
        app.processEvents(); // cho WA_DeleteOnClose dọn hai lớp phủ
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
