// Kiểm thử Auto Click: lõi THUẦN (mô hình, tuần tự hóa JSON, đường dẫn lưu trữ) + "khói" giao diện (dựng
// cửa sổ, bấm nút Qt). TUYỆT ĐỐI KHÔNG gửi chuột/phím thật (SendInput/SetCursorPos), không cài hook
// toàn cục, không đăng ký phím tắt hệ thống - không bao giờ bấm "Chạy" trong test: mọi thứ cần chuỗi chạy
// thật phải tự kiểm tra tay. Dữ liệu ghi vào một thư mục tạm (AppPaths::setDataDirOverride), không đụng
// %LOCALAPPDATA%\OneForAll thật của máy đang chạy test.
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include "TestDataDir.h"
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
#include "ui/widgets/DurationInput.h"
#include "ui/widgets/ItemHoverCursor.h"

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

    TestDataDir dataDir;
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

    // ---- Action::typeName/description cho RecordedMacro ("Ghi nhớ chuỗi") - KHÔNG lộ keyCode/tọa độ cụ
    // thể (có thể là mật khẩu gõ lúc ghi), chỉ số bước + tổng thời gian ----
    {
        Action macro;
        macro.type = ActionType::RecordedMacro;
        CHECK(macro.typeName() == "Ghi nhớ chuỗi");
        CHECK(macro.description().contains("0 bước")); // chưa ghi gì

        RecordedEvent keyEv;
        keyEv.type = RecordedEventType::KeyDown;
        keyEv.keyCode = 65; // 'A' - giả lập phím bí mật gõ lúc ghi
        keyEv.delayMs = 1500;
        RecordedEvent moveEv;
        moveEv.type = RecordedEventType::MouseMove;
        moveEv.x = 777;
        moveEv.y = 888;
        moveEv.delayMs = 2500;
        macro.recordedEvents = {keyEv, moveEv};

        const QString desc = macro.description();
        CHECK(desc.contains("2 bước"));
        CHECK(desc.contains("4.0")); // tổng delayMs = 4000ms = 4.0 giây (QString::arg 'f' dùng dấu chấm)
        CHECK(!desc.contains("65"));
        CHECK(!desc.contains("777"));
        CHECK(!desc.contains("888"));
        CHECK(macro.logDescription() == macro.description()); // không có gì nhạy cảm hơn để che thêm
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

    // ---- ActionSerializer: khứ hồi JSON cho RecordedMacro ("Ghi nhớ chuỗi") - đủ cả 6 loại sự kiện, đúng
    // thứ tự + giá trị; hồ sơ CŨ không có "recordedEvents" vẫn đọc được (vector rỗng, không crash) ----
    {
        ActionChain chain;
        chain.id = "macro1";
        Action macro;
        macro.type = ActionType::RecordedMacro;

        RecordedEvent move; move.type = RecordedEventType::MouseMove; move.delayMs = 20; move.x = 111; move.y = 222;
        RecordedEvent down; down.type = RecordedEventType::MouseDown; down.delayMs = 30; down.x = 111; down.y = 222; down.button = MouseButtonType::Right;
        RecordedEvent up;   up.type = RecordedEventType::MouseUp;     up.delayMs = 40;  up.x = 333; up.y = 444; up.button = MouseButtonType::Right;
        RecordedEvent wheel; wheel.type = RecordedEventType::MouseWheel; wheel.delayMs = 50; wheel.x = 10; wheel.y = 20; wheel.wheelDelta = -240; wheel.wheelHorizontal = true;
        RecordedEvent keyDown; keyDown.type = RecordedEventType::KeyDown; keyDown.delayMs = 60; keyDown.keyCode = 65;
        RecordedEvent keyUp;   keyUp.type = RecordedEventType::KeyUp;     keyUp.delayMs = 70;  keyUp.keyCode = 65;
        macro.recordedEvents = {move, down, up, wheel, keyDown, keyUp};
        chain.actions.push_back(macro);

        std::vector<ActionChain> out;
        QString error;
        CHECK(ActionSerializer::fromJsonString(ActionSerializer::toJsonString({chain}), out, &error));
        CHECK(error.isEmpty());
        if (out.size() == 1 && out[0].actions.size() == 1)
        {
            const std::vector<RecordedEvent>& evs = out[0].actions[0].recordedEvents;
            CHECK(evs.size() == 6);
            if (evs.size() == 6)
            {
                CHECK(evs[0].type == RecordedEventType::MouseMove && evs[0].delayMs == 20 && evs[0].x == 111 && evs[0].y == 222);
                CHECK(evs[1].type == RecordedEventType::MouseDown && evs[1].delayMs == 30 && evs[1].button == MouseButtonType::Right);
                CHECK(evs[2].type == RecordedEventType::MouseUp && evs[2].delayMs == 40 && evs[2].x == 333 && evs[2].y == 444 && evs[2].button == MouseButtonType::Right);
                CHECK(evs[3].type == RecordedEventType::MouseWheel && evs[3].delayMs == 50 && evs[3].wheelDelta == -240 && evs[3].wheelHorizontal);
                CHECK(evs[4].type == RecordedEventType::KeyDown && evs[4].delayMs == 60 && evs[4].keyCode == 65);
                CHECK(evs[5].type == RecordedEventType::KeyUp && evs[5].delayMs == 70 && evs[5].keyCode == 65);
            }
        }

        // Hồ sơ cũ viết tay (trước khi có "Ghi nhớ chuỗi") - không có key "recordedEvents" ở cấp Action.
        const QString oldJson = QStringLiteral(
            "{\"chains\":[{\"id\":\"old\",\"name\":\"x\",\"repeatCount\":1,\"actions\":["
            "{\"type\":\"MouseClick\",\"enabled\":true,\"x\":1,\"y\":2}]}]}");
        std::vector<ActionChain> oldOut;
        QString oldErr;
        CHECK(ActionSerializer::fromJsonString(oldJson, oldOut, &oldErr));
        if (oldOut.size() == 1 && oldOut[0].actions.size() == 1)
            CHECK(oldOut[0].actions[0].recordedEvents.empty());

        // Phần tử hỏng (type lạ) trong mảng bị BỎ QUA, không làm hỏng cả hồ sơ; delayMs quá lớn bị kẹp
        // về trần [0, 60000] - không được tạo ra một bước chờ cả ngày lúc phát lại.
        const QString mixedJson = QStringLiteral(
            "{\"chains\":[{\"id\":\"mixed\",\"name\":\"x\",\"repeatCount\":1,\"actions\":["
            "{\"type\":\"RecordedMacro\",\"enabled\":true,\"recordedEvents\":["
            "{\"type\":\"KhongHopLe\",\"delayMs\":10},"
            "{\"type\":\"KeyDown\",\"delayMs\":999999999,\"keyCode\":13}"
            "]}]}]}");
        std::vector<ActionChain> mixedOut;
        QString mixedErr;
        CHECK(ActionSerializer::fromJsonString(mixedJson, mixedOut, &mixedErr));
        if (mixedOut.size() == 1 && mixedOut[0].actions.size() == 1)
        {
            const std::vector<RecordedEvent>& evs = mixedOut[0].actions[0].recordedEvents;
            CHECK(evs.size() == 1); // phần tử "KhongHopLe" bị loại
            if (evs.size() == 1)
            {
                CHECK(evs[0].type == RecordedEventType::KeyDown && evs[0].keyCode == 13);
                CHECK(evs[0].delayMs == 60000);
            }
        }
    }

    // ---- ActionChain::pinned: round-trip clone()/JSON, hồ sơ CŨ thiếu field vẫn ra false ----
    {
        ActionChain chain;
        chain.id = "c2";
        chain.name = "Demo";
        chain.pinned = true;

        const ActionChain copy = chain.clone();
        CHECK(copy.pinned); // clone() copy từng field thủ công - dễ sót field mới

        std::vector<ActionChain> out;
        QString error;
        CHECK(ActionSerializer::fromJsonString(ActionSerializer::toJsonString({chain}), out, &error));
        CHECK(out.size() == 1 && out[0].pinned);

        // Hồ sơ cũ (trước v1.19.8) không có key "pinned" - mô phỏng bằng JSON viết tay, không qua
        // toJsonString() (vốn LUÔN ghi field mới nên không mô phỏng được hồ sơ cũ thật).
        const QByteArray oldJson = R"({"version":"1.0","chains":[{"id":"c3","name":"Cu","actions":[]}]})";
        std::vector<ActionChain> outOld;
        CHECK(ActionSerializer::fromJsonString(QString::fromUtf8(oldJson), outOld, &error));
        CHECK(outOld.size() == 1 && !outOld[0].pinned);
    }

    // ---- Action::effectiveModOrder/setModifiers/description: THỨ TỰ thật, không cố định Ctrl-Alt-Shift-Win ----
    {
        Action a;
        a.type = ActionType::Hotkey;
        a.keyCode = 46; // Delete
        a.keyName = "DELETE";
        a.setModifiers({ModifierKey::Shift, ModifierKey::Ctrl}); // CỐ Ý ngược thứ tự "chuẩn"
        CHECK((a.effectiveModOrder() == std::vector<ModifierKey>{ModifierKey::Shift, ModifierKey::Ctrl}));
        CHECK(a.modShift && a.modCtrl && !a.modAlt && !a.modWin); // setModifiers vẫn đồng bộ 4 bool cũ
        CHECK(a.description() == "Tổ hợp phím Shift + Ctrl + DELETE"); // ĐÚNG thứ tự đã gán, không phải Ctrl trước
        CHECK(a.typeName() == "Tổ hợp phím"); // đổi tên hiển thị từ "Phím tắt" (v1.19.7)

        // Action dựng tay CHỈ gán 4 bool (như demo chain/code cũ, không gọi setModifiers) - modOrder rỗng,
        // effectiveModOrder() phải TỰ SUY LUẬN đúng thứ tự cố định Ctrl→Alt→Shift→Win như hành vi trước đây.
        Action legacy;
        legacy.type = ActionType::Hotkey;
        legacy.modWin = true;
        legacy.modCtrl = true;
        legacy.keyName = "S";
        CHECK((legacy.effectiveModOrder() == std::vector<ModifierKey>{ModifierKey::Ctrl, ModifierKey::Win}));

        // Round-trip JSON: modOrder ghi ra PHẢI đọc lại đúng thứ tự Shift,Ctrl (không bị ActionSerializer
        // tự sắp xếp lại) - kiểm tra riêng với round-trip đơn giản ở khối trên dùng modCtrl đơn lẻ.
        ActionChain orderChain;
        orderChain.id = "order";
        orderChain.actions.push_back(a);
        std::vector<ActionChain> orderOut;
        QString orderErr;
        CHECK(ActionSerializer::fromJsonString(ActionSerializer::toJsonString({orderChain}), orderOut, &orderErr));
        if (orderOut.size() == 1 && orderOut[0].actions.size() == 1)
            CHECK((orderOut[0].actions[0].effectiveModOrder() == std::vector<ModifierKey>{ModifierKey::Shift, ModifierKey::Ctrl}));

        // Hồ sơ CŨ (trước v1.19.7) không có "modOrder" trong JSON - fromJsonString() vẫn phải đọc được,
        // effectiveModOrder() suy đúng thứ tự cố định từ 4 bool cũ.
        const QString legacyJson = QStringLiteral(
            "{\"chains\":[{\"id\":\"legacy\",\"name\":\"x\",\"repeatCount\":1,\"actions\":["
            "{\"type\":\"Hotkey\",\"enabled\":true,\"keyCode\":83,\"keyName\":\"S\","
            "\"modCtrl\":true,\"modAlt\":false,\"modShift\":true,\"modWin\":false}]}]}");
        std::vector<ActionChain> legacyOut;
        QString legacyErr;
        CHECK(ActionSerializer::fromJsonString(legacyJson, legacyOut, &legacyErr));
        if (legacyOut.size() == 1 && legacyOut[0].actions.size() == 1)
            CHECK((legacyOut[0].actions[0].effectiveModOrder() == std::vector<ModifierKey>{ModifierKey::Ctrl, ModifierKey::Shift}));
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

    // ---- DurationInput: đổi đơn vị KHÔNG quy đổi số đang hiện (yêu cầu người dùng, v1.19.8) ----
    {
        DurationInput input;
        input.setValueMs(500);
        CHECK(input.valueMs() == 500);
        auto* numberSpin = input.findChild<QDoubleSpinBox*>();
        auto* unitCombo = input.findChild<QComboBox*>();
        CHECK(numberSpin != nullptr && unitCombo != nullptr);
        if (numberSpin && unitCombo)
        {
            CHECK(numberSpin->value() == 500.0); // nạp từ code: đơn vị ms, số = đúng mili-giây

            unitCombo->setCurrentIndex(1); // "giây"
            app.processEvents();
            // Số hiện KHÔNG được đổi thành "0.5" - vẫn là "500", giờ nghĩa là 500 giây.
            CHECK(numberSpin->value() == 500.0);
            CHECK(input.valueMs() == 500000);

            unitCombo->setCurrentIndex(3); // "giờ" - 500 giờ vượt xa trần mặc định 3.600.000ms (1 giờ)
            app.processEvents();
            CHECK(numberSpin->value() == 500.0); // số hiện vẫn không đổi
            CHECK(input.valueMs() == 3600000);   // giá trị THẬT (emit/lưu) bị kẹp đúng trần
        }
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

            // -- Mọi QComboBox trong editor phải có stylesheet bắt đầu bằng selector "QComboBox" - một
            // khối khai báo TRẦN (không selector, vd "background-color: ...;") nối với các rule có
            // selector khác (ComboBoxStyle::noArrow()) khiến Qt không áp dụng đúng các rule phía sau (ẩn
            // mũi tên không có tác dụng dù CSS hợp lệ riêng từng phần) - xác nhận thật bằng ảnh chụp,
            // v1.19.10. Cũng xác nhận cursor ngón tay đã được gán (ComboBoxStyle::applyCursor()).
            int badComboStyle = 0, badComboCursor = 0;
            for (auto* combo : editor->findChildren<QComboBox*>())
            {
                const QString sheet = combo->styleSheet().trimmed();
                if (!sheet.isEmpty() && !sheet.startsWith("QComboBox"))
                    ++badComboStyle;
                if (combo->cursor().shape() != Qt::PointingHandCursor)
                    ++badComboCursor;
            }
            CHECK(badComboStyle == 0);
            CHECK(badComboCursor == 0);

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
            {
                // Nhân bản/Xóa không còn nút riêng (v1.19.8 - đã chuyển hẳn vào menu chuột phải) - gọi
                // thẳng signal như menu chuột phải tự làm.
                const int beforeClone = list->count();
                const int demoRows = table->rowCount();
                emit chainList->cloneChainClicked(list->currentRow());
                app.processEvents();
                CHECK(list->count() == beforeClone + 1);
                CHECK(list->currentRow() == beforeClone);   // bản sao được chọn
                CHECK(table->rowCount() == demoRows);       // và hiện đúng hành động của bản sao

                // Tên gốc cho hộp thoại Đổi tên: KHÔNG bị cắt tại " (" của hậu tố "(Copy)"
                const QString storedName = list->item(beforeClone)->data(Qt::UserRole).toString();
                CHECK(storedName.endsWith(" (Copy)"));
                CHECK(list->item(beforeClone)->text().startsWith(storedName + " ("));
            }

            // -- Xóa chain cuối: chọn chain đứng ngay trước nó --
            {
                const int beforeDelete = list->count();
                CHECK(list->currentRow() == beforeDelete - 1);
                emit chainList->deleteChainClicked(list->currentRow());
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

            // -- Editor: RecordedMacro ("Ghi nhớ chuỗi") setAction()/getAction() khứ hồi đúng dãy sự kiện -
            // không cần ghi macro THẬT (hook toàn cục), chỉ kiểm đường dây UI<->model qua onMacroRecorded() --
            Action macroAction;
            macroAction.type = ActionType::RecordedMacro;
            editor->setAction(macroAction, 0); // chưa ghi gì -> m_recordedEvents rỗng
            CHECK(editor->getAction().recordedEvents.empty());

            RecordedEvent recMove; recMove.type = RecordedEventType::MouseMove; recMove.delayMs = 15; recMove.x = 50; recMove.y = 60;
            RecordedEvent recKey;  recKey.type = RecordedEventType::KeyDown;    recKey.delayMs = 25;  recKey.keyCode = 13;
            editor->onMacroRecorded({recMove, recKey}); // mô phỏng AutoClickWindow gọi khi ActionRecorder ghi xong
            const Action macroResult = editor->getAction();
            CHECK(macroResult.type == ActionType::RecordedMacro);
            CHECK(macroResult.recordedEvents.size() == 2);
            if (macroResult.recordedEvents.size() == 2)
            {
                CHECK(macroResult.recordedEvents[0].type == RecordedEventType::MouseMove && macroResult.recordedEvents[0].x == 50);
                CHECK(macroResult.recordedEvents[1].type == RecordedEventType::KeyDown && macroResult.recordedEvents[1].keyCode == 13);
            }

            // Nạp LẠI một action khác rồi nạp macro cũ - setAction() phải khôi phục ĐÚNG m_recordedEvents
            // từ Action đang nạp (không giữ sót dữ liệu của lần onMacroRecorded() trước).
            Action anotherClick;
            anotherClick.type = ActionType::MouseClick;
            editor->setAction(anotherClick, 1);
            CHECK(editor->getAction().recordedEvents.empty());
            editor->setAction(macroResult, 0);
            CHECK(editor->getAction().recordedEvents.size() == 2);

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

                // Người dùng sửa dở một trường mà chưa bấm "Lưu hành động" - QSpinBox-ms cũ đã đổi thành
                // DurationInput (v1.19.7), tìm lại theo objectName thay vì suffix "ms" không còn tồn tại.
                auto* waitInput = editor->findChild<DurationInput*>("waitBeforeInput");
                CHECK(waitInput != nullptr);
                if (waitInput)
                    waitInput->setValueMs(7777);
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

                // -- Cache chỉnh sửa chưa lưu khi đổi hàng xem: KHÔNG hỏi gì, chỉ giữ lại (m_pendingEdits) --
                // `editing` (waitBefore=7777) vẫn đang sửa dở ở hàng 0, chưa Apply - đổi sang hàng 1 không
                // được hỏi lưu (chỉ RUN/đóng cửa sổ mới hỏi) và không được làm mất chỉnh sửa đó.
                CHECK(editor->isDirty());
                actionList->setSelectedActionIndex(1);
                app.processEvents();
                CHECK(table->item(0, 0)->text().contains("*")); // vẫn còn dấu "*" dù đã rời đi
                CHECK(!editor->isDirty()); // hàng 1 vừa nạp CÒN SẠCH (chưa ai sửa gì ở hàng này)
                actionList->setSelectedActionIndex(0);
                app.processEvents();
                // Quay lại đúng hàng 0: phải nạp lại ĐÚNG bản đang sửa dở (cache), không phải bản gốc đã lưu.
                // editor->isDirty() tự về false ngay sau MỌI setAction() (kể cả nạp từ cache - "dirty" của
                // chính ActionEditorWidget chỉ nghĩa là "đã gõ gì đó kể từ lần nạp gần nhất", không phải
                // "khác dữ liệu gốc đã lưu") - dấu "*" ở bảng (do AutoClickWindow quản lý qua m_pendingEdits)
                // mới là chỉ báo ĐÚNG cho "còn cache chưa lưu", kiểm bên dưới.
                const Action cached = editor->getAction();
                CHECK(cached.waitBefore == editing.waitBefore);
                CHECK(table->item(0, 0)->text().contains("*"));

                // -- "✓ Lưu hành động (Apply)" khi đang sửa dở: PHẢI lưu xong NGAY LẦN BẤM ĐẦU --
                // (lỗi thật người dùng báo: phải bấm Lưu 2 lần dấu "*" mới hết. Nguyên nhân gốc: onActionSaved()
                // gọi setActions() - vẽ lại bảng - và setActions() tự phát currentCellChanged; editor LÚC ĐÓ còn
                // "dirty" (m_dirty chỉ reset SAU khi emit actionSaved() trả về) nên captureEditorIfDirty() ghi
                // LẠI đúng bản vừa Lưu vào m_pendingEdits - bản chụp "chưa lưu" ma, dấu "*" hiện lại.)
                {
                    actionList->setSelectedActionIndex(2);
                    app.processEvents();
                    CHECK(!table->item(2, 0)->text().contains("*")); // hàng 2 sạch
                    auto* w2 = editor->findChild<DurationInput*>("waitBeforeInput");
                    CHECK(w2 != nullptr);
                    if (w2)
                        w2->setValueMs(4321);
                    CHECK(editor->isDirty());
                    CHECK(table->item(2, 0)->text().contains("*"));
                    QPushButton* apply = nullptr;
                    for (QPushButton* b : editor->findChildren<QPushButton*>())
                        if (b->text().contains("Lưu hành động"))
                            apply = b;
                    CHECK(apply != nullptr);
                    if (apply)
                    {
                        apply->click(); // ĐÚNG MỘT lần bấm
                        app.processEvents();
                        CHECK(!table->item(2, 0)->text().contains("*")); // hết dấu "chưa lưu" ngay lần đầu
                        CHECK(table->item(2, 3)->text().contains("4321")); // dữ liệu đã vào bảng
                        CHECK(!editor->isDirty());
                        CHECK(table->item(0, 0)->text().contains("*")); // cache của hàng KHÁC (hàng 0) không bị đụng
                        // Hàng 2 đã lưu thật - rời đi rồi quay lại vẫn nạp đúng 4321, không phải cache cũ/bản gốc.
                        actionList->setSelectedActionIndex(1);
                        app.processEvents();
                        actionList->setSelectedActionIndex(2);
                        app.processEvents();
                        CHECK(editor->getAction().waitBefore.count() == 4321);
                        CHECK(!table->item(2, 0)->text().contains("*"));
                    }
                }

                // -- Nhân bản một hàng KHI hàng KHÁC đang sửa dở: cache phải đi theo hàng bị dời chỗ --
                {
                    actionList->setSelectedActionIndex(2);
                    app.processEvents();
                    auto* w3 = editor->findChild<DurationInput*>("waitBeforeInput");
                    if (w3)
                        w3->setValueMs(5555); // sửa dở hàng 2, CHƯA lưu
                    CHECK(editor->isDirty());
                    const int rowsBefore = table->rowCount();
                    // Nhân bản hàng 0 -> bản sao chèn ở hàng 1, hàng sửa dở dời từ 2 xuống 3.
                    CHECK(QMetaObject::invokeMethod(&win, "onCloneAction", Qt::DirectConnection, Q_ARG(int, 0)));
                    app.processEvents();
                    CHECK(table->rowCount() == rowsBefore + 1);
                    CHECK(table->item(0, 0)->text().contains("*")); // cache cũ của hàng 0 (7777) vẫn ở hàng 0
                    CHECK(!table->item(1, 0)->text().contains("*")); // bản sao sạch
                    CHECK(!table->item(2, 0)->text().contains("*")); // hàng 2 MỚI (cũ là hàng 1) sạch - không dính cache nhầm
                    CHECK(table->item(3, 0)->text().contains("*"));  // cache đi theo hàng bị dời (2 -> 3)
                    actionList->setSelectedActionIndex(3);
                    app.processEvents();
                    CHECK(editor->getAction().waitBefore.count() == 5555);
                    actionList->setSelectedActionIndex(2);
                    app.processEvents();
                    CHECK(editor->getAction().waitBefore.count() != 5555);
                }

                // -- Xóa một hàng KHI hàng KHÁC đang sửa dở: chỉnh sửa dở đó không được mất / lệch hàng --
                {
                    // Trạng thái: cache hàng 0 (7777) + hàng 3 (5555). Sửa dở thêm hàng 4 (6666), CHƯA lưu.
                    actionList->setSelectedActionIndex(4);
                    app.processEvents();
                    auto* w4 = editor->findChild<DurationInput*>("waitBeforeInput");
                    if (w4)
                        w4->setValueMs(6666);
                    CHECK(editor->isDirty());
                    CHECK(QMetaObject::invokeMethod(&win, "onDeleteAction", Qt::DirectConnection, Q_ARG(int, 1)));
                    app.processEvents();
                    // Sau xóa hàng 1: hàng 0 giữ cache, 5555 dời 3 -> 2, 6666 dời 4 -> 3; hàng 1 và 4 sạch.
                    CHECK(table->item(0, 0)->text().contains("*"));
                    CHECK(!table->item(1, 0)->text().contains("*"));
                    CHECK(table->item(2, 0)->text().contains("*"));
                    CHECK(table->item(3, 0)->text().contains("*"));
                    CHECK(!table->item(4, 0)->text().contains("*"));
                    actionList->setSelectedActionIndex(3);
                    app.processEvents();
                    CHECK(editor->getAction().waitBefore.count() == 6666);
                    actionList->setSelectedActionIndex(2);
                    app.processEvents();
                    CHECK(editor->getAction().waitBefore.count() == 5555);
                }

                // -- Đóng cửa sổ khi còn cache chưa lưu: PHẢI HỎI (Hủy -> không đóng, giữ nguyên cache) --
                bool dialogSeen = false;
                QTimer::singleShot(0, &win, [&dialogSeen] {
                    if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    {
                        dialogSeen = true;
                        box->button(QMessageBox::Cancel)->click();
                    }
                });
                const bool closedByCancel = win.close();
                app.processEvents();
                CHECK(dialogSeen);
                CHECK(!closedByCancel);
                CHECK(win.isVisible()); // Hủy -> cửa sổ KHÔNG đóng, cache vẫn còn nguyên
                CHECK(table->item(0, 0)->text().contains("*")); // cache vẫn còn - xem comment isDirty() ở trên

                // Lần hai: chọn "Không" (bỏ chỉnh sửa dở) - phải đóng được.
                dialogSeen = false;
                QTimer::singleShot(0, &win, [&dialogSeen] {
                    if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
                    {
                        dialogSeen = true;
                        box->button(QMessageBox::No)->click();
                    }
                });
                const bool closedByNo = win.close();
                app.processEvents();
                CHECK(dialogSeen);
                CHECK(closedByNo);
                CHECK(!win.isVisible());
            }
        }
    }

    // ---- ItemHoverCursor: ngón tay trỏ CHỈ khi chuột nằm trên một mục (danh sách công cụ ở cửa sổ chính) ----
    {
        QListWidget list;
        for (const char* n : {"Auto Click", "QR Tools", "Disk Cleanup"})
            list.addItem(n);
        list.setAttribute(Qt::WA_DontShowOnScreen, true);
        list.resize(220, 400);
        list.show();
        app.processEvents();
        ItemHoverCursor::install(&list);
        QWidget* vp = list.viewport();
        CHECK(list.hasMouseTracking() && vp->hasMouseTracking()); // nhận MouseMove khi chưa bấm nút nào
        CHECK(vp->cursor().shape() == Qt::ArrowCursor);
        const QPoint onItem = list.visualItemRect(list.item(1)).center();
        const QPoint emptyArea(vp->width() / 2, vp->height() - 5); // dưới mục cuối - không có gì để bấm
        CHECK(!list.indexAt(emptyArea).isValid());
        auto move = [&](const QPoint& p) {
            QMouseEvent ev(QEvent::MouseMove, QPointF(p), QPointF(vp->mapToGlobal(p)), Qt::NoButton, Qt::NoButton,
                           Qt::NoModifier);
            QApplication::sendEvent(vp, &ev);
        };
        move(onItem);
        CHECK(vp->cursor().shape() == Qt::PointingHandCursor);
        move(emptyArea);
        CHECK(vp->cursor().shape() == Qt::ArrowCursor); // vùng trống: trả lại mũi tên
        move(onItem);
        CHECK(vp->cursor().shape() == Qt::PointingHandCursor);
        QEvent leave(QEvent::Leave);
        QApplication::sendEvent(vp, &leave);
        CHECK(vp->cursor().shape() == Qt::ArrowCursor); // rời khỏi danh sách: trả lại mũi tên
        ItemHoverCursor::install(nullptr);               // null an toàn
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: chay chuoi that (SendInput/SetCursorPos), phim dung toan cuc Ctrl+Alt+F8, bat toa do/\n");
    std::printf("thao tac keo/to hop phim (hook toan cuc) KHONG duoc chay trong test tu dong - can kiem tra tay.\n");
    return g_fail == 0 ? 0 : 1;
}
