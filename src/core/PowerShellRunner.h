#pragma once

#include <QByteArray>
#include <QString>
#include <atomic>

/// Chạy script PowerShell từ C++ - dùng CHUNG cho mọi module cần gọi PowerShell (PartitionManager để
/// đổi kích thước phân vùng, VpnController để quản lý kết nối VPN hệ thống...). Tách riêng để không
/// lặp lại (và không vô tình tái phát) 2 lỗi THẬT đã tìm ra khi kiểm tra tay trên máy thật - xem ghi
/// chú trong PowerShellRunner.cpp.
namespace PowerShellRunner
{
/// Chạy script PowerShell, trả về stdout; *ok = (exitCode == 0). -NoProfile/-NonInteractive: không
/// nạp profile người dùng, không chờ nhập liệu.
QByteArray run(const QString& script, bool* ok, QString* error = nullptr, int timeoutMs = 20000);

/// Biến thể CÓ THỂ HỦY GIỮA CHỪNG - dùng cho script có thể chạy rất lâu (vd Start-MpScan quét toàn bộ
/// máy, có thể mất hàng giờ). *cancelFlag được kiểm tra định kỳ (mỗi 200ms) trong lúc chờ - đặt thành
/// true từ luồng khác (vd khi người dùng đóng cửa sổ/bấm Hủy) để yêu cầu dừng; tiến trình powershell.exe
/// con bị kill() AN TOÀN ngay trên luồng đã tạo QProcess (không gọi QProcess xuyên luồng - KHÔNG an
/// toàn theo tài liệu Qt). Lý do cần hàm riêng này: hủy một QThread đang thực sự chạy dở (thay vì hủy
/// tiến trình con bên trong rồi để run() tự thoát) là hành vi KHÔNG XÁC ĐỊNH theo tài liệu Qt
/// (QThread::~QThread cảnh báo "Destroyed while thread is still running") - đã phát hiện nguy cơ này
/// khi tự rà soát toàn bộ ứng dụng cho các thao tác chạy lâu (Start-MpScan tới 1 giờ) mà cửa sổ chứa nó
/// lại không chặn việc đóng cửa sổ giữa chừng.
QByteArray runCancelable(const QString& script, const std::atomic_bool* cancelFlag, bool* ok,
                         QString* error = nullptr, int timeoutMs = 20000);

/// Thoát một chuỗi để đặt AN TOÀN giữa hai dấu nháy đơn trong script PowerShell ('...'). PowerShell coi
/// CẢ BỐN ký tự U+2018/U+2019/U+201A/U+201B là dấu nháy đơn y như U+0027 (xem tokenizer của PowerShell,
/// IsSingleQuote) - chỉ nhân đôi dấu ' ASCII như các bản escapePsString riêng lẻ trước đây để lọt một
/// tên thư mục/tên kết nối VPN chứa ’ đóng chuỗi sớm và chạy phần còn lại như lệnh (đã xác nhận thật
/// bằng parser của PowerShell). Mọi nơi dựng script từ chuỗi không tự kiểm soát PHẢI dùng hàm này.
QString quoteLiteral(const QString& value);

/// Base64(UTF-16LE) của script - giá trị truyền cho `powershell.exe -EncodedCommand`. Dành cho nơi phải
/// tự khởi chạy powershell.exe TÁCH RỜI (không chờ kết quả) nên không dùng run()/runCancelable() được,
/// vd tiến trình trợ giúp cài bản cập nhật sau khi ứng dụng đã thoát (UpdateInstaller).
QString encodedCommand(const QString& script);
} // namespace PowerShellRunner
