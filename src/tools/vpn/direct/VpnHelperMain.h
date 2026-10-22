#pragma once

/// Điểm vào của TIẾN TRÌNH PHỤ NÂNG QUYỀN cho kết nối VPN trực tiếp: `OneForAll.exe --vpn-helper <pipe> <mã>`.
/// main() gọi hàm này TRƯỚC khi dựng QApplication. Tiến trình phụ nối vào named pipe do ứng dụng chính tạo,
/// gửi lại mã một lần để chứng minh nó đúng là tiến trình ứng dụng vừa khởi chạy, nhận việc (giao thức, máy
/// chủ, tài khoản) qua pipe - mật khẩu KHÔNG bao giờ nằm trên dòng lệnh - rồi chạy TunnelEngine. Pipe đóng
/// (ứng dụng chính thoát/đóng cửa sổ/bị dừng) => tự ngắt VPN, gỡ tuyến, xóa adapter rồi thoát: không bao
/// giờ để lại một tiến trình nâng quyền mồ côi giữ đường hầm (lỗi đã gặp với openvpn.exe ở v1.19.30).
int runVpnHelper(int argc, char* argv[]);
