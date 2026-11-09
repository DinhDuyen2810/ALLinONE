// Kiểm thử "khói" cho giao diện Disk Cleanup: dựng cửa sổ, không crash, các widget chính tồn tại.
// KHÔNG bấm nút Quét thật ở CleanupTab/LargeFilesTab/DuplicateFilesTab: hạng mục dọn dẹp trỏ vào
// %TEMP%/%WINDIR% thật và "Tìm tệp lớn"/"Tìm tệp trùng lặp" cần người dùng chọn thư mục trước - đụng
// vào hệ thống thật của máy build, không phù hợp cho test tự động (giống nguyên tắc đã áp dụng cho
// wifi_ui_tests: quét/kết nối thật phải tự kiểm tra tay). PartitionTab thì khác: liệt kê phân vùng
// (listPartitions) chỉ ĐỌC và AN TOÀN (giống DiskSpaceInfo::listDrives()), nên được gọi thật ngay khi
// dựng UI - test xác nhận nó thực sự chạy được trên máy build. KHÔNG bao giờ test đổi kích thước thật.
// Build: cmake --build build --target diskcleanup_ui_tests && build\diskcleanup_ui_tests.exe
#include <QApplication>
#include <QCoreApplication>
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
#include "tools/diskcleanup/engine/PartitionMover.h"
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

    // ---- Sơ đồ đĩa: chế độ DỜI (tay nắm mép trái, kéo cả khối, đổi thứ tự) trên dữ liệu GIẢ - KHÔNG PowerShell, KHÔNG đụng đĩa ----
    // Bố cục: EFI | C: 100 GiB | [20 GiB trống] | D: 40 GiB | [30 GiB trống] | E: 10 GiB
    const qint64 cEnd2 = (101 + 100 * 1024) * MiB;
    const qint64 d2Start = cEnd2 + 20 * GiB, d2Size = 40 * GiB, d2End = d2Start + d2Size, e2Start = d2End + 30 * GiB;
    PartitionManager::DiskInfo moveDisk;
    moveDisk.number = 0;
    moveDisk.sizeBytes = e2Start + 10 * GiB + 2 * MiB;
    moveDisk.partitionStyle = "GPT";
    const QList<PartitionManager::PartitionInfo> moveParts = {
        fakePart(1, "System", 1, 100, "", "", "FAT32"), fakePart(2, "Basic", 101, 100 * 1024, "C", "Windows", "NTFS"),
        fakePart(3, "Basic", d2Start / MiB, d2Size / MiB, "D", "Data", "NTFS"), fakePart(4, "Basic", e2Start / MiB, 10 * 1024, "E", "Extra", "NTFS")};
    PartitionManager::SupportedSizeRange moveRange; // Windows cho D: co 10 GiB .. 40 + 30 (vùng trống bên phải) GiB
    moveRange.ok = true;
    moveRange.minBytes = 10 * GiB;
    moveRange.maxBytes = 70 * GiB;
    {
        DiskMapWidget map;
        map.setData({moveDisk}, moveParts);
        map.resize(1000, map.sizeHint().height());
        auto mouse = [&](QEvent::Type type, QPoint p, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent e(type, p, map.mapToGlobal(p), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(&map, &e);
        };
        auto press = [&](QPoint p) { mouse(QEvent::MouseButtonPress, p, Qt::LeftButton, Qt::LeftButton); };
        auto move = [&](QPoint p) { mouse(QEvent::MouseMove, p, Qt::NoButton, Qt::LeftButton); };
        auto release = [&](QPoint p) { mouse(QEvent::MouseButtonRelease, p, Qt::LeftButton, Qt::NoButton); };

        map.setResizeContext(0, 3, moveRange, DiskLayout::ResizeDirection::Free);
        CHECK(map.isResizeActive() && map.currentStart() == d2Start && map.previewStart() == d2Start && !map.isMoveEnabled());
        CHECK(map.leftHandleRectForTest().isNull()); // chưa bật dời: CHỈ có tay nắm mép phải
        int geoSignals = 0, sizeSignals = 0;
        qint64 lastStart = -1, lastSize = -1;
        QObject::connect(&map, &DiskMapWidget::previewGeometryChanged, [&](qint64 st, qint64 sz) { ++geoSignals; lastStart = st; lastSize = sz; });
        QObject::connect(&map, &DiskMapWidget::previewSizeChanged, [&](qint64) { ++sizeSignals; });

        // Kéo mép phải khi chưa dời: tín hiệu hình học phát kèm vị trí hiện tại (đường cũ không đổi hành vi).
        {
            const QRect rh = map.handleRectForTest();
            press(rh.center());
            move(rh.center() + QPoint(-15, 0));
            release(rh.center() + QPoint(-15, 0));
            CHECK(map.previewSize() < d2Size && map.previewStart() == d2Start && geoSignals >= 1 && lastStart == d2Start && lastSize == map.previewSize() && sizeSignals >= 1);
            map.setPreviewSize(d2Size);
            geoSignals = sizeSignals = 0;
        }

        map.setMoveEnabled(true);
        CHECK(map.isMoveEnabled());
        const QRect dBlock = map.blockRectForTest(0, 3);
        QRect lh = map.leftHandleRectForTest();
        CHECK(!lh.isNull() && std::abs(lh.center().x() - dBlock.left()) <= 1); // tay nắm nằm ở MÉP TRÁI của D:
        CHECK(map.diskSizeOfTarget() == moveDisk.sizeBytes);
        const auto span = map.targetSpan();
        CHECK(span.lo == cEnd2 && span.hi == e2Start); // chỗ cũ + 20 GiB trống bên trái + 30 GiB trống bên phải

        // --- Kéo mép TRÁI sang TRÁI: mở rộng về bên trái, mép phải giữ nguyên, kẹp ở mép phải của C: ---
        press(lh.center());
        move(lh.center() + QPoint(-20, 0));
        CHECK(map.previewStart() < d2Start && map.previewStart() >= cEnd2 && map.previewStart() + map.previewSize() == d2End);
        CHECK(map.previewStart() % MiB == 0 && geoSignals >= 1 && lastStart == map.previewStart() && lastSize == map.previewSize());
        CHECK(map.previewSize() > d2Size);
        move(lh.center() + QPoint(-1000, 0));
        CHECK(map.previewStart() == cEnd2 && map.previewSize() == d2End - cEnd2); // hết vùng trống bên trái
        move(lh.center() + QPoint(1000, 0)); // kéo quá bên phải: kích thước không nhỏ hơn mức tối thiểu của Windows
        CHECK(map.previewSize() == moveRange.minBytes && map.previewStart() == d2End - moveRange.minBytes);
        move(lh.center());                  // về đúng chỗ cũ = chưa đổi (giữ nguyên giá trị cũ)
        CHECK(map.previewStart() == d2Start && map.previewSize() == d2Size);
        release(lh.center());
        const int sigAfter = geoSignals;
        move(lh.center() + QPoint(-100, 0)); // đã nhả chuột
        CHECK(geoSignals == sigAfter && map.previewStart() == d2Start);

        // --- Khóa hướng: chỉ thu nhỏ -> mép trái chỉ đi sang PHẢI; chỉ mở rộng -> mép trái chỉ đi sang TRÁI ---
        map.setDirection(DiskLayout::ResizeDirection::ShrinkOnly);
        lh = map.leftHandleRectForTest();
        press(lh.center());
        move(lh.center() + QPoint(-200, 0));
        CHECK(map.previewStart() == d2Start && map.previewSize() == d2Size); // sang trái bị chặn
        move(lh.center() + QPoint(40, 0));
        CHECK(map.previewStart() > d2Start && map.previewStart() + map.previewSize() == d2End && map.previewSize() < d2Size);
        release(lh.center());
        map.setPreviewGeometry(d2Start, d2Size);
        map.setDirection(DiskLayout::ResizeDirection::GrowOnly);
        lh = map.leftHandleRectForTest();
        press(lh.center());
        move(lh.center() + QPoint(200, 0));
        CHECK(map.previewStart() == d2Start && map.previewSize() == d2Size); // sang phải bị chặn
        move(lh.center() + QPoint(-30, 0));
        CHECK(map.previewStart() < d2Start && map.previewSize() > d2Size);
        release(lh.center());
        map.setDirection(DiskLayout::ResizeDirection::Free);
        map.setPreviewGeometry(d2Start, d2Size);
        CHECK(map.previewStart() == d2Start && map.previewSize() == d2Size);

        // --- Kéo CẢ KHỐI: dời giữ nguyên kích thước, kẹp trong vùng trống liền kề hai bên ---
        {
            const QRect body = map.blockRectForTest(0, 3);
            press(body.center());
            move(body.center() + QPoint(15, 0));
            CHECK(map.previewStart() > d2Start && map.previewSize() == d2Size && map.previewStart() % MiB == 0);
            move(body.center() + QPoint(2000, 0));
            CHECK(map.previewStart() == e2Start - d2Size && map.previewSize() == d2Size); // sát E: (kẹp ở cuối vùng trống bên phải)
            move(body.center() + QPoint(-2000, 0));
            CHECK(map.previewStart() == cEnd2 && map.previewSize() == d2Size);            // sát C: (kẹp ở đầu vùng trống bên trái)
            move(body.center());
            CHECK(map.previewStart() == d2Start);                                         // về chỗ cũ
            release(body.center());
        }
        // Kéo khối KHÔNG xảy ra khi đã đổi kích thước (preview != hiện tại) hoặc khi khóa hướng.
        {
            map.setPreviewSize(d2Size - 5 * GiB);
            const QRect body = map.blockRectForTest(0, 3);
            press(body.center());
            move(body.center() + QPoint(40, 0));
            release(body.center());
            CHECK(map.previewStart() == d2Start);
            map.setPreviewSize(d2Size);
            map.setDirection(DiskLayout::ResizeDirection::ShrinkOnly);
            press(map.blockRectForTest(0, 3).center());
            move(map.blockRectForTest(0, 3).center() + QPoint(40, 0));
            release(map.blockRectForTest(0, 3).center());
            CHECK(map.previewStart() == d2Start);
            map.setDirection(DiskLayout::ResizeDirection::Free);
        }

        // --- Dời xong rồi kéo mép PHẢI: giới hạn là hết vùng trống của khe (không còn là mức Windows cho vị trí cũ) ---
        map.setPreviewGeometry(cEnd2, d2Size); // D: sát C:
        CHECK(map.previewStart() == cEnd2 && map.previewSize() == d2Size);
        {
            const QRect rh = map.handleRectForTest();
            press(rh.center());
            move(rh.center() + QPoint(5000, 0));
            release(rh.center());
            CHECK(map.previewStart() == cEnd2 && map.previewSize() == e2Start - cEnd2); // lấn hết cả hai vùng trống tới E:
        }
        map.setPreviewGeometry(d2Start, d2Size);

        // --- setPreviewGeometry: đặt KHÔNG phát tín hiệu; hình học sai (chồng phân vùng khác, ngoài đĩa, không căn lề) bị bỏ ---
        geoSignals = 0;
        map.setPreviewGeometry(d2Start + 5 * GiB, d2Size);
        CHECK(map.previewStart() == d2Start + 5 * GiB && geoSignals == 0);
        map.setPreviewGeometry(cEnd2 - GiB, d2Size); // lấn vào C:
        CHECK(map.previewStart() == d2Start + 5 * GiB);
        map.setPreviewGeometry(d2Start + 5 * GiB + 4096, d2Size); // lệch căn lề 1 MiB
        CHECK(map.previewStart() == d2Start + 5 * GiB);
        map.setPreviewGeometry(moveDisk.sizeBytes, d2Size); // ngoài đĩa
        CHECK(map.previewStart() == d2Start + 5 * GiB);
        map.setPreviewGeometry(d2Start, d2Size);
        CHECK(map.previewStart() == d2Start);

        // --- Tắt chế độ dời giữa lúc kéo: kéo dừng, tay nắm trái biến mất, kéo khối không còn ---
        lh = map.leftHandleRectForTest();
        press(lh.center());
        map.setMoveEnabled(false, "thử");
        move(lh.center() + QPoint(-30, 0));
        CHECK(map.previewStart() == d2Start && map.leftHandleRectForTest().isNull());
        release(lh.center());

        // --- Có ảnh chụp khi cần xem ---
        if (qEnvironmentVariableIsSet("DISKCLEANUP_UI_SHOTS"))
        {
            const QString dir = qEnvironmentVariable("DISKCLEANUP_UI_SHOTS");
            map.setSelection(0, 3);
            map.setMoveEnabled(true);
            map.setPreviewGeometry(cEnd2, d2End - cEnd2);
            map.grab().save(dir + "/diskmap_left_grow.png");
            map.setPreviewGeometry(d2Start + 20 * GiB, d2Size);
            map.grab().save(dir + "/diskmap_move_right.png");
        }
    }

    // ---- Đổi THỨ TỰ: kéo khối sang một vùng chưa phân bổ KHÁC (không liền kề) ----
    // Bố cục: A 10 GiB | [5 GiB trống] | B 20 GiB | [30 GiB trống]
    {
        PartitionManager::DiskInfo d3;
        d3.number = 0;
        d3.partitionStyle = "GPT";
        const qint64 aStart = 1 * MiB, aSize = 10 * GiB, bStart = aStart + aSize + 5 * GiB, bSize = 20 * GiB, free2 = bStart + bSize;
        d3.sizeBytes = free2 + 30 * GiB + 2 * MiB;
        const QList<PartitionManager::PartitionInfo> parts3 = {fakePart(1, "Basic", aStart / MiB, aSize / MiB, "A", "A", "NTFS"),
                                                              fakePart(2, "Basic", bStart / MiB, bSize / MiB, "B", "B", "NTFS")};
        PartitionManager::SupportedSizeRange r3;
        r3.ok = true;
        r3.minBytes = 1 * GiB;
        r3.maxBytes = aSize + 5 * GiB;
        DiskMapWidget map;
        map.setData({d3}, parts3);
        map.resize(1000, map.sizeHint().height());
        auto mouse = [&](QEvent::Type type, QPoint p, Qt::MouseButton button, Qt::MouseButtons buttons) {
            QMouseEvent e(type, p, map.mapToGlobal(p), button, buttons, Qt::NoModifier);
            QApplication::sendEvent(&map, &e);
        };
        map.setResizeContext(0, 1, r3, DiskLayout::ResizeDirection::Free);
        map.setMoveEnabled(true);
        const QRect aBlock = map.blockRectForTest(0, 1);
        const QRect free2Rect = map.unallocatedRectForTest(0, 1); // vùng trống thứ 2 (30 GiB, sau B)
        CHECK(!aBlock.isNull() && !free2Rect.isNull() && free2Rect.left() > map.blockRectForTest(0, 2).right());
        mouse(QEvent::MouseButtonPress, aBlock.center(), Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseMove, QPoint(free2Rect.center().x(), aBlock.center().y()), Qt::NoButton, Qt::LeftButton);
        CHECK(map.previewStart() >= free2 && map.previewStart() + aSize <= d3.sizeBytes && map.previewSize() == aSize);
        CHECK(map.previewStart() > bStart); // A: nhảy qua B: sang vùng trống sau B: = đổi thứ tự
        mouse(QEvent::MouseMove, QPoint(free2Rect.right() + 400, aBlock.center().y()), Qt::NoButton, Qt::LeftButton);
        CHECK(map.previewStart() + aSize <= d3.sizeBytes - MiB); // kẹp trong đĩa (chừa 1 MiB cuối)
        mouse(QEvent::MouseButtonRelease, aBlock.center(), Qt::LeftButton, Qt::NoButton);
        // Bố cục xem trước đúng: A: nằm sau B:.
        const QRect aAfter = map.blockRectForTest(0, 1), bAfter = map.blockRectForTest(0, 2);
        CHECK(aAfter.left() > bAfter.right());
        // B: (20 GiB) KHÔNG vừa vùng trống 5 GiB: kéo vào đó không làm B: nhảy sang chỗ không đủ chỗ.
        map.setData({d3}, parts3);
        map.setResizeContext(0, 2, PartitionManager::SupportedSizeRange{1 * GiB, 20 * GiB + 30 * GiB, true, QString()}, DiskLayout::ResizeDirection::Free);
        map.setMoveEnabled(true);
        const QRect bBlock = map.blockRectForTest(0, 2), free1 = map.unallocatedRectForTest(0, 0);
        mouse(QEvent::MouseButtonPress, bBlock.center(), Qt::LeftButton, Qt::LeftButton);
        mouse(QEvent::MouseMove, QPoint(free1.center().x(), bBlock.center().y()), Qt::NoButton, Qt::LeftButton);
        CHECK(map.previewSize() == bSize && map.previewStart() >= aStart + aSize + MiB - MiB); // vẫn là một vị trí hợp lệ (không chồng A:)
        CHECK(map.previewStart() >= aStart + aSize); // không bao giờ chồng lên A:
        mouse(QEvent::MouseButtonRelease, bBlock.center(), Qt::LeftButton, Qt::NoButton);
    }

    // ---- PartitionTab với dữ liệu GIẢ: ô vị trí, kế hoạch, chặn khi không dời được, banner nhật ký ----
    if (partTab && !partTab->isLoading())
    {
        partTab->setDataForTest({moveDisk}, moveParts);
        app.processEvents();
        auto* table = partTab->findChild<QTableWidget*>();
        auto* sizeSpin = partTab->findChild<QDoubleSpinBox*>("partitionNewSizeSpin");
        auto* startSpin = partTab->findChild<QDoubleSpinBox*>("partitionNewStartSpin");
        auto* confirm = partTab->findChild<QLineEdit*>("partitionConfirmEdit");
        auto* resizeBtn = partTab->findChild<QPushButton*>("partitionResizeButton");
        auto* change = partTab->findChild<QLabel*>("partitionChangeLabel");
        auto* edgeNote = partTab->findChild<QLabel*>("partitionEdgeNote");
        auto* pending = partTab->findChild<QLabel*>("partitionPendingBanner");
        auto* resumeBtn = partTab->findChild<QPushButton*>("partitionResumeButton");
        auto* abandonBtn = partTab->findChild<QPushButton*>("partitionAbandonButton");
        auto* pauseBtn = partTab->findChild<QPushButton*>("partitionPauseButton");
        DiskMapWidget* tmap = partTab->diskMap();
        CHECK(table && sizeSpin && startSpin && confirm && resizeBtn && change && edgeNote && pending && resumeBtn && abandonBtn && pauseBtn && tmap);
        if (table && sizeSpin && startSpin && confirm && resizeBtn && change && edgeNote && pending && resumeBtn && abandonBtn && pauseBtn && tmap)
        {
            CHECK(table->rowCount() == 4 && !pending->isVisibleTo(partTab) && !partTab->hasPendingJournal());
            CHECK(!pauseBtn->isVisibleTo(partTab) && !startSpin->isVisibleTo(partTab));
            table->selectRow(2); // D:
            app.processEvents();
            partTab->applySupportedRangeForTest(moveRange);
            app.processEvents();
            // THỬ NGHIỆM: mặc định TẮT - phải tích ô xác nhận mới dời được; chưa tích thì ghi rõ lý do, ô vị trí ẩn, chỉ kéo mép phải.
            auto* experimental = partTab->findChild<QCheckBox*>("partitionMoveExperimentalCheck");
            CHECK(experimental != nullptr && !experimental->isChecked());
            CHECK(partTab->moveSupportReason().contains("THỬ NGHIỆM") && !tmap->isMoveEnabled() && !startSpin->isVisibleTo(partTab));
            CHECK(edgeNote->text().contains("THỬ NGHIỆM") && tmap->leftHandleRectForTest().isNull());
            if (experimental)
                experimental->setChecked(true);
            app.processEvents();
            // Đã tích nhưng không phải Administrator (test không chạy với quyền cao): KHÔNG dời được, ghi rõ lý do.
            if (!PartitionManager::isElevated())
            {
                CHECK(!partTab->moveSupportReason().isEmpty() && !tmap->isMoveEnabled() && !startSpin->isVisibleTo(partTab));
                CHECK(edgeNote->text().contains("Administrator") && tmap->leftHandleRectForTest().isNull());
            }
            partTab->setMoveSupportForTest(true);
            app.processEvents();
            CHECK(partTab->moveSupportReason().isEmpty() && tmap->isMoveEnabled() && startSpin->isVisibleTo(partTab));
            CHECK(edgeNote->text().contains("DỜI được") && !tmap->leftHandleRectForTest().isNull());
            CHECK(std::abs(startSpin->value() - static_cast<double>(d2Start) / GiB) < 0.006);
            // Kế hoạch ban đầu: chưa đổi gì.
            auto g = partTab->plannedGeometry();
            CHECK(g.start == d2Start && g.size == d2Size && !partTab->currentPlan().blockReason.isEmpty());
            // Kéo mép trái hết vùng trống bên trái -> ô nhập đồng bộ ĐÚNG số byte của sơ đồ + mô tả "MỞ RỘNG ... về bên TRÁI" + 2 bước.
            {
                const QRect lh = tmap->leftHandleRectForTest();
                QMouseEvent pr(QEvent::MouseButtonPress, lh.center(), tmap->mapToGlobal(lh.center()), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(tmap, &pr);
                QMouseEvent mv(QEvent::MouseMove, lh.center() + QPoint(-3000, 0), tmap->mapToGlobal(lh.center()), Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(tmap, &mv);
                QMouseEvent rl(QEvent::MouseButtonRelease, lh.center(), tmap->mapToGlobal(lh.center()), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(tmap, &rl);
            }
            g = partTab->plannedGeometry();
            CHECK(g.start == cEnd2 && g.size == d2End - cEnd2);
            CHECK(std::abs(startSpin->value() - static_cast<double>(cEnd2) / GiB) < 0.006 && std::abs(sizeSpin->value() - static_cast<double>(d2End - cEnd2) / GiB) < 0.006);
            const auto plan = partTab->currentPlan();
            CHECK(plan.blockReason.isEmpty() && plan.steps.size() == 2 && plan.steps[0].kind == DiskLayout::StepKind::MoveData && plan.steps[1].kind == DiskLayout::StepKind::GrowRight);
            CHECK(change->text().contains("MỞ RỘNG") && change->text().contains("CHÉP") && change->text().contains("TRÁI"));
            CHECK(!resizeBtn->isEnabled()); // chưa gõ xác nhận
            CHECK(partTab->resizeRequestBlockReason().contains("xác nhận"));
            confirm->setText(confirm->placeholderText());
            CHECK(partTab->resizeRequestBlockReason().isEmpty() && resizeBtn->isEnabled());
            // Gõ ô vị trí: dời giữ nguyên kích thước; vị trí chồng lên C: bị bỏ và ô được đưa về giá trị sơ đồ chấp nhận.
            startSpin->setValue(static_cast<double>(d2Start + 5 * GiB) / GiB); // ô nhập làm tròn 0,01 GB -> vị trí lệch tối đa vài MiB, luôn căn 1 MiB
            const qint64 typedStart = tmap->previewStart();
            CHECK(std::llabs(typedStart - (d2Start + 5 * GiB)) < 8 * MiB && typedStart % MiB == 0 && tmap->previewSize() == d2End - cEnd2);
            startSpin->setValue(static_cast<double>(cEnd2 - 10 * GiB) / GiB); // lấn vào C:
            CHECK(tmap->previewStart() == typedStart);
            CHECK(std::abs(startSpin->value() - static_cast<double>(typedStart) / GiB) < 0.006);
            // Về đúng vị trí hiện tại ở độ chính xác của ô nhập = KHÔNG dời (đường chỉ-đổi-mép-phải như cũ).
            startSpin->setValue(std::round(static_cast<double>(d2Start) / GiB * 100.0) / 100.0);
            CHECK(tmap->previewStart() == d2Start);
            // Thay đổi vị trí rồi đổi hướng khóa/chọn dòng khác: mọi thứ reset sạch (không "rò" vị trí cũ sang phân vùng khác).
            startSpin->setValue(static_cast<double>(d2Start + 3 * GiB) / GiB);
            CHECK(std::llabs(tmap->previewStart() - (d2Start + 3 * GiB)) < 8 * MiB);
            table->selectRow(3); // E:
            app.processEvents();
            CHECK(!startSpin->isVisibleTo(partTab) && !resizeBtn->isVisibleTo(partTab) && !tmap->isMoveEnabled() && !tmap->isResizeActive());
            partTab->applySupportedRangeForTest(moveRange);
            CHECK(tmap->isResizeActive() && tmap->previewStart() == e2Start && tmap->currentStart() == e2Start);

            // Ứng dụng đang chạy từ chính ổ của phân vùng: dời qua BẢN CHẠY TẠM (needsRelocation) + ghi chú nêu rõ; ổ khác thì dời trực tiếp như thường.
            {
                const QString exeLetter = QDir::toNativeSeparators(QCoreApplication::applicationFilePath()).left(1).toUpper();
                auto relocParts = moveParts;
                relocParts[2].driveLetter = exeLetter;
                relocParts[3].driveLetter = exeLetter == "Z" ? "Y" : "Z";
                partTab->setDataForTest({moveDisk}, relocParts);
                app.processEvents();
                table->selectRow(2);
                app.processEvents();
                partTab->applySupportedRangeForTest(moveRange);
                CHECK(partTab->needsRelocation() && edgeNote->text().contains("TỰ chép mình"));
                CHECK(partTab->moveSupportReason().isEmpty()); // KHÔNG còn bị tắt chỉ vì chạy từ ổ này
                table->selectRow(3);
                app.processEvents();
                partTab->applySupportedRangeForTest(moveRange);
                CHECK(!partTab->needsRelocation() && !edgeNote->text().contains("TỰ chép mình"));
                partTab->setDataForTest({moveDisk}, moveParts);
                app.processEvents();
            }

            // Banner nhật ký dở dang: ghi tệp nhật ký THẬT (nhưng trong thư mục dữ liệu tạm của test) -> banner + nút theo canAbandon + chặn mọi thao tác.
            PartitionMove::JournalState j;
            j.diskNumber = 0;
            j.partitionNumber = 3;
            j.srcOffset = d2Start;
            j.dstOffset = d2Start - 5 * GiB;
            j.length = d2Size;
            j.unitBytes = 16 * MiB;
            j.doneBytes = 10 * 16 * MiB;
            j.sectorBytes = 512;
            {
                QFile f(PartitionMover::journalPath());
                CHECK(f.open(QIODevice::WriteOnly));
                f.write(j.toJson());
            }
            partTab->refreshPendingJournalForTest();
            app.processEvents();
            CHECK(partTab->hasPendingJournal() && pending->isVisibleTo(partTab) && resumeBtn->isVisibleTo(partTab));
            CHECK(!abandonBtn->isVisibleTo(partTab)); // chồng lấn + đã ghi -> chỉ "Tiếp tục"
            CHECK(pending->text().contains("DỞ DANG") && pending->text().contains("Đĩa 0"));
            table->selectRow(2);
            app.processEvents();
            partTab->applySupportedRangeForTest(moveRange);
            confirm->setText(confirm->placeholderText());
            CHECK(partTab->resizeRequestBlockReason().contains("dở dang") && !resizeBtn->isEnabled());
            CHECK(!tmap->isMoveEnabled() || partTab->moveSupportReason().isEmpty()); // (đang ép dời cho test)
            // Nhật ký chưa ghi gì -> hủy bỏ được.
            j.doneBytes = 0;
            {
                QFile f(PartitionMover::journalPath());
                CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
                f.write(j.toJson());
            }
            partTab->refreshPendingJournalForTest();
            CHECK(abandonBtn->isVisibleTo(partTab));
            // Nhật ký hỏng: banner báo lỗi, KHÔNG có nút Tiếp tục/Hủy, KHÔNG tự xóa tệp.
            {
                QFile f(PartitionMover::journalPath());
                CHECK(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
                f.write("{khong hop le");
            }
            partTab->refreshPendingJournalForTest();
            CHECK(partTab->hasPendingJournal() && pending->isVisibleTo(partTab) && !resumeBtn->isVisibleTo(partTab) && !abandonBtn->isVisibleTo(partTab));
            CHECK(pending->text().contains("hỏng") && QFile::exists(PartitionMover::journalPath()));
            // Xóa nhật ký: mọi thứ trở lại bình thường.
            PartitionMover::discardJournal();
            partTab->refreshPendingJournalForTest();
            CHECK(!partTab->hasPendingJournal() && !pending->isVisibleTo(partTab) && !resumeBtn->isVisibleTo(partTab));
            partTab->setMoveSupportForTest(false);
            // Tắt ô THỬ NGHIỆM giữa lúc đã kéo: xem trước về hình học hiện tại, ô vị trí ẩn.
            if (experimental)
            {
                partTab->setMoveSupportForTest(true);
                table->selectRow(2);
                app.processEvents();
                partTab->applySupportedRangeForTest(moveRange);
                tmap->setPreviewGeometry(d2Start + 4 * GiB, d2Size);
                CHECK(tmap->previewStart() == d2Start + 4 * GiB);
                partTab->setMoveSupportForTest(false);
                experimental->setChecked(false);
                app.processEvents();
                CHECK(tmap->previewStart() == d2Start && !tmap->isMoveEnabled() && !startSpin->isVisibleTo(partTab));
                CHECK(partTab->moveSupportReason().contains("THỬ NGHIỆM"));
            }
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

            // =============================================================================================================
            // Xem từng tệp/thư mục nhỏ (đọc trực tiếp, nạp nền) + xóa vào Thùng rác / xóa vĩnh viễn. Cây `base` ở trên là cây thật
            // trong thư mục tạm; mọi lần xóa chỉ chạm vào đúng cây này.
            // =============================================================================================================
            {
                using Kind = FolderTreeModel::Kind;
                folderTab->setAutoConfirmForTest(true); // không hiện hộp thoại - nội dung được ghi lại (lastMessageForTest)
                auto* m = qobject_cast<FolderTreeModel*>(view->model());
                CHECK(m != nullptr);
                auto* recycleBtn = folderTab->findChild<QPushButton*>("treeRecycleButton");
                auto* deleteBtn = folderTab->findChild<QPushButton*>("treeDeleteButton");
                CHECK(recycleBtn && deleteBtn && !recycleBtn->isEnabled() && !deleteBtn->isEnabled() == true);
                if (m && recycleBtn && deleteBtn)
                {
                    const QString baseClean = QDir::cleanPath(base);
                    const auto childByName = [&](const QModelIndex& parent, const QString& name) {
                        for (int r = 0; r < m->rowCount(parent); ++r)
                            if (m->index(r, 0, parent).data().toString() == name)
                                return m->index(r, 0, parent);
                        return QModelIndex();
                    };
                    const auto selectOnly = [&](const QList<QModelIndex>& list) {
                        view->selectionModel()->clearSelection();
                        for (const QModelIndex& i : list)
                            view->selectionModel()->select(i, QItemSelectionModel::Select | QItemSelectionModel::Rows);
                        view->selectionModel()->setCurrentIndex(list.isEmpty() ? QModelIndex() : list.first(), QItemSelectionModel::NoUpdate);
                        app.processEvents();
                    };
                    const auto doDelete = [&](bool permanent) {
                        folderTab->deleteSelected(permanent);
                        CHECK(waitFor([&]() { return !folderTab->isDeletingNow(); }, 30000));
                        app.processEvents();
                    };

                    QModelIndex topIdx = m->index(0, 0);
                    CHECK(topIdx.isValid() && m->isScanRoot(topIdx) && !m->isDeletable(topIdx));

                    // --- "[1 tệp nhỏ]" ở gốc: mở ra -> dòng "đang đọc" rồi tệp thật ---
                    QModelIndex agg = m->aggregateIndex(topIdx, Kind::SmallFiles);
                    CHECK(agg.isValid() && m->hasChildren(agg) && !m->isDeletable(agg));
                    view->expand(agg);
                    CHECK(m->rowCount(agg) >= 1);
                    CHECK(waitFor([&]() { return m->rowCount(agg) == 1 && m->kindAt(m->index(0, 0, agg)) == Kind::LiveFile; }, 15000));
                    const QModelIndex rootFile = m->index(0, 0, agg);
                    CHECK(m->pathAt(rootFile) == baseClean + "/rootfile.bin" && m->data(rootFile).toString() == "rootfile.bin");
                    CHECK(m->bytesAt(rootFile) >= 300 * 1024 && m->isDeletable(rootFile) && !m->hasChildren(rootFile));
                    CHECK(m->data(rootFile, FolderTreeModel::ShareRole).toDouble() > 0.0);
                    CHECK(view->isExpanded(agg)); // chèn mục thật không làm QTreeView thu gọn dòng cha

                    // --- "small" -> "[40 thư mục nhỏ ...]" -> 40 thư mục, mỗi thư mục 25 tệp ---
                    const QModelIndex smallIdx = childByName(topIdx, "small");
                    CHECK(smallIdx.isValid() && m->kindAt(smallIdx) == Kind::Folder);
                    view->expand(smallIdx);
                    QModelIndex smallDirs = m->aggregateIndex(smallIdx, Kind::SmallFolders);
                    CHECK(smallDirs.isValid());
                    view->expand(smallDirs);
                    CHECK(waitFor([&]() { return m->rowCount(smallDirs) == 40 && m->kindAt(m->index(0, 0, smallDirs)) == Kind::LiveFolder; }, 15000));
                    bool allUniform = true;
                    for (int r = 0; r < m->rowCount(smallDirs); ++r)
                    {
                        const QModelIndex d = m->index(r, 0, smallDirs);
                        allUniform = allUniform && m->kindAt(d) == Kind::LiveFolder && m->isDeletable(d) && m->hasChildren(d) &&
                                     m->data(m->index(r, FolderTreeModel::FilesColumn, smallDirs)).toString() == "25" &&
                                     m->pathAt(d).startsWith(baseClean + "/small/d");
                    }
                    CHECK(allUniform);
                    // Mở một thư mục nhỏ -> 25 tệp 200 byte.
                    const QModelIndex d0 = childByName(smallDirs, "d0");
                    CHECK(d0.isValid());
                    view->expand(d0);
                    CHECK(waitFor([&]() { return m->rowCount(d0) == 25 && m->kindAt(m->index(0, 0, d0)) == Kind::LiveFile; }, 15000));
                    CHECK(m->pathAt(m->index(0, 0, d0)).startsWith(baseClean + "/small/d0/f"));
                    if (qEnvironmentVariableIsSet("DISKCLEANUP_UI_SHOTS"))
                    {
                        selectOnly({d0, childByName(smallDirs, "d1")});
                        view->scrollTo(d0, QAbstractItemView::PositionAtTop);
                        app.processEvents();
                        win.grab().save(qEnvironmentVariable("DISKCLEANUP_UI_SHOTS") + "/foldertree_live.png");
                    }

                    // --- Nút xóa bật/tắt theo vùng chọn: dòng gộp/gốc không xóa được; thư mục/tệp thật thì được ---
                    selectOnly({agg});
                    CHECK(!recycleBtn->isEnabled() && !deleteBtn->isEnabled());
                    selectOnly({topIdx});
                    CHECK(!recycleBtn->isEnabled() && !deleteBtn->isEnabled());
                    selectOnly({d0, childByName(smallDirs, "d1")});
                    CHECK(recycleBtn->isEnabled() && deleteBtn->isEnabled());

                    // --- Gốc quét KHÔNG xóa được, dù cố bấm: không có gì thay đổi trên đĩa ---
                    selectOnly({topIdx});
                    doDelete(true);
                    CHECK(QDir(base).exists() && QFileInfo::exists(base + "/rootfile.bin"));
                    CHECK(folderTab->lastMessageForTest().contains("không phải thư mục/tệp xóa được"));

                    // --- Xóa vĩnh viễn HAI thư mục nhỏ (d0 + d1) trong dòng gộp ---
                    const qint64 filesBefore = m->tree()->root->fileCount;
                    const qint64 bytesBefore = m->tree()->root->bytes;
                    const qint64 d0Bytes = m->bytesAt(d0);
                    const QModelIndex d1 = childByName(smallDirs, "d1");
                    const qint64 d1Bytes = m->bytesAt(d1);
                    selectOnly({d0, d1});
                    doDelete(true);
                    CHECK(!QFileInfo::exists(base + "/small/d0") && !QFileInfo::exists(base + "/small/d1") && QFileInfo::exists(base + "/small/d2"));
                    CHECK(status->text().contains("Đã xóa vĩnh viễn 2 mục"));
                    CHECK(m->tree()->root->fileCount == filesBefore - 50);
                    CHECK(m->tree()->root->bytes == bytesBefore - d0Bytes - d1Bytes);
                    // Cây dựng lại nhưng các nhánh đang mở được mở lại và danh sách đọc lại còn 38 thư mục.
                    topIdx = m->index(0, 0);
                    CHECK(view->isExpanded(topIdx));
                    const QModelIndex smallIdx2 = childByName(topIdx, "small");
                    CHECK(smallIdx2.isValid() && view->isExpanded(smallIdx2) && view->currentIndex() == smallIdx2);
                    smallDirs = m->aggregateIndex(smallIdx2, Kind::SmallFolders);
                    CHECK(smallDirs.isValid() && view->isExpanded(smallDirs));
                    CHECK(waitFor([&]() { return m->rowCount(smallDirs) == 38 && m->kindAt(m->index(0, 0, smallDirs)) == Kind::LiveFolder; }, 15000));
                    CHECK(!childByName(smallDirs, "d0").isValid() && !childByName(smallDirs, "d1").isValid());

                    // --- Xóa VÀO THÙNG RÁC một tệp nhỏ (tệp do test tạo trong thư mục tạm) ---
                    agg = m->aggregateIndex(topIdx, Kind::SmallFiles);
                    CHECK(agg.isValid() && view->isExpanded(agg));
                    CHECK(waitFor([&]() { return m->rowCount(agg) == 1 && m->kindAt(m->index(0, 0, agg)) == Kind::LiveFile; }, 15000));
                    selectOnly({m->index(0, 0, agg)});
                    doDelete(false);
                    if (QFileInfo::exists(base + "/rootfile.bin"))
                        std::printf("LUU Y: tep khong vao duoc Thung rac tren may nay (%s) - bo qua kiem tra xoa vao Thung rac\n",
                                    qPrintable(folderTab->lastMessageForTest().left(120)));
                    else
                    {
                        CHECK(status->text().contains("Thùng rác"));
                        topIdx = m->index(0, 0);
                        CHECK(!m->aggregateIndex(topIdx, Kind::SmallFiles).isValid()); // hết tệp nhỏ ở gốc -> dòng gộp biến mất
                    }

                    // --- Thư mục được giữ riêng ("mid") + chọn cả thư mục lẫn tệp bên trong thư mục khác (chỉ xóa MỘT lần) ---
                    topIdx = m->index(0, 0);
                    selectOnly({childByName(topIdx, "mid")});
                    doDelete(true);
                    CHECK(!QFileInfo::exists(base + "/mid") && status->text().contains("Đã xóa vĩnh viễn 1 mục"));
                    topIdx = m->index(0, 0);
                    CHECK(!childByName(topIdx, "mid").isValid());
                    const QModelIndex bigIdx = childByName(topIdx, "big");
                    CHECK(bigIdx.isValid());
                    view->expand(bigIdx);
                    const QModelIndex b0 = childByName(bigIdx, "b0.bin");
                    CHECK(b0.isValid() && m->kindAt(b0) == Kind::File && m->isDeletable(b0));
                    selectOnly({bigIdx, b0});
                    doDelete(true);
                    CHECK(!QFileInfo::exists(base + "/big") && status->text().contains("Đã xóa vĩnh viễn 1 mục"));

                    // --- Số liệu của cây đã sửa phải KHỚP một lượt quét lại ---
                    FolderTreeScanner fresh;
                    std::shared_ptr<FolderTree> freshTree;
                    QObject::connect(&fresh, &FolderTreeScanner::scanFinished, [&](std::shared_ptr<FolderTree> t) { freshTree = t; });
                    fresh.setRootPath(base);
                    fresh.startScan();
                    CHECK(waitFor([&]() { return static_cast<bool>(freshTree); }, 30000));
                    fresh.wait();
                    if (freshTree)
                    {
                        const auto& edited = *m->tree();
                        CHECK(edited.root->bytes == freshTree->root->bytes && edited.root->logicalBytes == freshTree->root->logicalBytes);
                        CHECK(edited.root->fileCount == freshTree->root->fileCount && edited.root->folderCount == freshTree->root->folderCount);
                        CHECK(edited.stats.bytes == freshTree->stats.bytes && edited.stats.files == freshTree->stats.files);
                    }
                    CHECK(!folderTab->isDeletingNow() && !win.hasUnsafeOperationInProgress());
                }
                folderTab->setAutoConfirmForTest(false);
            }

            // --- Hủy mô hình / dựng lại trong lúc đang đọc thư mục: không crash, kết quả cũ bị bỏ ---
            {
                using Kind = FolderTreeModel::Kind;
                QTemporaryDir t2;
                CHECK(t2.isValid());
                const QString b2 = t2.path() + "/s2";
                QDir().mkpath(b2 + "/many");
                for (int i = 0; i < 300; ++i)
                {
                    QFile f(b2 + "/many/f" + QString::number(i) + ".txt");
                    f.open(QIODevice::WriteOnly);
                    f.write("hello");
                }
                QFile big(b2 + "/many/big.bin");
                big.open(QIODevice::WriteOnly);
                big.write(QByteArray(2 * 1024 * 1024, 'y'));
                big.close();
                FolderTreeScanner sc;
                std::shared_ptr<FolderTree> t;
                QObject::connect(&sc, &FolderTreeScanner::scanFinished, [&](std::shared_ptr<FolderTree> r) { t = r; });
                sc.setRootPath(b2);
                sc.startScan();
                CHECK(waitFor([&]() { return static_cast<bool>(t); }, 30000));
                sc.wait();
                if (t)
                {
                    // (a) hủy mô hình ngay sau khi bắt đầu đọc.
                    for (int round = 0; round < 20; ++round)
                    {
                        auto mdl = std::make_unique<FolderTreeModel>();
                        mdl->setTree(t);
                        const QModelIndex top = mdl->index(0, 0);
                        const QModelIndex many = mdl->index(0, 0, top);
                        const QModelIndex a = mdl->aggregateIndex(many, Kind::SmallFiles);
                        CHECK(a.isValid());
                        mdl->rowCount(a); // bắt đầu đọc nền
                        if (round % 2 == 0)
                            app.processEvents();
                        mdl.reset(); // ~FolderTreeModel chờ luồng nền thoát
                    }
                    app.processEvents();
                    // (b) dựng lại cây trong lúc đang đọc: dòng "đang đọc" cũ không được gắn vào cây mới.
                    FolderTreeModel mdl;
                    mdl.setTree(t);
                    QModelIndex top = mdl.index(0, 0);
                    QModelIndex many = mdl.index(0, 0, top);
                    QModelIndex a = mdl.aggregateIndex(many, Kind::SmallFiles);
                    mdl.rowCount(a);
                    mdl.setTree(t); // reset
                    app.processEvents();
                    top = mdl.index(0, 0);
                    many = mdl.index(0, 0, top);
                    a = mdl.aggregateIndex(many, Kind::SmallFiles);
                    CHECK(a.isValid() && mdl.rowCount(a) == 1 && mdl.kindAt(mdl.index(0, 0, a)) == Kind::Loading); // lượt mới, sạch
                    CHECK(waitFor([&]() { return mdl.rowCount(a) == 300 && mdl.kindAt(mdl.index(0, 0, a)) == Kind::LiveFile; }, 15000));
                    CHECK(mdl.liveLoadsInFlight() == 0);
                    // Thư mục không còn tồn tại lúc mở: một dòng ghi chú, không crash.
                    QFile::remove(b2 + "/many/f0.txt");
                    QDir(b2 + "/many").removeRecursively();
                    FolderTreeModel gone;
                    gone.setTree(t);
                    const QModelIndex gTop = gone.index(0, 0);
                    const QModelIndex gMany = gone.index(0, 0, gTop);
                    const QModelIndex gAgg = gone.aggregateIndex(gMany, Kind::SmallFiles);
                    gone.rowCount(gAgg);
                    CHECK(waitFor([&]() { return gone.rowCount(gAgg) == 1 && gone.kindAt(gone.index(0, 0, gAgg)) == Kind::LiveMore; }, 15000));
                    CHECK(!gone.isDeletable(gone.index(0, 0, gAgg)));
                }
            }
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
