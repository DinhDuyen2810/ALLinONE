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
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QTabWidget>
#include <QTableView>
#include <QTableWidget>
#include <QTemporaryDir>
#include "TestDataDir.h"
#include <QTreeView>
#include <QTreeWidget>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>

#include <windows.h>

#include "core/AppPaths.h"

#include "tools/diskcleanup/CleanupTab.h"
#include "tools/diskcleanup/DiskCleanupWindow.h"
#include "tools/diskcleanup/DiskMapWidget.h"
#include "tools/diskcleanup/DuplicateFilesTab.h"
#include "tools/diskcleanup/FolderTreeModel.h"
#include "tools/diskcleanup/FolderTreeTab.h"
#include "tools/diskcleanup/LargeFilesTab.h"
#include "tools/diskcleanup/PartitionTab.h"
#include "tools/diskcleanup/ProcessTab.h"
#include "tools/diskcleanup/ProcessTableModel.h"

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
    CHECK(tabs != nullptr && tabs->count() == 6);

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

    // =====================================================================================
    // Tab "Cây thư mục" (quét + cây theo dung lượng) - chỉ quét thư mục tạm do chính test tạo.
    // =====================================================================================
    const auto waitFor = [&](const std::function<bool()>& cond, int timeoutMs) {
        QElapsedTimer t;
        t.start();
        while (!cond() && t.elapsed() < timeoutMs)
            app.processEvents(QEventLoop::AllEvents, 20);
        return cond();
    };

    // ---- FolderTreeModel với cây DỰNG TAY (không quét): thứ tự, dòng gộp, % của thư mục cha, đường dẫn ----
    {
        auto tree = std::make_shared<FolderTree>();
        tree->rootPath = "D:/Data";
        tree->stats.keepFolderBytes = 128 * 1024;
        tree->root = std::make_unique<FolderNode>();
        FolderNode* root = tree->root.get();
        root->name = "D:/Data";
        const auto addChild = [](FolderNode* parent, const QString& name, qint64 bytes) {
            auto c = std::make_unique<FolderNode>();
            c->name = name;
            c->parent = parent;
            c->bytes = bytes;
            c->logicalBytes = bytes;
            c->fileCount = 3;
            c->folderCount = 1;
            parent->children.push_back(std::move(c));
            return parent->children.back().get();
        };
        FolderNode* videos = addChild(root, "Videos", 600);
        addChild(root, "Docs", 100);
        FolderNode* locked = addChild(root, "Locked", 0);
        locked->unreadable = true;
        addChild(videos, "Trip", 400);
        // Tệp trực tiếp của gốc: 1 tệp lớn 150 + 5 tệp nhỏ (tổng 40) ; thư mục nhỏ đã gộp: 3 thư mục, tổng 60.
        root->ownBytes = 190;
        root->ownLogicalBytes = 190;
        root->ownFileCount = 6;
        root->bigFiles.push_back({"movie.mkv", 150, 150});
        root->prunedFolderCount = 3;
        root->prunedBytes = 60;
        root->bytes = 600 + 100 + 0 + 190 + 60; // = 950
        root->logicalBytes = root->bytes;
        root->fileCount = 20;
        root->folderCount = 9;
        videos->ownBytes = 200;
        videos->ownFileCount = 1; // đúng một tệp (a.mp4) - không còn "tệp nhỏ" nào để gộp
        videos->bigFiles.push_back({"a.mp4", 200, 200});

        FolderTreeModel model;
        CHECK(model.rowCount() == 0); // chưa có cây
        model.setTree(tree);
        CHECK(model.rowCount() == 1); // đúng một dòng gốc
        const QModelIndex top = model.index(0, 0);
        CHECK(top.isValid() && model.data(top, Qt::DisplayRole).toString() == "D:/Data");
        CHECK(model.pathAt(top) == "D:/Data" && model.hasChildren(top));
        CHECK(model.data(top, FolderTreeModel::ShareRole).toDouble() == 1.0);
        // Con của gốc, GIẢM DẦN theo dung lượng: Videos 600, Docs 100, [thư mục nhỏ] 60... xem thứ tự thật bên dưới.
        QStringList names;
        QList<qint64> sizes;
        for (int r = 0; r < model.rowCount(top); ++r)
        {
            const QModelIndex idx = model.index(r, 0, top);
            names << model.data(idx, Qt::DisplayRole).toString();
            sizes << model.bytesAt(idx);
        }
        CHECK(names.size() == 6); // Videos, movie.mkv, Docs, [5 tệp nhỏ]... + Locked + [3 thư mục nhỏ]
        bool descending = true;
        for (int i = 1; i < sizes.size(); ++i)
            descending = descending && sizes[i - 1] >= sizes[i];
        CHECK(descending);
        CHECK(names.value(0) == "Videos" && names.value(1) == "movie.mkv" && names.value(2) == "Docs");
        // Dòng gộp: 5 tệp nhỏ (bytes = 190 - 150 = 40) và 3 thư mục nhỏ (60); thư mục không đọc được hiện ghi chú đỏ + 0 byte.
        CHECK(names.contains("[5 tệp nhỏ]"));
        bool foundSmallFolders = false, foundLocked = false;
        for (const QString& n : names)
        {
            foundSmallFolders = foundSmallFolders || (n.startsWith("[3 thư mục nhỏ") && n.contains("128 KB"));
            foundLocked = foundLocked || (n.startsWith("Locked") && n.contains("không đọc được"));
        }
        CHECK(foundSmallFolders && foundLocked);
        CHECK(names.last().startsWith("Locked")); // 0 byte -> cuối cùng
        const QModelIndex videosIdx = model.index(0, 0, top);
        CHECK(model.kindAt(videosIdx) == FolderTreeModel::Kind::Folder && model.pathAt(videosIdx) == "D:/Data/Videos");
        CHECK(qAbs(model.data(videosIdx, FolderTreeModel::ShareRole).toDouble() - 600.0 / 950.0) < 1e-9);
        CHECK(model.data(model.index(0, FolderTreeModel::ShareColumn, top), Qt::DisplayRole).toString().endsWith("%"));
        const QModelIndex movieIdx = model.index(1, 0, top);
        CHECK(model.kindAt(movieIdx) == FolderTreeModel::Kind::File && model.pathAt(movieIdx) == "D:/Data/movie.mkv");
        CHECK(!model.hasChildren(movieIdx) && model.rowCount(movieIdx) == 0);
        // Cháu: Videos -> a.mp4 (200) và Trip (400) -> Trip trước; cha của cháu là Videos.
        CHECK(model.rowCount(videosIdx) == 2 && model.index(0, 0, videosIdx).data().toString() == "Trip");
        CHECK(model.parent(model.index(0, 0, videosIdx)) == videosIdx && model.parent(videosIdx) == top && !model.parent(top).isValid());
        CHECK(model.pathAt(model.index(1, 0, videosIdx)) == "D:/Data/Videos/a.mp4");
        // Dòng gộp: đường dẫn là THƯ MỤC chứa chúng (để "Mở trong Explorer" vẫn dùng được).
        for (int r = 0; r < model.rowCount(top); ++r)
            if (model.data(model.index(r, 0, top)).toString().startsWith("[5"))
                CHECK(model.pathAt(model.index(r, 0, top)) == "D:/Data" && model.kindAt(model.index(r, 0, top)) == FolderTreeModel::Kind::SmallFiles);
        model.clear();
        CHECK(model.rowCount() == 0 && !model.index(0, 0).isValid());
    }

    // ---- Tab thật: quét một cây thư mục tạm rồi xem kết quả ----
    auto* folderTab = win.findChild<FolderTreeTab*>();
    CHECK(folderTab != nullptr);
    CHECK(tabs && tabs->tabText(4).contains("Cây thư mục") && tabs->tabText(5).contains("Tiến trình"));
    if (tabs) { tabs->setCurrentIndex(4); app.processEvents(); }
    QTemporaryDir treeTmp;
    CHECK(treeTmp.isValid());
    if (folderTab && treeTmp.isValid())
    {
        const QString base = treeTmp.path() + "/scan";
        const auto writeBytes = [](const QString& path, qint64 size) {
            QFile f(path);
            f.open(QIODevice::WriteOnly);
            f.write(QByteArray(static_cast<int>(size), 'x'));
        };
        QDir().mkpath(base + "/big");
        QDir().mkpath(base + "/mid");
        QDir().mkpath(base + "/small");
        for (int i = 0; i < 3; ++i)
            writeBytes(base + "/big/b" + QString::number(i) + ".bin", 2 * 1024 * 1024);
        writeBytes(base + "/mid/m.bin", 1536 * 1024);
        for (int d = 0; d < 40; ++d) // nhiều tệp nhỏ (1000 x 200 byte ~ 200 KB, vượt ngưỡng gộp 128 KB của thư mục "small")
        {
            QDir().mkpath(base + "/small/d" + QString::number(d));
            for (int f = 0; f < 25; ++f)
                writeBytes(base + "/small/d" + QString::number(d) + "/f" + QString::number(f) + ".txt", 200);
        }
        writeBytes(base + "/rootfile.bin", 300 * 1024);

        auto* scanBtn = folderTab->findChild<QPushButton*>("treeScanButton");
        auto* stopBtn = folderTab->findChild<QPushButton*>("treeStopButton");
        auto* heaviestBtn = folderTab->findChild<QPushButton*>("treeHeaviestButton");
        auto* status = folderTab->findChild<QLabel*>("treeStatusLabel");
        auto* summary = folderTab->findChild<QLabel*>("treeSummaryLabel");
        auto* view = folderTab->findChild<QTreeView*>("folderTreeView");
        CHECK(scanBtn && stopBtn && heaviestBtn && status && summary && view);
        if (scanBtn && stopBtn && heaviestBtn && status && summary && view)
        {
            CHECK(!folderTab->isScanningNow() && !stopBtn->isVisibleTo(folderTab) && scanBtn->isVisibleTo(folderTab));
            CHECK(!heaviestBtn->isEnabled() && !summary->isVisibleTo(folderTab)); // chưa quét
            CHECK(view->model()->rowCount() == 0);

            folderTab->setScanRoot(base);
            scanBtn->click();
            // Đang quét: nút Dừng thay nút Quét, tab khác trong cửa sổ bị chặn bắt đầu thao tác đụng đĩa (nếu lượt quét
            // nhanh tới mức đã xong thì bỏ qua - không để test phụ thuộc tốc độ máy).
            if (folderTab->isScanningNow())
            {
                // Giao diện chỉ đổi trạng thái khi xử lý sự kiện nên chắc chắn đang ở trạng thái "đang quét" ở đây.
                CHECK(stopBtn->isVisibleTo(folderTab) && !scanBtn->isVisibleTo(folderTab));
                const bool runningBefore = folderTab->isScanningNow();
                const bool busyForOthers = win.isAnyOtherTabBusy(largeFilesTab);
                const bool busyForSelf = win.isAnyOtherTabBusy(folderTab);
                if (runningBefore && folderTab->isScanningNow()) // luồng chắc chắn chạy suốt khoảng đo
                {
                    CHECK(busyForOthers);
                    CHECK(!busyForSelf); // chính nó không tự chặn mình
                }
            }
            CHECK(waitFor([&]() { return status->text().startsWith("✓ Đã quét"); }, 30000));
            CHECK(!folderTab->isScanningNow() && scanBtn->isVisibleTo(folderTab) && !stopBtn->isVisibleTo(folderTab));
            CHECK(summary->isVisibleTo(folderTab) && summary->text().contains("Quét xong"));
            // 1005 tệp + 43 thư mục (định dạng số theo ngôn ngữ máy: 1005 / 1.005 / 1,005).
            CHECK(summary->text().contains(QRegularExpression("1[.,]?005 tệp")) && summary->text().contains("43 thư mục"));

            auto* model = view->model();
            CHECK(model->rowCount() == 1);
            const QModelIndex top = model->index(0, 0);
            CHECK(top.isValid() && view->isExpanded(top)); // gốc tự mở sẵn
            CHECK(model->data(top).toString() == QDir::cleanPath(base));
            // Con của gốc giảm dần: big (6 MB) > mid (1,5 MB) > tệp rootfile (300 KB, gộp "tệp nhỏ") > small (~200 KB, nhiều tệp nhỏ).
            QStringList order;
            for (int r = 0; r < model->rowCount(top); ++r)
                order << model->index(r, 0, top).data().toString();
            CHECK(order == QStringList({"big", "mid", "[1 tệp nhỏ]", "small"}));
            CHECK(heaviestBtn->isEnabled());

            // "Mở nhánh nặng nhất": big chiếm > 15% -> mở và chọn nó; con đầu của big là TỆP nên dừng ở big.
            heaviestBtn->click();
            app.processEvents();
            CHECK(view->currentIndex().isValid() && view->currentIndex().data().toString() == "big");
            CHECK(view->isExpanded(view->currentIndex()));
            if (qEnvironmentVariableIsSet("DISKCLEANUP_UI_SHOTS"))
                win.grab().save(qEnvironmentVariable("DISKCLEANUP_UI_SHOTS") + "/foldertree.png");
            // Thu gọn: chỉ còn gốc mở.
            for (QPushButton* b : folderTab->findChildren<QPushButton*>())
                if (b->text() == "Thu gọn")
                    b->click();
            app.processEvents();
            CHECK(view->isExpanded(top) && !view->isExpanded(model->index(0, 0, top)));
            // Mở rộng 1 cấp: mở thêm các thư mục con trực tiếp.
            for (QPushButton* b : folderTab->findChildren<QPushButton*>())
                if (b->text() == "Mở rộng 1 cấp")
                    b->click();
            app.processEvents();
            CHECK(view->isExpanded(model->index(0, 0, top)));

            // Nút "Mở trong Explorer"/"Sao chép đường dẫn" chỉ bật khi có dòng được chọn (KHÔNG bấm - không mở cửa sổ/đổi clipboard).
            bool anyEnabled = false;
            for (QPushButton* b : folderTab->findChildren<QPushButton*>())
                if (b->text().contains("Mở trong Explorer") || b->text().contains("Sao chép đường dẫn"))
                    anyEnabled = anyEnabled || b->isEnabled();
            CHECK(anyEnabled);

            // Quét lại: kết quả cũ được thay, không nhân đôi.
            scanBtn->click();
            CHECK(waitFor([&]() { return !folderTab->isScanningNow() && status->text().startsWith("✓ Đã quét"); }, 30000));
            CHECK(view->model()->rowCount() == 1);
            // Dừng khi không quét: không làm gì, không crash.
            folderTab->stopScanIfRunning();
            CHECK(!folderTab->isScanningNow());
        }
    }

    // =====================================================================================
    // Tab "Tiến trình & RAM"
    // =====================================================================================
    // ---- ProcessTableModel với mẫu GIẢ: gộp theo tên, cập nhật tại chỗ / dựng lại, sắp xếp, định dạng ----
    {
        SystemSnapshot snap;
        snap.memory.totalPhysical = 8LL * 1024 * 1024 * 1024;
        snap.memory.availablePhysical = 4LL * 1024 * 1024 * 1024;
        const auto proc = [](quint32 pid, const QString& name, double cpu, qint64 ramMb, const QString& user) {
            ProcessInfo p;
            p.pid = pid;
            p.parentPid = 4;
            p.name = name;
            p.path = "C:/Windows/System32/" + name;
            p.user = user;
            p.cpuPercent = cpu;
            p.privateWorkingSetBytes = ramMb * 1024 * 1024;
            p.workingSetBytes = p.privateWorkingSetBytes + 10 * 1024 * 1024;
            p.privateBytes = p.privateWorkingSetBytes * 2;
            p.threads = 10;
            p.handles = 100;
            p.ioBytesPerSec = 2 * 1024 * 1024;
            p.startTime = QDateTime::fromSecsSinceEpoch(1700000000 + pid);
            return p;
        };
        snap.processes = {proc(100, "svchost.exe", 1.0, 50, "NT AUTHORITY\\SYSTEM"), proc(200, "svchost.exe", 2.0, 30, "NT AUTHORITY\\LOCAL SERVICE"),
                          proc(300, "chrome.exe", 55.0, 900, "PC\\An"), proc(0, "System Idle Process", 90.0, 0, "NT AUTHORITY\\SYSTEM")};
        snap.processes[3].workingSetBytes = 8192;
        snap.processes[3].privateWorkingSetBytes = 0;

        ProcessTableModel m;
        CHECK(m.update(snap, false)); // lần đầu: dựng lại
        CHECK(m.rowCount() == 4 && m.columnCount() == ProcessTableModel::ColumnCount);
        CHECK(m.data(m.index(2, ProcessTableModel::NameColumn)).toString() == "chrome.exe");
        CHECK(m.data(m.index(2, ProcessTableModel::PidColumn)).toString() == "300"); // không có dấu phân cách hàng nghìn
        CHECK(m.data(m.index(2, ProcessTableModel::RamColumn)).toString() == "900.0 MB");
        CHECK(m.data(m.index(2, ProcessTableModel::CpuColumn), ProcessTableModel::SortRole).toDouble() == 55.0);
        CHECK(m.data(m.index(2, ProcessTableModel::RamColumn), ProcessTableModel::SortRole).toLongLong() == 900LL * 1024 * 1024);
        CHECK(m.data(m.index(3, ProcessTableModel::RamColumn), ProcessTableModel::SortRole).toLongLong() == 8192); // không có RAM riêng -> working set
        CHECK(ProcessTableModel::formatCpu(0.0) == "0 %" && ProcessTableModel::formatCpu(12.34).startsWith("12") && ProcessTableModel::formatCpu(12.34).endsWith("%"));
        CHECK(ProcessTableModel::formatRate(0) == "0" && ProcessTableModel::formatRate(1023) == "0" && ProcessTableModel::formatRate(3 * 1024 * 1024) == "3.0 MB/s");
        // Bản đồ nhiệt: chrome (CPU 55% -> đỏ; RAM 900 MB / 8 GB = 11% -> vàng); tiến trình Idle không tô màu.
        const QBrush cpuHeat = qvariant_cast<QBrush>(m.data(m.index(2, ProcessTableModel::CpuColumn), Qt::BackgroundRole));
        const QBrush ramHeat = qvariant_cast<QBrush>(m.data(m.index(2, ProcessTableModel::RamColumn), Qt::BackgroundRole));
        CHECK(cpuHeat.style() != Qt::NoBrush && cpuHeat.color().red() > cpuHeat.color().green());
        CHECK(ramHeat.style() != Qt::NoBrush);
        CHECK(!m.data(m.index(3, ProcessTableModel::CpuColumn), Qt::BackgroundRole).isValid());
        CHECK(m.data(m.index(0, ProcessTableModel::NameColumn), Qt::ToolTipRole).toString().contains("PID 100"));
        CHECK(m.data(m.index(0, ProcessTableModel::RamColumn), Qt::ToolTipRole).toString().contains("Working set"));

        // Cùng tập tiến trình -> cập nhật TẠI CHỖ (không reset) và báo dataChanged.
        int dataChanged = 0, resets = 0;
        QObject::connect(&m, &QAbstractItemModel::dataChanged, [&]() { ++dataChanged; });
        QObject::connect(&m, &QAbstractItemModel::modelReset, [&]() { ++resets; });
        snap.processes[2].cpuPercent = 10.0;
        CHECK(!m.update(snap, false) && dataChanged == 1 && resets == 0);
        CHECK(m.data(m.index(2, ProcessTableModel::CpuColumn), ProcessTableModel::SortRole).toDouble() == 10.0);
        // Tiến trình thoát -> dựng lại.
        SystemSnapshot fewer = snap;
        fewer.processes.removeAt(1);
        CHECK(m.update(fewer, false) && resets == 1 && m.rowCount() == 3);
        CHECK(m.update(snap, false) && resets == 2 && m.rowCount() == 4);

        // Gộp theo tên: 2 svchost -> 1 dòng "(×2)", cộng số liệu, PID = 0, nhiều người dùng.
        CHECK(m.update(snap, true) && m.grouped() && m.rowCount() == 3);
        CHECK(m.data(m.index(0, ProcessTableModel::NameColumn)).toString().contains("svchost.exe") && m.data(m.index(0, ProcessTableModel::NameColumn)).toString().contains("×2"));
        CHECK(m.data(m.index(0, ProcessTableModel::PidColumn)).toString().isEmpty() && m.data(m.index(0, 0), ProcessTableModel::PidRole).toUInt() == 0);
        CHECK(m.data(m.index(0, ProcessTableModel::RamColumn), ProcessTableModel::SortRole).toLongLong() == 80LL * 1024 * 1024);
        CHECK(m.data(m.index(0, ProcessTableModel::CpuColumn), ProcessTableModel::SortRole).toDouble() == 3.0);
        CHECK(m.data(m.index(0, ProcessTableModel::ThreadsColumn), ProcessTableModel::SortRole).toLongLong() == 20);
        CHECK(m.data(m.index(0, ProcessTableModel::UserColumn)).toString().contains("nhiều"));
        CHECK(m.data(m.index(1, ProcessTableModel::NameColumn)).toString() == "chrome.exe"); // một tiến trình: không có "(×N)"
        CHECK(m.data(m.index(1, ProcessTableModel::PidColumn)).toString() == "300");
        CHECK(!m.update(snap, true)); // gộp rồi, cùng tập -> tại chỗ
        CHECK(m.update(snap, false)); // đổi chế độ -> dựng lại
        CHECK(m.rowCount() == 4);
    }

    // ---- Tab thật: lấy mẫu CHỈ khi tab đang hiện ----
    auto* procTab = win.findChild<ProcessTab*>();
    auto* sampler = win.findChild<ProcessSamplerThread*>("processSampler");
    CHECK(procTab != nullptr && sampler != nullptr);
    if (tabs) { tabs->setCurrentIndex(0); app.processEvents(); }
    if (procTab && sampler)
    {
        QElapsedTimer idle;
        idle.start();
        while (idle.elapsed() < 700)
            app.processEvents(QEventLoop::AllEvents, 20);
        CHECK(sampler->isRunning() && !sampler->isActive() && sampler->sampleCount() == 0); // tab ẩn -> luồng ngủ

        auto* table = procTab->findChild<QTableView*>("processTable");
        auto* filter = procTab->findChild<QLineEdit*>("processFilter");
        auto* group = procTab->findChild<QCheckBox*>("processGroupCheck");
        auto* pause = procTab->findChild<QCheckBox*>("processPauseCheck");
        auto* intervalCombo = procTab->findChild<QComboBox*>("processIntervalCombo");
        auto* countLabel = procTab->findChild<QLabel*>("processCountLabel");
        auto* ramCard = procTab->findChild<QFrame*>("ramCard");
        auto* cpuCard = procTab->findChild<QFrame*>("cpuCard");
        auto* history = procTab->findChild<QWidget*>("usageHistory");
        CHECK(table && filter && group && pause && intervalCombo && countLabel && ramCard && cpuCard && history);
        if (table && filter && group && pause && intervalCombo && countLabel && ramCard && cpuCard && history)
        {
            CHECK(table->model()->rowCount() == 0);
            CHECK(intervalCombo->count() == 3 && intervalCombo->currentData().toInt() == 2000);
            intervalCombo->setCurrentIndex(0); // 1 giây - để test nhanh hơn
            CHECK(sampler->isRunning());

            if (tabs) { tabs->setCurrentIndex(5); app.processEvents(); }
            CHECK(waitFor([&]() { return table->model()->rowCount() > 20; }, 15000));
            CHECK(sampler->isActive() && sampler->sampleCount() >= 1);
            const int total = table->model()->rowCount();
            // Thẻ chỉ số có nội dung thật: RAM "x / y", mô tả "Đang dùng".
            auto* ramValue = ramCard->findChild<QLabel*>("meterValue");
            auto* ramSub = ramCard->findChild<QLabel*>("meterSub");
            CHECK(ramValue && ramValue->text().contains("/") && ramSub && ramSub->text().contains("Đang dùng"));
            CHECK(cpuCard->findChild<QLabel*>("meterSub")->text().contains("lõi logic"));
            CHECK(history->height() > 50);

            // Mặc định sắp theo RAM giảm dần.
            const auto sortValue = [&](int row) { return table->model()->index(row, ProcessTableModel::RamColumn).data(ProcessTableModel::SortRole).toLongLong(); };
            bool descending = true;
            for (int r = 1; r < qMin(40, total); ++r)
                descending = descending && sortValue(r - 1) >= sortValue(r);
            CHECK(descending);

            // Tiến trình của CHÍNH test có trong bảng, đúng tên tệp thực thi.
            const quint32 selfPid = GetCurrentProcessId();
            int selfRow = -1;
            const auto findSelf = [&]() {
                for (int r = 0; r < table->model()->rowCount(); ++r)
                    if (table->model()->index(r, 0).data(ProcessTableModel::PidRole).toUInt() == selfPid)
                        return r;
                return -1;
            };
            selfRow = findSelf();
            CHECK(selfRow >= 0);
            const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
            CHECK(selfRow >= 0 && table->model()->index(selfRow, ProcessTableModel::NameColumn).data().toString().compare(exeName, Qt::CaseInsensitive) == 0);

            // Lựa chọn được GIỮ khi bảng làm mới (chọn dòng của mình, chờ thêm mẫu mới, vẫn đang chọn đúng PID).
            if (selfRow >= 0)
            {
                table->setCurrentIndex(table->model()->index(selfRow, 0));
                table->selectRow(selfRow);
                const int before = sampler->sampleCount();
                CHECK(waitFor([&]() { return sampler->sampleCount() >= before + 2; }, 10000));
                app.processEvents();
                const QModelIndex cur = table->currentIndex();
                CHECK(cur.isValid() && cur.data(ProcessTableModel::PidRole).toUInt() == selfPid);
            }

            // Lọc: theo tên tệp thực thi của test -> còn ít dòng; chuỗi vô lý -> 0; xóa -> đủ như cũ.
            filter->setText(exeName.left(exeName.size() - 4));
            app.processEvents();
            CHECK(table->model()->rowCount() >= 1 && table->model()->rowCount() < total / 2);
            CHECK(countLabel->text().startsWith(QString::number(table->model()->rowCount()) + " /"));
            filter->setText("zzz_khong_co_tien_trinh_nao_ten_nhu_the_zzz");
            app.processEvents();
            CHECK(table->model()->rowCount() == 0);
            filter->clear();
            app.processEvents();
            CHECK(table->model()->rowCount() >= total - 5); // có thể đổi vài tiến trình giữa hai mẫu

            // Gộp theo tên: ít dòng hơn (luôn có nhiều svchost.exe), dòng gộp có "(×N)"; bỏ gộp -> trở lại.
            group->setChecked(true);
            app.processEvents();
            const int groupedRows = table->model()->rowCount();
            CHECK(groupedRows < total);
            bool sawGroup = false;
            for (int r = 0; r < groupedRows; ++r)
            {
                const QString n = table->model()->index(r, ProcessTableModel::NameColumn).data().toString();
                sawGroup = sawGroup || (n.startsWith("svchost.exe") && n.contains("×"));
            }
            CHECK(sawGroup);
            if (qEnvironmentVariableIsSet("DISKCLEANUP_UI_SHOTS"))
                win.grab().save(qEnvironmentVariable("DISKCLEANUP_UI_SHOTS") + "/processes_grouped.png");
            group->setChecked(false);
            app.processEvents();
            CHECK(table->model()->rowCount() >= total - 5);
            if (qEnvironmentVariableIsSet("DISKCLEANUP_UI_SHOTS"))
                win.grab().save(qEnvironmentVariable("DISKCLEANUP_UI_SHOTS") + "/processes.png");

            // Tạm dừng: luồng ngủ, số mẫu đứng yên; bỏ tạm dừng: chạy tiếp.
            pause->setChecked(true);
            CHECK(waitFor([&]() { return !sampler->isActive(); }, 3000));
            QElapsedTimer settle;
            settle.start();
            while (settle.elapsed() < 400)
                app.processEvents(QEventLoop::AllEvents, 20);
            const int frozen = sampler->sampleCount();
            settle.restart();
            while (settle.elapsed() < 1500)
                app.processEvents(QEventLoop::AllEvents, 20);
            CHECK(sampler->sampleCount() == frozen);
            pause->setChecked(false);
            CHECK(waitFor([&]() { return sampler->sampleCount() > frozen; }, 10000));

            // Ẩn tab -> luồng ngủ ngay; hiện lại -> chạy tiếp.
            if (tabs) { tabs->setCurrentIndex(0); app.processEvents(); }
            CHECK(waitFor([&]() { return !sampler->isActive(); }, 3000));
            if (tabs) { tabs->setCurrentIndex(5); app.processEvents(); }
            CHECK(waitFor([&]() { return sampler->isActive(); }, 3000));

            // Thanh RAM đọc nền: nhãn hiện khi đọc được (máy ảo có thể không có -> chỉ báo, không coi là lỗi).
            auto* modulesLabel = procTab->findChild<QLabel*>("ramModulesLabel");
            CHECK(modulesLabel != nullptr);
            if (modulesLabel)
            {
                const bool shown = waitFor([&]() { return modulesLabel->isVisibleTo(procTab); }, 20000);
                if (shown)
                    CHECK(modulesLabel->text().contains("thanh RAM"));
                else
                    std::printf("LUU Y: khong doc duoc thong tin thanh RAM (may ao?) - nhan khong hien\n");
            }

            // Thoát ứng dụng: dừng HẲN luồng lấy mẫu, mở lại tab cũng không chạy lại.
            win.stopSafelyCancellableWorkForQuit();
            CHECK(!sampler->isRunning());
            if (tabs) { tabs->setCurrentIndex(0); app.processEvents(); tabs->setCurrentIndex(5); app.processEvents(); }
            CHECK(!sampler->isRunning() && !sampler->isActive());
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
