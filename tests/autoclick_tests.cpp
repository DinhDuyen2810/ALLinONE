// Kiểm thử Auto Click: lõi THUẦN (mô hình, tuần tự hóa JSON, đường dẫn lưu trữ) + "khói" giao diện (dựng
// cửa sổ, bấm nút Qt). TUYỆT ĐỐI KHÔNG gửi chuột/phím thật (SendInput/SetCursorPos), không cài hook
// toàn cục, không đăng ký phím tắt hệ thống - không bao giờ bấm "Chạy" trong test: mọi thứ cần chuỗi chạy
// thật phải tự kiểm tra tay. Dữ liệu ghi vào một thư mục tạm (AppPaths::setDataDirOverride), không đụng
// %LOCALAPPDATA%\OneForAll thật của máy đang chạy test.
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <climits>
#include <cstdio>

#include "core/AppPaths.h"
#include "tools/autoclick/AutoClickWindow.h"
#include "tools/autoclick/engine/InputController.h"
#include "tools/autoclick/engine/StopHotkey.h"
#include "tools/autoclick/model/ActionChain.h"
#include "tools/autoclick/storage/ActionSerializer.h"
#include "tools/autoclick/widgets/ActionEditorWidget.h"
#include "tools/autoclick/widgets/ActionListWidget.h"
#include "tools/autoclick/widgets/ChainListWidget.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

static QPushButton* buttonByText(QWidget* parent, const QString& text)
{
    for (auto* b : parent->findChildren<QPushButton*>())
        if (b->text() == text)
            return b;
    return nullptr;
}

static bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(data);
    return true;
}

static QByteArray readFile(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    QTemporaryDir dataDir;
    CHECK(dataDir.isValid());
    AppPaths::setDataDirOverride(dataDir.path()); // TRƯỚC mọi thứ đụng Logger/tệp hồ sơ

    // ---- Action::logDescription: không lộ nội dung "Gõ văn bản" ra log ----
    {
        Action typeText;
        typeText.type = ActionType::TypeText;
        typeText.text = "MatKhau!Bi-Mat";
        CHECK(typeText.description().contains("MatKhau!Bi-Mat")); // cột Mô tả trên giao diện vẫn hiện
        CHECK(!typeText.logDescription().contains("MatKhau"));
        CHECK(typeText.logDescription().contains("14")); // chỉ ghi số ký tự

        Action click;
        click.type = ActionType::MouseClick;
        CHECK(click.logDescription() == click.description()); // loại khác giữ nguyên
    }

    // ---- ActionChain::enabledActionIndices: ánh xạ "hành động thứ k đang chạy" -> hàng gốc ----
    {
        ActionChain chain;
        chain.actions.resize(5);
        chain.actions[0].enabled = false;
        chain.actions[3].enabled = false;
        const std::vector<int> idx = chain.enabledActionIndices();
        CHECK((idx == std::vector<int>{1, 2, 4}));

        ActionChain none;
        CHECK(none.enabledActionIndices().empty());
    }

    // ---- ActionSerializer: khứ hồi JSON giữ nguyên mọi trường (kể cả enabled=false, phím ngoài danh sách) ----
    {
        ActionChain chain;
        chain.id = "c1";
        chain.name = "Đăng nhập (Copy)";
        chain.repeatCount = -1;
        Action a;
        a.type = ActionType::Hotkey;
        a.enabled = false;
        a.keyCode = 49; // phím "1" - không có trong KEY_LIST của editor
        a.keyName = "1";
        a.modCtrl = true;
        a.waitBefore = std::chrono::milliseconds(123);
        a.duration = std::chrono::milliseconds(4567);
        a.text = "xin chào";
        chain.actions.push_back(a);

        std::vector<ActionChain> out;
        QString error;
        CHECK(ActionSerializer::fromJsonString(ActionSerializer::toJsonString({chain}), out, &error));
        CHECK(error.isEmpty());
        CHECK(out.size() == 1);
        if (out.size() == 1 && out[0].actions.size() == 1)
        {
            CHECK(out[0].name == "Đăng nhập (Copy)");
            CHECK(out[0].repeatCount == -1);
            const Action& b = out[0].actions[0];
            CHECK(b.type == ActionType::Hotkey);
            CHECK(!b.enabled);
            CHECK(b.keyCode == 49 && b.keyName == "1" && b.modCtrl);
            CHECK(b.waitBefore.count() == 123 && b.duration.count() == 4567);
            CHECK(b.text == "xin chào");
        }
    }

    // ---- ActionSerializer: tệp hỏng/sai định dạng phải báo LỖI, không được coi là "hồ sơ rỗng hợp lệ" ----
    {
        std::vector<ActionChain> out;
        out.push_back(ActionChain{}); // phải giữ nguyên khi nạp thất bại
        QString error;
        CHECK(!ActionSerializer::fromJsonString("{ \"version\": \"1.0\", \"chains\": [ ", out, &error)); // JSON cụt
        CHECK(!error.isEmpty());
        CHECK(out.size() == 1);

        error.clear();
        CHECK(!ActionSerializer::fromJsonString("{ \"version\": \"1.0\" }", out, &error)); // thiếu "chains"
        CHECK(!error.isEmpty());
        error.clear();
        CHECK(!ActionSerializer::fromJsonString("{ \"chains\": \"khong phai mang\" }", out, &error));
        CHECK(!error.isEmpty());
        error.clear();
        CHECK(!ActionSerializer::fromJsonString("[1, 2, 3]", out, &error)); // gốc không phải đối tượng
        CHECK(!error.isEmpty());
        CHECK(out.size() == 1);

        // Mảng "chains" rỗng là HỢP LỆ (hồ sơ chưa có chuỗi nào)
        CHECK(ActionSerializer::fromJsonString("{ \"chains\": [] }", out, &error));
        CHECK(out.empty());
    }

    // ---- ActionSerializer: thời gian ngoài miền trong tệp sửa tay/nhập từ nơi khác bị ép về [0, INT_MAX] ----
    {
        const QString json = R"({ "chains": [ { "name": "x", "actions": [
            { "type": "KeyPress", "waitBefore": -5, "waitAfter": 9223372036854775807, "duration": 1e300 },
            { "type": "KeyPress", "waitBefore": 1500, "waitAfter": "abc", "duration": -9223372036854775808 } ] } ] })";
        std::vector<ActionChain> out;
        CHECK(ActionSerializer::fromJsonString(json, out));
        CHECK(out.size() == 1 && out[0].actions.size() == 2);
        if (out.size() == 1 && out[0].actions.size() == 2)
        {
            CHECK(out[0].actions[0].waitBefore.count() == 0);         // âm -> 0
            CHECK(out[0].actions[0].waitAfter.count() == INT_MAX);    // quá lớn -> trần (không tràn mốc thời gian)
            CHECK(out[0].actions[0].duration.count() == INT_MAX);
            CHECK(out[0].actions[1].waitBefore.count() == 1500);      // giá trị thường giữ nguyên
            CHECK(out[0].actions[1].waitAfter.count() == 500);        // sai kiểu -> mặc định
            CHECK(out[0].actions[1].duration.count() == 0);
        }
    }

    // ---- Action::description: văn bản chứa "%1"/"%2" (vd chuỗi mã hóa URL) phải hiện nguyên văn ----
    {
        Action typeText;
        typeText.type = ActionType::TypeText;
        typeText.text = "a%2Fb %1";
        CHECK(typeText.description() == QString::fromUtf8("Gõ \"a%2Fb %1\" (tức thì)"));
    }

    // ---- ActionSerializer: lưu/nạp tệp, đường dẫn mặc định, không tự tạo thư mục "profiles" lạc chỗ ----
    {
        const QString defaultPath = ActionSerializer::getDefaultProfilePath();
        CHECK(QFileInfo(defaultPath).isAbsolute());
        CHECK(QDir::cleanPath(defaultPath).startsWith(QDir::cleanPath(dataDir.path()), Qt::CaseInsensitive));

        // Xuất ra một thư mục bất kỳ: ghi được, và KHÔNG tạo "profiles/" trong thư mục làm việc hiện tại
        QTemporaryDir exportDir;
        QTemporaryDir cwd;
        const QString oldCwd = QDir::currentPath();
        QDir::setCurrent(cwd.path());

        ActionChain chain;
        chain.name = "Xuat";
        chain.actions.resize(2);
        const QString exportPath = exportDir.path() + "/out.json";
        QString error;
        CHECK(ActionSerializer::saveToFile(exportPath, {chain}, &error));
        CHECK(error.isEmpty());
        CHECK(!QFileInfo::exists(cwd.path() + "/profiles"));

        std::vector<ActionChain> loaded;
        CHECK(ActionSerializer::loadFromFile(exportPath, loaded, &error));
        CHECK(loaded.size() == 1 && loaded[0].actions.size() == 2);

        // Ghi vào thư mục không tồn tại: phải báo lỗi (có lý do), không âm thầm "thành công"
        error.clear();
        CHECK(!ActionSerializer::saveToFile(exportDir.path() + "/khong/ton/tai/out.json", {chain}, &error));
        CHECK(!error.isEmpty());

        // Ghi đè tệp đã có: nội dung mới thay hoàn toàn, không còn tệp tạm sót lại bên cạnh
        chain.actions.resize(3);
        CHECK(ActionSerializer::saveToFile(exportPath, {chain}, &error));
        CHECK(ActionSerializer::loadFromFile(exportPath, loaded, &error));
        CHECK(loaded.size() == 1 && loaded[0].actions.size() == 3);
        CHECK(QDir(exportDir.path()).entryList(QDir::Files).size() == 1);

        // Nạp tệp không tồn tại: lỗi
        error.clear();
        CHECK(!ActionSerializer::loadFromFile(exportDir.path() + "/khong_co.json", loaded, &error));
        CHECK(!error.isEmpty());

        QDir::setCurrent(oldCwd);
    }

    // ---- InputController / StopHotkey: trạng thái ban đầu (không gọi hàm nào gửi chuột/phím thật) ----
    {
        InputController input;
        CHECK(!input.hasError());
        CHECK(input.lastError().isEmpty());
        input.typeText(std::string(), TextTypeMode::Instant, std::chrono::milliseconds(0)); // rỗng -> không gửi gì
        CHECK(!input.hasError());

        CHECK(!StopHotkey::label().isEmpty());
        CHECK(!StopHotkey::isStopMessage(nullptr));
    }

    // ---- Hồ sơ mặc định HỎNG: cửa sổ phải giữ lại bản gốc (.bak), không ghi đè mất ----
    {
        const QString defaultPath = ActionSerializer::getDefaultProfilePath();
        const QByteArray corrupt = "{ \"version\": \"1.0\", \"chains\": [ { \"name\": \"Chuoi quan trong\", ";
        CHECK(writeFile(defaultPath, corrupt));

        {
            AutoClickWindow win; // không show -> không có hộp thoại nào bật lên
            app.processEvents();
        }

        const QFileInfo info(defaultPath);
        const QStringList backups = info.dir().entryList({info.fileName() + ".*.bak"}, QDir::Files);
        CHECK(backups.size() == 1);
        if (backups.size() == 1)
            CHECK(readFile(info.dir().filePath(backups.first())) == corrupt); // bản gốc còn NGUYÊN

        // Tệp mặc định mới là hồ sơ mẫu hợp lệ
        std::vector<ActionChain> fresh;
        QString error;
        CHECK(ActionSerializer::loadFromFile(defaultPath, fresh, &error));
        CHECK(!fresh.empty());
    }

    // ---- "Khói" giao diện ----
    {
        AutoClickWindow win;
        win.setAttribute(Qt::WA_DontShowOnScreen, true);
        win.show();
        app.processEvents();
        CHECK(true); // dựng cửa sổ không crash

        auto* chainList = win.findChild<ChainListWidget*>();
        auto* actionList = win.findChild<ActionListWidget*>();
        auto* editor = win.findChild<ActionEditorWidget*>();
        CHECK(chainList && actionList && editor);
        if (chainList && actionList && editor)
        {
            auto* list = chainList->findChild<QListWidget*>();
            auto* table = actionList->findChild<QTableWidget*>();
            CHECK(list && table);

            // -- Thêm / nhân bản chain phải CHỌN chain mới (trước đây vẫn nằm ở chain cũ) --
            const int before = list->count();
            CHECK(before >= 1);
            CHECK(list->currentRow() == 0);
            if (auto* addBtn = buttonByText(chainList, "+ Thêm"))
            {
                addBtn->click();
                app.processEvents();
                CHECK(list->count() == before + 1);
                CHECK(list->currentRow() == before);        // chain vừa thêm được chọn
                CHECK(chainList->selectedChainIndex() == before);
                CHECK(table->rowCount() == 0);              // ...và danh sách hành động là của chain mới (rỗng)
            }
            else
            {
                CHECK(false);
            }

            list->setCurrentRow(0);
            app.processEvents();
            CHECK(table->rowCount() >= 1);
            if (auto* cloneBtn = buttonByText(chainList, "Nhân bản"))
            {
                const int beforeClone = list->count();
                const int demoRows = table->rowCount();
                cloneBtn->click();
                app.processEvents();
                CHECK(list->count() == beforeClone + 1);
                CHECK(list->currentRow() == beforeClone);   // bản sao được chọn
                CHECK(table->rowCount() == demoRows);       // và hiện đúng hành động của bản sao

                // Tên gốc cho hộp thoại Đổi tên: KHÔNG bị cắt tại " (" của hậu tố "(Copy)"
                const QString storedName = list->item(beforeClone)->data(Qt::UserRole).toString();
                CHECK(storedName.endsWith(" (Copy)"));
                CHECK(list->item(beforeClone)->text().startsWith(storedName + " ("));
            }
            else
            {
                CHECK(false);
            }

            // -- Xóa chain cuối: chọn chain đứng ngay trước nó --
            if (auto* deleteBtn = buttonByText(chainList, "Xóa"))
            {
                const int beforeDelete = list->count();
                CHECK(list->currentRow() == beforeDelete - 1);
                deleteBtn->click();
                app.processEvents();
                CHECK(list->count() == beforeDelete - 1);
                CHECK(list->currentRow() == beforeDelete - 2);
            }

            // -- Editor: "Lưu hành động" không được bật lại hành động đã tắt, không đổi phím ngoài danh sách --
            Action odd;
            odd.type = ActionType::KeyPress;
            odd.enabled = false;
            odd.keyCode = 49;
            odd.keyName = "1";
            editor->setAction(odd, 0);
            const Action back = editor->getAction();
            CHECK(!back.enabled);
            CHECK(back.keyCode == 49);
            CHECK(back.keyName == "1");

            odd.type = ActionType::Hotkey;
            editor->setAction(odd, 0);
            const Action backHotkey = editor->getAction();
            CHECK(backHotkey.keyCode == 49 && backHotkey.keyName == "1" && !backHotkey.enabled);

            // Tọa độ màn hình ảo vượt ±10000 px (nhiều màn hình độ phân giải cao) không được bị ô nhập cắt.
            Action wide;
            wide.type = ActionType::MouseDrag;
            wide.startX = 11500;
            wide.startY = -12000;
            wide.endX = 20000;
            wide.endY = 15000;
            editor->setAction(wide, 0);
            const Action backWide = editor->getAction();
            CHECK(backWide.startX == 11500 && backWide.startY == -12000 && backWide.endX == 20000 && backWide.endY == 15000);

            // Nạp một hành động phím thường: mục tạm "(phím khác: ...)" phải biến mất khỏi mọi combo
            Action normal;
            normal.type = ActionType::KeyPress;
            normal.keyCode = 9;
            normal.keyName = "TAB";
            editor->setAction(normal, 0);
            const Action backNormal = editor->getAction();
            CHECK(backNormal.enabled && backNormal.keyCode == 9 && backNormal.keyName == "TAB");
            bool customItemLeft = false;
            for (auto* combo : editor->findChildren<QComboBox*>())
                for (int i = 0; i < combo->count(); ++i)
                    if (combo->itemText(i).startsWith("(phím khác"))
                        customItemLeft = true;
            CHECK(!customItemLeft);

            // -- Đánh dấu hàng đang chạy KHÔNG được đổi hàng đang chọn / nạp lại editor --
            list->setCurrentRow(0);
            app.processEvents();
            CHECK(table->rowCount() >= 3);
            if (table->rowCount() >= 3)
            {
                actionList->setSelectedActionIndex(0);
                app.processEvents();

                // Người dùng sửa dở một trường mà chưa bấm "Lưu hành động"
                QSpinBox* waitSpin = nullptr;
                for (auto* spin : editor->findChildren<QSpinBox*>())
                    if (spin->suffix() == " ms") { waitSpin = spin; break; }
                CHECK(waitSpin != nullptr);
                if (waitSpin)
                    waitSpin->setValue(7777);
                const Action editing = editor->getAction();

                int selectionChanges = 0; // (không dùng QSignalSpy - dự án không link Qt6::Test)
                QObject::connect(actionList, &ActionListWidget::actionSelectionChanged, &win,
                                 [&selectionChanges](int) { ++selectionChanges; });
                actionList->setRunningRow(2);
                CHECK(selectionChanges == 0);
                CHECK(actionList->selectedActionIndex() == 0);
                CHECK(table->item(2, 0)->text().contains("▶"));
                CHECK(!table->item(1, 0)->text().contains("▶"));
                actionList->setRunningRow(-1);
                CHECK(!table->item(2, 0)->text().contains("▶"));

                // Tín hiệu "bắt đầu hành động" của ActionRunner (giả lập, không chạy runner thật) không
                // được đụng tới lựa chọn hay nội dung đang sửa dở trong editor.
                CHECK(QMetaObject::invokeMethod(&win, "onRunnerActionStarted", Qt::DirectConnection,
                                                Q_ARG(int, 1), Q_ARG(int, 1), Q_ARG(int, 3), Q_ARG(int, 5),
                                                Q_ARG(QString, "hien tai"), Q_ARG(QString, "ke tiep"),
                                                Q_ARG(int, 100), Q_ARG(int, 100), Q_ARG(int, 2)));
                app.processEvents();
                CHECK(selectionChanges == 0);
                CHECK(actionList->selectedActionIndex() == 0);
                const Action after = editor->getAction();
                CHECK(after.waitBefore == editing.waitBefore && after.waitAfter == editing.waitAfter &&
                      after.duration == editing.duration);
                // Không có chuỗi nào đang chạy -> không hàng nào được đánh dấu
                CHECK(!table->item(2, 0)->text().contains("▶"));
            }
        }

        win.close();
        app.processEvents();
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: chay chuoi that (SendInput/SetCursorPos), phim dung toan cuc Ctrl+Alt+F8, bat toa do/\n");
    std::printf("thao tac keo/to hop phim (hook toan cuc) KHONG duoc chay trong test tu dong - can kiem tra tay.\n");
    return g_fail == 0 ? 0 : 1;
}
