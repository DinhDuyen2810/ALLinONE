// KIỂM TRA TAY - CHỈ ĐỌC: liệt kê chương trình/dịch vụ đang GIỮ một ổ bằng đúng VolumeHolders::scan của cửa sổ dời phân vùng - dùng khi dời/đổi
// kích thước báo "Không khóa được ổ ... Access is denied (mã 5)". KHÔNG dừng gì.
//
//   volume_holders_real_check <ổ> [--all] [--quick]
//     <ổ>    vd D hoặc D:
//     --all  in đủ mọi đường dẫn mẫu (mặc định 1 dòng mỗi chương trình)
//     --quick bỏ bước kiểm TỪNG tệp (chỉ nhân bản handle + DLL - nhanh nhưng không thấy nhân Windows/tiến trình được bảo vệ)
//
// Chạy KHÔNG quyền Administrator chỉ thấy tiến trình của tài khoản này; muốn thấy dịch vụ/tiến trình hệ thống:
//   Start-Process build\volume_holders_real_check.exe -ArgumentList 'D' -Verb RunAs   (bấm Yes ở UAC; kết quả in ra cửa sổ console riêng)
#include <QCoreApplication>
#include <QElapsedTimer>

#include <cstdio>

#include "tools/diskcleanup/engine/VolumeHolders.h"

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() < 2)
    {
        std::printf("Cach dung: volume_holders_real_check <o> [--all]\n");
        return 2;
    }
    const bool all = args.contains("--all");
    const bool quick = args.contains("--quick"); // bỏ bước kiểm từng tệp
    QElapsedTimer t;
    t.start();
    const VolumeHolders::Report r =
        VolumeHolders::scan(args[1], nullptr, !quick, [](int n) { std::fprintf(stderr, "  ... da kiem %d tep\r", n); });
    if (!r.error.isEmpty())
    {
        std::printf("LOI: %s\n", r.error.toUtf8().constData());
        return 1;
    }
    std::printf("O %s | quyen Administrator: %s | quet handle %s | kiem ky: %s (liet ke %d, hoi %d tep/thu muc%s, %d khong mo duoc) | %lld ms\n",
                args[1].toUtf8().constData(), r.elevated ? "CO" : "KHONG", r.handleScanComplete ? "day du" : "CHUA DAY DU",
                r.deepScanDone ? "xong" : "khong", r.entriesListed, r.filesChecked, r.deepLimited ? " - CHAM TRAN" : "", r.filesDenied, static_cast<long long>(t.elapsed()));
    if (r.pagefileOnDrive)
        std::printf("!! Co pagefile.sys/swapfile.sys tren o nay\n");
    std::printf("%d chuong trinh dang giu o:\n", static_cast<int>(r.holders.size()));
    for (const VolumeHolders::Holder& h : r.holders)
    {
        std::printf("  [%s] %-28s PID %-6u %s\n", h.stopLabel().toUtf8().constData(), h.name.toUtf8().constData(), h.pid,
                    h.holdingSummary().toUtf8().constData());
        if (!h.serviceLabels.isEmpty())
            std::printf("      dich vu: %s\n", h.serviceLabels.join(", ").toUtf8().constData());
        if (all)
            for (const QString& p : h.samplePaths)
                std::printf("      %s\n", p.toUtf8().constData());
    }
    for (const QString& n : r.notes)
        std::printf("Ghi chu: %s\n", n.toUtf8().constData());
    if (!r.unreadable.isEmpty())
        std::printf("Khong doc duoc %d tien trinh (duoc bao ve/thieu quyen): %s\n", static_cast<int>(r.unreadable.size()),
                    r.unreadable.mid(0, 15).join(", ").toUtf8().constData());
    return 0;
}
