// Kiểm thử "khói" cho giao diện Disk Cleanup: dựng cửa sổ, không crash, các widget chính tồn tại.
// KHÔNG bấm nút Quét thật ở CleanupTab/LargeFilesTab/DuplicateFilesTab: hạng mục dọn dẹp trỏ vào
// %TEMP%/%WINDIR% thật và "Tìm tệp lớn"/"Tìm tệp trùng lặp" cần người dùng chọn thư mục trước - đụng
// vào hệ thống thật của máy build, không phù hợp cho test tự động (giống nguyên tắc đã áp dụng cho
// wifi_ui_tests: quét/kết nối thật phải tự kiểm tra tay). PartitionTab thì khác: liệt kê phân vùng
// (listPartitions) chỉ ĐỌC và AN TOÀN (giống DiskSpaceInfo::listDrives()), nên được gọi thật ngay khi
// dựng UI - test xác nhận nó thực sự chạy được trên máy build. KHÔNG bao giờ test đổi kích thước thật.
// Build: cmake --build build --target diskcleanup_ui_tests && build\diskcleanup_ui_tests.exe
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTableWidget>
#include <QTemporaryDir>
#include "TestDataDir.h"
#include <QTreeWidget>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core/AppPaths.h"

#include "tools/diskcleanup/CleanupTab.h"
#include "tools/diskcleanup/DiskCleanupWindow.h"
#include "tools/diskcleanup/DiskMapWidget.h"
#include "tools/diskcleanup/DuplicateFilesTab.h"
#include "tools/diskcleanup/LargeFilesTab.h"
#include "tools/diskcleanup/PartitionTab.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // Log của ứng dụng (Logger) ghi vào thư mục tạm - KHÔNG đụng dữ liệu thật của người dùng.
    TestDataDir dataDir;
    AppPaths::setDataDirOverride(dataDir.path());

    DiskCleanupWindow win;
    win.setAttribute(Qt::WA_DontShowOnScreen, true);
    win.show();
    app.processEvents();
    CHECK(true); // tới được đây tức là dựng cửa sổ không crash

    auto* tabs = win.findChild<QTabWidget*>();
    CHECK(tabs != nullptr && tabs->count() == 4);

    auto* cleanupTab = win.findChild<CleanupTab*>();
    CHECK(cleanupTab != nullptr);
    if (cleanupTab)
    {
        auto* table = cleanupTab->findChild<QTableWidget*>();
        CHECK(table != nullptr);
        // Ngay sau khi dựng, bảng rỗng vì chưa bấm Quét (xem lý do ở đầu file)
        CHECK(table != nullptr && table->rowCount() == 0);

        const auto buttons = cleanupTab->findChildren<QPushButton*>();
        CHECK(buttons.size() >= 2); // ít nhất nút Quét + nút Dọn dẹp

        auto* permanentCheck = cleanupTab->findChild<QCheckBox*>();
        CHECK(permanentCheck != nullptr && !permanentCheck->isChecked()); // mặc định KHÔNG xóa vĩnh viễn

        auto* progress = cleanupTab->findChild<QProgressBar*>();
        CHECK(progress != nullptr && !progress->isVisible()); // chưa quét thì chưa hiện progress bar
    }

    if (tabs) { tabs->setCurrentIndex(1); app.processEvents(); }

    auto* largeFilesTab = win.findChild<LargeFilesTab*>();
    CHECK(largeFilesTab != nullptr);
    if (largeFilesTab)
    {
        auto* table = largeFilesTab->findChild<QTableWidget*>();
        CHECK(table != nullptr && table->rowCount() == 0);
    }

    if (tabs) { tabs->setCurrentIndex(2); app.processEvents(); }

    auto* dupTab = win.findChild<DuplicateFilesTab*>();
    CHECK(dupTab != nullptr);
    if (dupTab)
    {
        auto* tree = dupTab->findChild<QTreeWidget*>();
        CHECK(tree != nullptr && tree->topLevelItemCount() == 0); // chưa quét -> rỗng

        const auto buttons = dupTab->findChildren<QPushButton*>();
        CHECK(buttons.size() >= 2); // Chọn thư mục + Quét (+ Dừng/Xóa ẩn sẵn)

        // ---- Mỗi nhóm trùng lặp LUÔN phải giữ lại ít nhất 1 bản (chặn trong code, không chỉ lời nhắn) ----
        // Nạp một kết quả dựng sẵn (đường dẫn không tồn tại - KHÔNG có gì bị xóa, không bấm nút Xóa).
        if (tree)
        {
            DuplicateGroup group;
            group.paths = {"Z:/khong_ton_tai/a.bin", "Z:/khong_ton_tai/b.bin", "Z:/khong_ton_tai/c.bin"};
            group.lastWriteTimes = {1, 2, 3};
            group.sizeEachBytes = 5000;
            group.hashHex = "00";
            dupTab->onScanFinished({group}, group.wastedBytes(), 1);
            app.processEvents();

            CHECK(tree->topLevelItemCount() == 1);
            QTreeWidgetItem* groupItem = tree->topLevelItemCount() == 1 ? tree->topLevelItem(0) : nullptr;
            if (groupItem && groupItem->childCount() == 3)
            {
                QTreeWidgetItem* a = groupItem->child(0);
                QTreeWidgetItem* b = groupItem->child(1);
                QTreeWidgetItem* c = groupItem->child(2);
                // Mặc định: giữ bản đầu, tick sẵn các bản còn lại
                CHECK(a->checkState(0) == Qt::Unchecked);
                CHECK(b->checkState(0) == Qt::Checked && c->checkState(0) == Qt::Checked);

                // Tick nốt bản cuối cùng còn giữ -> bị hoàn lại ngay (không thể chọn xóa cả nhóm)
                a->setCheckState(0, Qt::Checked);
                app.processEvents();
                CHECK(a->checkState(0) == Qt::Unchecked);

                // Bỏ tick b (b thành bản giữ lại) thì tick a được; rồi tick lại b -> bị hoàn lại
                b->setCheckState(0, Qt::Unchecked);
                a->setCheckState(0, Qt::Checked);
                app.processEvents();
                CHECK(a->checkState(0) == Qt::Checked);
                b->setCheckState(0, Qt::Checked);
                app.processEvents();
                CHECK(b->checkState(0) == Qt::Unchecked);

                int kept = 0;
                for (int i = 0; i < groupItem->childCount(); ++i)
                    if (groupItem->child(i)->checkState(0) != Qt::Checked)
                        ++kept;
                CHECK(kept >= 1);
            }
            else
            {
                CHECK(false); // cây không dựng đúng 3 dòng con
            }
        }
    }

    if (tabs) { tabs->setCurrentIndex(3); app.processEvents(); }

    auto* partTab = win.findChild<PartitionTab*>();
    CHECK(partTab != nullptr);
    if (partTab)
    {
        // listPartitions() chạy THẬT ngay khi dựng UI (chỉ đọc, an toàn) - máy Windows nào cũng có
        // ít nhất phân vùng chứa Windows, nên bảng phải có dữ liệu.
        auto* table = partTab->findChild<QTableWidget*>();
        CHECK(table != nullptr);
        // Danh sách phân vùng nay được đọc trên luồng NỀN (không còn làm đứng giao diện lúc mở cửa sổ) -
        // chờ tới khi xong (tối đa 60 giây: lần đầu PowerShell nạp module Storage có thể chậm).
        QElapsedTimer loadTimer;
        loadTimer.start();
        while (partTab->isLoading() && loadTimer.elapsed() < 60000)
            app.processEvents(QEventLoop::AllEvents, 50);
        CHECK(!partTab->isLoading());
        if (table)
        {
            std::printf("PartitionTab: liet ke duoc %d phan vung\n", table->rowCount());
            CHECK(table->rowCount() > 0);
            CHECK(table->columnCount() == 7);
        }

        // Chưa chọn dòng nào -> khu vực đổi kích thước phải đang ẩn (không bao giờ lộ sẵn thao tác nguy hiểm)
        auto* progress = partTab->findChild<QProgressBar*>();
        CHECK(progress != nullptr && !progress->isVisible());

        const auto buttons = partTab->findChildren<QPushButton*>();
        CHECK(buttons.size() >= 2); // ít nhất Làm mới + (Chạy lại quyền Quản trị, nếu chưa elevated)

        // ---- Đổi dòng chọn phải XÓA SẠCH trạng thái của lần tra trước ----
        // Hồi quy lỗi thật: chọn ổ A -> tra -> gõ xác nhận (nút bật) -> bấm sang dòng ổ B: nút vẫn bật cùng
        // khoảng kích thước của A, bấm là đổi kích thước ổ B theo số liệu của ổ A. Test CHỈ gán một khoảng
        // kích thước GIẢ qua applySupportedRangeForTest() và đọc trạng thái widget - KHÔNG BAO GIỜ bấm nút
        // "Đổi kích thước" (không có lệnh PowerShell đổi phân vùng nào được dựng hay chạy ở đây).
        auto* resizeBtn = partTab->findChild<QPushButton*>("partitionResizeButton");
        auto* confirmEdit = partTab->findChild<QLineEdit*>("partitionConfirmEdit");
        auto* sizeSpin = partTab->findChild<QDoubleSpinBox*>("partitionNewSizeSpin");
        CHECK(resizeBtn && confirmEdit && sizeSpin);
        if (table && resizeBtn && confirmEdit && sizeSpin && table->rowCount() > 0)
        {
            CHECK(resizeBtn->isHidden() && !resizeBtn->isEnabled());
            CHECK(!partTab->resizeRequestBlockReason().isEmpty()); // chưa chọn gì

            table->selectRow(0);
            app.processEvents();
            CHECK(resizeBtn->isHidden());
            CHECK(!partTab->resizeRequestBlockReason().isEmpty()); // đã chọn nhưng chưa tra

            PartitionManager::SupportedSizeRange fakeRange;
            fakeRange.ok = true;
            fakeRange.minBytes = 64LL * 1024 * 1024;
            fakeRange.maxBytes = 1024LL * 1024 * 1024 * 1024;
            partTab->applySupportedRangeForTest(fakeRange);
            CHECK(!resizeBtn->isHidden());
            CHECK(!resizeBtn->isEnabled()); // chưa gõ xác nhận
            CHECK(!confirmEdit->isHidden() && !sizeSpin->isHidden());

            const QString token = confirmEdit->placeholderText();
            CHECK(!token.isEmpty());
            confirmEdit->setText(token + "x");
            CHECK(!resizeBtn->isEnabled()); // gõ sai
            confirmEdit->setText(token);
            CHECK(resizeBtn->isEnabled());
            CHECK(partTab->resizeRequestBlockReason().isEmpty());

            if (table->rowCount() >= 2)
            {
                table->selectRow(1); // sang phân vùng KHÁC
                app.processEvents();
                CHECK(!resizeBtn->isEnabled());
                CHECK(resizeBtn->isHidden());
                CHECK(confirmEdit->text().isEmpty() && confirmEdit->isHidden());
                CHECK(sizeSpin->isHidden());
                CHECK(!partTab->resizeRequestBlockReason().isEmpty());

                // Gõ lại đúng chuỗi cũ khi chưa tra cho phân vùng mới -> vẫn bị chặn
                confirmEdit->setText(token);
                CHECK(!resizeBtn->isEnabled());
                CHECK(!partTab->resizeRequestBlockReason().isEmpty());

                // Quay lại dòng cũ: cũng phải tra lại từ đầu
                table->selectRow(0);
                app.processEvents();
                CHECK(!resizeBtn->isEnabled() && resizeBtn->isHidden());
                CHECK(confirmEdit->text().isEmpty());
                CHECK(!partTab->resizeRequestBlockReason().isEmpty());
            }
            else
            {
                std::printf("LUU Y: may chi co 1 phan vung - bo qua kiem tra doi dong chon\n");
            }

            // Bỏ chọn hẳn cũng xóa trạng thái
            partTab->applySupportedRangeForTest(fakeRange);
            confirmEdit->setText(confirmEdit->placeholderText());
            CHECK(resizeBtn->isEnabled());
            table->clearSelection();
            app.processEvents();
            CHECK(!resizeBtn->isEnabled() && resizeBtn->isHidden());
            CHECK(!partTab->resizeRequestBlockReason().isEmpty());
            CHECK(!partTab->isResizingNow());
        }
    }

    // ---- Sơ đồ đĩa kiểu Disk Management + tay nắm kéo: dữ liệu GIẢ, KHÔNG PowerShell, KHÔNG đụng đĩa ----
    constexpr qint64 MiB = 1024LL * 1024, GiB = 1024 * MiB;
    PartitionManager::DiskInfo fakeDisk;
    fakeDisk.number = 0;
    fakeDisk.sizeBytes = 256060514304LL;
    fakeDisk.partitionStyle = "GPT";
    auto fakePart = [&](int num, const char* type, qint64 offMiB, qint64 sizeMiB, const char* letter, const char* label, const char* fs) {
        PartitionManager::PartitionInfo p;
        p.diskNumber = 0;
        p.partitionNumber = num;
        p.type = type;
        p.driveLetter = letter;
        p.label = label;
        p.fileSystem = fs;
        p.offsetBytes = offMiB * MiB;
        p.sizeBytes = sizeMiB * MiB;
        p.isSystem = QString(type) == "System";
        p.isBoot = QString(letter) == "C";
        return p;
    };
    // Đúng bố cục ảnh Disk Management của người dùng: EFI | MSR | C: | D: | ~67 GB chưa phân bổ | Recovery
    const QList<PartitionManager::PartitionInfo> fakeParts = {
        fakePart(1, "System", 1, 100, "", "", "FAT32"), fakePart(2, "Reserved", 101, 16, "", "", ""),
        fakePart(3, "Basic", 117, 123557, "C", "Windows", "NTFS"), fakePart(4, "Basic", 123675, 51394, "D", "Data", "NTFS"),
        fakePart(5, "Recovery", 243675, 522, "", "", "NTFS")};
    PartitionManager::SupportedSizeRange fakeDRange; // khoảng ảnh người dùng: 45.29 - 117.19 GB
    fakeDRange.ok = true;
    fakeDRange.minBytes = static_cast<qint64>(45.29 * GiB);
    fakeDRange.maxBytes = static_cast<qint64>(117.19 * GiB);
    const qint64 dSize = fakeParts[3].sizeBytes;
    {
        DiskMapWidget map;
        map.setData({fakeDisk}, fakeParts);
        map.resize(900, map.sizeHint().height());
        auto mouse = [&](QEvent::Type type, QPoint p, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent e(type, p, map.mapToGlobal(p), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(&map, &e);
        };
        auto press = [&](QPoint p) { mouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton); };
        auto move = [&](QPoint p) { mouse(QEvent::MouseMove, p, Qt::NoButton, Qt::LeftButton); };
        auto release = [&](QPoint p) { mouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton); };
        auto key = [&](int k, Qt::KeyboardModifiers mods = Qt::NoModifier) {
            QKeyEvent e(QEvent::KeyPress, k, mods);
            QApplication::sendEvent(&map, &e);
        };

        CHECK(map.rowCount() == 1);
        CHECK(map.handleRectForTest().isNull() && !map.isResizeActive()); // chưa tra khoảng cho phép -> KHÔNG có tay nắm
        const QRect eRect = map.blockRectForTest(0, 1), cRect = map.blockRectForTest(0, 3), dRect = map.blockRectForTest(0, 4);
        CHECK(!eRect.isNull() && !cRect.isNull() && !dRect.isNull());
        CHECK(eRect.width() >= 36);                      // EFI 100 MB vẫn nhìn thấy được (độ rộng tối thiểu)
        CHECK(cRect.width() > dRect.width());            // khối rộng theo dung lượng: C: (120 GB) > D: (50 GB)
        const QRect freeRect = map.unallocatedRectForTest(0, 0);
        CHECK(!freeRect.isNull() && freeRect.left() > dRect.right() && freeRect.width() > dRect.width()); // 67 GB nằm ngay SAU D:

        int clickCount = 0, clickDisk = -1, clickPart = -1;
        QObject::connect(&map, &DiskMapWidget::partitionClicked, [&](int d, int p) { ++clickCount; clickDisk = d; clickPart = p; });
        press(cRect.center());
        release(cRect.center());
        CHECK(clickCount == 1 && clickDisk == 0 && clickPart == 3);
        press(freeRect.center()); // vùng chưa phân bổ không chọn được
        release(freeRect.center());
        CHECK(clickCount == 1);

        map.setResizeContext(0, 4, fakeDRange, DiskLayout::ResizeDirection::Free);
        CHECK(map.isResizeActive() && map.previewSize() == dSize && map.currentSize() == dSize);
        CHECK(map.bounds().minBytes == fakeDRange.minBytes && map.bounds().maxBytes == fakeDRange.maxBytes);
        CHECK(map.freeBytesAfterResizeTarget() == (243675 - 123675 - 51394) * MiB && map.freeBytesBeforeResizeTarget() == 0);
        QRect h = map.handleRectForTest();
        CHECK(!h.isNull() && std::abs(h.center().x() - dRect.right()) <= 1); // tay nắm nằm ở MÉP PHẢI của D:

        int sigCount = 0;
        qint64 lastBytes = -1;
        QObject::connect(&map, &DiskMapWidget::previewSizeChanged, [&](qint64 b) { ++sigCount; lastBytes = b; });

        // Kéo SANG TRÁI = thu nhỏ; SANG PHẢI = mở rộng; kẹp đúng hai đầu khoảng Windows cho phép
        press(h.center());
        move(h.center() + QPoint(-10, 0));
        CHECK(map.previewSize() < dSize && map.previewSize() >= fakeDRange.minBytes && sigCount >= 1 && lastBytes == map.previewSize());
        move(h.center() + QPoint(20, 0));
        CHECK(map.previewSize() > dSize && map.previewSize() <= fakeDRange.maxBytes);
        move(h.center() + QPoint(-900, 0));
        CHECK(map.previewSize() == fakeDRange.minBytes);
        move(h.center() + QPoint(900, 0));
        CHECK(map.previewSize() == fakeDRange.maxBytes);
        release(h.center());
        const qint64 held = map.previewSize();
        move(h.center() + QPoint(-100, 0)); // đã nhả chuột: di chuyển không làm gì nữa
        CHECK(map.previewSize() == held);

        // Phím: ←/→ 1 GB, Home/End = đầu khoảng, Esc = về hiện tại; ô nhập gọi setPreviewSize() KHÔNG phát tín hiệu
        key(Qt::Key_Escape);
        CHECK(map.previewSize() == dSize);
        key(Qt::Key_Right);
        CHECK(map.previewSize() == dSize + GiB);
        key(Qt::Key_Left, Qt::ControlModifier); // 10 GB sang trái từ (hiện tại + 1 GB) rơi xuống DƯỚI mức tối thiểu -> kẹp đúng mức tối thiểu
        CHECK(map.previewSize() == fakeDRange.minBytes);
        key(Qt::Key_Right, Qt::ShiftModifier);  // Shift = bước nhỏ 100 MB
        CHECK(map.previewSize() == fakeDRange.minBytes + 100 * MiB);
        key(Qt::Key_Home);
        CHECK(map.previewSize() == fakeDRange.minBytes);
        key(Qt::Key_End);
        CHECK(map.previewSize() == fakeDRange.maxBytes);
        const int sigBefore = sigCount;
        map.setPreviewSize(5000 * GiB);
        CHECK(map.previewSize() == fakeDRange.maxBytes && sigCount == sigBefore);
        map.setPreviewSize(dSize - 3 * GiB);
        CHECK(map.previewSize() == dSize - 3 * GiB && sigCount == sigBefore);

        // KHÓA hướng: chỉ thu nhỏ -> không kéo quá kích thước hiện tại; chỉ mở rộng -> không nhỏ hơn hiện tại
        map.setDirection(DiskLayout::ResizeDirection::ShrinkOnly);
        CHECK(map.bounds().maxBytes == dSize && map.bounds().minBytes == fakeDRange.minBytes && map.previewSize() == dSize - 3 * GiB);
        map.setPreviewSize(dSize);
        h = map.handleRectForTest();
        press(h.center());
        move(h.center() + QPoint(300, 0));
        CHECK(map.previewSize() == dSize); // kéo sang PHẢI bị chặn
        move(h.center() + QPoint(-10, 0));
        CHECK(map.previewSize() < dSize);  // kéo sang TRÁI vẫn được
        release(h.center());
        key(Qt::Key_End);
        CHECK(map.previewSize() == dSize); // End = đầu TRÊN của khoảng đã khóa

        map.setDirection(DiskLayout::ResizeDirection::GrowOnly);
        CHECK(map.bounds().minBytes == dSize && map.bounds().maxBytes == fakeDRange.maxBytes && map.previewSize() == dSize); // kẹp lên hiện tại
        h = map.handleRectForTest();
        press(h.center());
        move(h.center() + QPoint(-300, 0));
        CHECK(map.previewSize() == dSize); // kéo sang TRÁI bị chặn
        move(h.center() + QPoint(15, 0));
        CHECK(map.previewSize() > dSize);
        release(h.center());

        map.clearResizeContext();
        CHECK(!map.isResizeActive() && map.handleRectForTest().isNull());
        map.setResizeContext(0, 99, fakeDRange, DiskLayout::ResizeDirection::Free); // phân vùng không tồn tại
        CHECK(!map.isResizeActive());
        PartitionManager::SupportedSizeRange bad;
        map.setResizeContext(0, 4, bad, DiskLayout::ResizeDirection::Free);          // khoảng chưa hợp lệ
        CHECK(!map.isResizeActive());
        map.setResizeContext(0, 4, fakeDRange, DiskLayout::ResizeDirection::Free);
        map.setData({fakeDisk}, fakeParts);                                            // dữ liệu mới -> bỏ chế độ kéo
        CHECK(!map.isResizeActive());

        // Chỉ vẽ trên máy thật khi cần xem ảnh: DISKCLEANUP_UI_SHOTS=<thư mục>
        if (qEnvironmentVariableIsSet("DISKCLEANUP_UI_SHOTS"))
        {
            const QString dir = qEnvironmentVariable("DISKCLEANUP_UI_SHOTS");
            map.setSelection(0, 4);
            map.grab().save(dir + "/diskmap_idle.png");
            map.setResizeContext(0, 4, fakeDRange, DiskLayout::ResizeDirection::Free);
            map.setPreviewSize(dSize - 20 * GiB);
            map.grab().save(dir + "/diskmap_shrink.png");
            map.setPreviewSize(dSize + 30 * GiB);
            map.grab().save(dir + "/diskmap_grow.png");
        }
    }

    // ---- PartitionTab + sơ đồ: đồng bộ chọn dòng <-> khối, kéo/nhập <-> ô nhập, nút KHÓA hướng ----
    // Vẫn chỉ gán khoảng kích thước GIẢ (applySupportedRangeForTest) và đọc trạng thái widget - KHÔNG bấm "Đổi kích thước".
    if (partTab && !partTab->isLoading())
    {
        QString listError;
        const auto realParts = PartitionManager::listPartitions(&listError); // CHỈ ĐỌC; thứ tự giống bảng
        auto* table = partTab->findChild<QTableWidget*>();
        auto* spin = partTab->findChild<QDoubleSpinBox*>("partitionNewSizeSpin");
        auto* confirm = partTab->findChild<QLineEdit*>("partitionConfirmEdit");
        auto* resizeBtn = partTab->findChild<QPushButton*>("partitionResizeButton");
        auto* change = partTab->findChild<QLabel*>("partitionChangeLabel");
        auto* edgeNote = partTab->findChild<QLabel*>("partitionEdgeNote");
        auto* dirShrink = partTab->findChild<QPushButton*>("partitionDirShrink");
        auto* dirFree = partTab->findChild<QPushButton*>("partitionDirFree");
        auto* dirGrow = partTab->findChild<QPushButton*>("partitionDirGrow");
        DiskMapWidget* map = partTab->diskMap();
        CHECK(table && spin && confirm && resizeBtn && change && edgeNote && dirShrink && dirFree && dirGrow && map);
        CHECK(map && map->rowCount() >= 1);
        if (table && spin && confirm && resizeBtn && change && edgeNote && dirShrink && dirFree && dirGrow && map && realParts.size() == table->rowCount())
        {
            // Chưa tra kích thước: hàng hướng/mô tả/ghi chú mép phải đều ẨN, sơ đồ không có tay nắm
            CHECK(!dirShrink->isVisibleTo(partTab) && !change->isVisibleTo(partTab) && !edgeNote->isVisibleTo(partTab));
            CHECK(!map->isResizeActive());

            // Chọn phân vùng lớn nhất (thường là C:) để ô nhập 2 chữ số thập phân đủ độ phân giải
            int big = 0;
            for (int i = 1; i < realParts.size(); ++i)
                if (realParts[i].sizeBytes > realParts[big].sizeBytes)
                    big = i;
            // Bấm khối trên SƠ ĐỒ -> chọn đúng dòng trong bảng
            const QRect bigRect = map->blockRectForTest(realParts[big].diskNumber, realParts[big].partitionNumber);
            CHECK(!bigRect.isNull());
            {
                QMouseEvent e(QEvent::MouseButtonPress, bigRect.center(), map->mapToGlobal(bigRect.center()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(map, &e);
            }
            app.processEvents();
            const auto rows = table->selectionModel()->selectedRows();
            CHECK(rows.size() == 1 && rows.first().row() == big);

            PartitionManager::SupportedSizeRange range;
            range.ok = true;
            range.minBytes = 64LL * MiB;
            range.maxBytes = 4096 * GiB;
            partTab->applySupportedRangeForTest(range);
            const qint64 cur = realParts[big].sizeBytes;
            const double curGb = static_cast<double>(cur) / GiB;
            CHECK(dirShrink->isVisibleTo(partTab) && dirFree->isVisibleTo(partTab) && dirGrow->isVisibleTo(partTab));
            CHECK(change->isVisibleTo(partTab) && edgeNote->isVisibleTo(partTab) && edgeNote->text().contains("MÉP PHẢI"));
            CHECK(dirFree->isChecked() && !dirShrink->isChecked() && !dirGrow->isChecked());
            CHECK(partTab->resizeDirection() == DiskLayout::ResizeDirection::Free);
            CHECK(map->isResizeActive() && map->currentSize() == cur && map->previewSize() == cur); // ô nhập chưa sửa = đúng kích thước hiện tại
            CHECK(change->text().contains("Chưa thay đổi"));

            // Nhập kích thước nhỏ hơn -> sơ đồ + mô tả theo: THU NHỎ, vùng giải phóng nằm BÊN PHẢI
            spin->setValue(curGb - 5.0);
            CHECK(map->previewSize() < cur && std::abs(double(map->previewSize()) / GiB - (curGb - 5.0)) < 0.02);
            CHECK(change->text().contains("THU NHỎ") && change->text().contains("BÊN PHẢI") && change->text().contains("TRÁI giữ nguyên"));
            spin->setValue(curGb + 5.0);
            CHECK(map->previewSize() > cur && change->text().contains("MỞ RỘNG"));
            CHECK(change->text().contains("⚠") == (map->freeBytesAfterResizeTarget() < 5 * GiB)); // cảnh báo khi vùng trống liền kề bên phải không đủ

            // Kéo tay nắm trên sơ đồ -> ô nhập cập nhật theo (và ngược lại ở trên)
            QRect h = map->handleRectForTest();
            CHECK(!h.isNull());
            {
                QMouseEvent p1(QEvent::MouseButtonPress, h.center(), map->mapToGlobal(h.center()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(map, &p1);
                const QPoint to = h.center() + QPoint(-6, 0);
                QMouseEvent m1(QEvent::MouseMove, to, map->mapToGlobal(to), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(map, &m1);
                QMouseEvent r1(QEvent::MouseButtonRelease, to, map->mapToGlobal(to), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(map, &r1);
            }
            CHECK(spin->value() < curGb + 5.0 - 0.005 && std::abs(spin->value() - double(map->previewSize()) / GiB) < 0.02);

            // KHÓA chỉ thu nhỏ: ô nhập không vượt quá hiện tại; đang ở giá trị mở rộng thì bị kẹp về hiện tại
            spin->setValue(curGb + 5.0);
            dirShrink->click();
            CHECK(partTab->resizeDirection() == DiskLayout::ResizeDirection::ShrinkOnly && dirShrink->isChecked() && !dirFree->isChecked());
            CHECK(spin->maximum() <= curGb + 0.006 && spin->value() <= curGb + 0.006);
            CHECK(map->bounds().maxBytes == cur && map->previewSize() <= cur);
            spin->setValue(curGb - 3.0);
            confirm->setText(confirm->placeholderText());
            CHECK(resizeBtn->isEnabled() && partTab->resizeRequestBlockReason().isEmpty()); // thu nhỏ hợp hướng -> được phép đi tiếp
            // Chuyển sang CHỈ MỞ RỘNG khi đang ở giá trị THU NHỎ: giá trị bị kẹp lên hiện tại, không còn trái hướng
            dirGrow->click();
            CHECK(partTab->resizeDirection() == DiskLayout::ResizeDirection::GrowOnly && dirGrow->isChecked() && !dirShrink->isChecked());
            CHECK(spin->minimum() >= curGb - 0.006 && spin->value() >= curGb - 0.006 && map->bounds().minBytes == cur);
            CHECK(DiskLayout::directionBlockReason(spin->value(), cur, partTab->resizeDirection()).isEmpty());
            // Nút "Cả hai" trả khoảng về như Windows cho phép
            dirFree->click();
            CHECK(dirFree->isChecked() && spin->minimum() <= range.minBytes / double(GiB) + 0.006 && map->bounds().minBytes == range.minBytes);

            // Hướng đã khóa được GIỮ khi đổi sang phân vùng khác; hàng hướng ẩn lại cho tới khi tra
            dirShrink->click();
            const int other = (big + 1) % realParts.size();
            table->selectRow(other);
            app.processEvents();
            CHECK(!dirShrink->isVisibleTo(partTab) && !map->isResizeActive());
            CHECK(partTab->resizeDirection() == DiskLayout::ResizeDirection::ShrinkOnly && dirShrink->isChecked());
            dirFree->click(); // trả về mặc định cho các kiểm tra sau
            table->clearSelection();
            app.processEvents();
            CHECK(!partTab->isResizingNow());
        }
        else if (table && realParts.size() != table->rowCount())
        {
            std::printf("LUU Y: danh sach phan vung doi giua hai lan doc (%d vs %d) - bo qua kiem tra sau do\n", int(realParts.size()), table->rowCount());
        }
    }

    if (tabs) { tabs->setCurrentIndex(0); app.processEvents(); }

    if (qEnvironmentVariableIsSet("DISKCLEANUP_UI_SHOTS"))
    {
        const QString dir = qEnvironmentVariable("DISKCLEANUP_UI_SHOTS");
        const char* names[] = {"cleanup", "largefiles", "duplicates", "partitions"};
        for (int i = 0; i < 4 && tabs; ++i)
        {
            tabs->setCurrentIndex(i);
            app.processEvents();
            win.grab().save(dir + "/" + names[i] + ".png");
        }
        // Tab "Quản lý phân vùng" ở chế độ kéo (dữ liệu đĩa THẬT của máy chỉ để ĐỌC + khoảng kích thước GIẢ): chọn ổ D:
        if (partTab && tabs)
        {
            tabs->setCurrentIndex(3);
            auto* table = partTab->findChild<QTableWidget*>();
            if (table)
                for (int r = 0; r < table->rowCount(); ++r)
                    if (table->item(r, 0) && table->item(r, 0)->text() == "D:")
                    {
                        table->selectRow(r);
                        PartitionManager::SupportedSizeRange shotRange = fakeDRange;
                        partTab->applySupportedRangeForTest(shotRange);
                        if (auto* shotSpin = partTab->findChild<QDoubleSpinBox*>("partitionNewSizeSpin"))
                            shotSpin->setValue(shotSpin->value() - 15.0);
                        break;
                    }
            app.processEvents();
            win.grab().save(dir + "/partitions_resize.png");
        }
    }

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: quet hang muc/tep lon/tep trung lap va DOI KICH THUOC PHAN VUNG that can\n");
    std::printf("tu kiem tra tay tren may that - khong phu hop de chay trong test tu dong.\n");
    return g_fail == 0 ? 0 : 1;
}
