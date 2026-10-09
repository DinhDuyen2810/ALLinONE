# Chính sách quyền riêng tư - One for ALL

**Tóm tắt:** One for ALL không thu thập, không gửi đi, không bán bất kỳ dữ liệu cá nhân nào của bạn. Toàn
bộ dữ liệu ứng dụng tạo ra ở lại trên chính máy bạn.

## Không có hệ thống thu thập dữ liệu/phân tích (telemetry)
Ứng dụng KHÔNG gửi bất kỳ thông tin sử dụng, số liệu thống kê, hay nhận dạng thiết bị nào về bất kỳ máy
chủ nào do nhóm phát triển vận hành - vì **không có máy chủ nào do nhóm phát triển vận hành cả**. Mọi
tính năng chạy hoàn toàn cục bộ trên máy bạn.

## Dữ liệu lưu trên máy bạn (không rời khỏi máy, trừ khi chính bạn xuất ra)
Từ bản 1.19.0, toàn bộ dữ liệu dưới đây nằm trong thư mục riêng của tài khoản Windows của bạn:
`%LOCALAPPDATA%\OneForAll\` (thư mục con `profiles\` và `logs\`). Gỡ cài đặt ứng dụng KHÔNG xóa thư mục
này - muốn xóa sạch dữ liệu, tự xóa thư mục đó.
- **Auto Click**: chuỗi hành động tự động hóa bạn tạo (`profiles\default.json`). Nội dung bạn nhập cho hành
  động "Gõ văn bản" được lưu dạng chữ thường trong tệp này - đừng đặt mật khẩu quan trọng vào đó.
- **QR Tools**: lịch sử quét/tạo mã QR (`profiles\qr_history.json`), gồm cả nội dung mã WiFi bạn đã tạo/quét.
- **Connect Together**: danh sách thiết bị đã ghép đôi + khóa dài hạn của từng cặp máy
  (`profiles\connect_peers.json`, khóa được bảo vệ bằng Windows DPAPI theo tài khoản của bạn), định danh máy
  bạn tự sinh ngẫu nhiên (`profiles\connect_identity.json`, không gắn với danh tính thật).
- **WiFi Connection**: hồ sơ/mật khẩu WiFi đọc trực tiếp từ Windows (API WLAN chính thức) - không sao
  chép ra nơi khác ngoài những gì bạn chủ động xuất (export).
- **VPN & Location**: nhãn quốc gia/tên đăng nhập bạn tự khai báo (`profilespn_profiles.json`); mật khẩu
  VPN KHÔNG được ghi ra đĩa dưới bất kỳ hình thức nào.
- **Log ứng dụng**: `logs\*.log` - nhật ký hoạt động/chẩn đoán lỗi, ở lại trên máy bạn, không gửi đi đâu
  (trừ khi chính bạn chủ động gửi để báo lỗi). Log không ghi nội dung văn bản Auto Click gõ hộ bạn.

## Kết nối mạng ra bên ngoài ứng dụng CÓ thực hiện (và vì sao)
Các tính năng sau cần kết nối Internet để hoạt động - đây là các dịch vụ **bên thứ ba độc lập**, không
phải máy chủ của nhóm phát triển, và chỉ gọi khi bạn chủ động dùng tính năng tương ứng:
| Tính năng | Gọi tới đâu | Dữ liệu gửi đi |
|---|---|---|
| Tự kiểm tra bản cập nhật (1 lần/lúc mở ứng dụng) | GitHub Releases API (`api.github.com`); khi bạn bấm "Cập nhật ngay" thì tải tệp cài đặt từ `github.com` | Không gì ngoài yêu cầu HTTP tiêu chuẩn (không gửi kèm thông tin máy/định danh) |
| VPN & Location - xem vị trí theo IP | `ipwho.is` (dịch vụ tra cứu IP công khai) | Địa chỉ IP công khai hiện tại của bạn (vốn đã lộ với MỌI trang web bạn truy cập, không riêng gì tính năng này) |
| WiFi - đo tốc độ mạng | `speed.cloudflare.com` | Dữ liệu đo tốc độ tiêu chuẩn (giống mọi công cụ speedtest khác) |
| Downloader - tải trực tiếp/quét trang/video nền tảng | URL/trang web do CHÍNH BẠN nhập | Yêu cầu HTTP tới đúng địa chỉ bạn dán vào |
| Android Phone Control, Connect Together | Thiết bị/máy khác trên mạng LAN của bạn (không qua Internet) | Dữ liệu điều khiển/màn hình - không rời khỏi mạng cục bộ |

## Mã nguồn mở - tự kiểm chứng được
Toàn bộ mã nguồn công khai tại [github.com/DinhDuyen2810/ALLinONE](https://github.com/DinhDuyen2810/ALLinONE)
(giấy phép MIT) - bất kỳ ai cũng có thể đọc lại chính xác ứng dụng làm gì, không cần tin theo lời khẳng
định ở tài liệu này.

## Liên hệ
Có câu hỏi về quyền riêng tư - mở một Issue tại trang GitHub của dự án.
