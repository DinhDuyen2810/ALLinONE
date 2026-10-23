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
  động "Gõ văn bản" được lưu dạng chữ thường trong tệp này - đừng đặt mật khẩu quan trọng vào đó. Hành động
  **"Ghi nhớ chuỗi"** (từ bản 1.19.11) ghi lại TOÀN BỘ thao tác chuột+bàn phím thật của bạn trên **toàn bộ
  máy** (không giới hạn trong cửa sổ One for ALL) trong lúc đang ghi - có thể gồm mật khẩu nếu bạn gõ trong
  lúc đó. Dữ liệu ghi được chỉ lưu vào tệp hồ sơ cục bộ này, không gửi đi đâu; ứng dụng luôn hỏi xác nhận
  trước khi bắt đầu ghi mỗi lần.
- **QR Tools**: lịch sử quét/tạo mã QR (`profiles\qr_history.json`), gồm cả nội dung mã WiFi bạn đã tạo/quét.
  Từ bản 1.19.12: quét được mã QR WiFi có thể bấm "Kết nối WiFi" ngay (dùng lại đúng API WLAN/hộp thoại của
  module WiFi Connection, đúng MỘT mật khẩu lấy từ chính mã QR, không dò/thử gì thêm); tạo mã QR WiFi có thể
  chọn nhanh từ hồ sơ WiFi đã lưu trên máy (đọc trực tiếp từ Windows, không gửi đi đâu); tạo mã QR Vị trí có
  nút "Lấy vị trí hiện tại" gọi dịch vụ `ipwho.is` để ước lượng tọa độ theo IP công khai (xem bảng bên dưới);
  dán link Google Maps ĐẦY ĐỦ chỉ đọc tọa độ từ chuỗi bạn dán, không gọi mạng; dán link RÚT GỌN
  (`maps.app.goo.gl/...`) thì ứng dụng gửi MỘT yêu cầu HTTPS tới Google để đọc địa chỉ chuyển hướng (chỉ đọc
  header, không tải trang; chỉ đi theo các địa chỉ thuộc Google, tối đa 5 bước).
- **VPN đã cài sẵn (từ 1.19.28)**: ứng dụng ĐỌC (chỉ đọc) tên hồ sơ + địa chỉ máy chủ của các kết nối VPN đã cấu hình bởi FortiClient/GlobalProtect/OpenVPN/Cisco trên máy bạn (từ registry/tệp cấu hình của CHÍNH các phần mềm đó) để hiển thị và mở phần mềm đó khi bạn bấm - KHÔNG đọc/giải mã mật khẩu đã lưu trong các phần mềm đó.
- **Kết nối trực tiếp FortiGate SSL-VPN / GlobalProtect (từ 1.19.31)**: khi bạn bấm "Kết nối trực tiếp", ứng dụng gửi
  tên đăng nhập + mật khẩu (và mã OTP nếu máy chủ hỏi) MÀ BẠN NHẬP tới ĐÚNG máy chủ VPN của cơ quan đã cấu hình trong
  hồ sơ, qua HTTPS. Mật khẩu/OTP KHÔNG ghi ra tệp hay nhật ký. Nếu bạn tích "Lưu tên đăng nhập và mật khẩu" (từ
  1.19.33), tài khoản được lưu trong **Windows Credential Manager** (mục "OneForAll/VPN/<máy chủ>", Windows mã hóa theo
  tài khoản Windows của bạn) CHỈ sau khi đăng nhập thành công, để lần sau chỉ cần bấm Kết nối; xóa bằng nút "Quên mật
  khẩu đã lưu" hoặc trong Control Panel > Credential Manager. Không tích thì mật khẩu chỉ nằm trong bộ nhớ tới khi đăng
  nhập xong rồi bị xóa trắng. Tên đăng nhập gần nhất được nhớ theo máy chủ (`profiles\vpn_direct_users.json`). Nếu chứng chỉ máy
  chủ không được Windows tin cậy (FortiGate hay dùng chứng chỉ mặc định tự ký), ứng dụng hiện vân tay SHA-256 để bạn
  đối chiếu; vân tay bạn chấp nhận được lưu theo "máy chủ:cổng" (`profiles\vpn_trusted_certs.json`). Phần mạng chạy
  trong một tiến trình phụ của chính ứng dụng với quyền Administrator (hộp thoại UAC) để tạo adapter mạng ảo wintun
  và đặt tuyến/DNS; nó nhận việc qua kênh nội bộ có mã một lần (mật khẩu không nằm trên dòng lệnh) và tự gỡ tuyến,
  xóa adapter, thoát khi bạn ngắt hoặc khi ứng dụng đóng. Khi kết nối, lưu lượng theo tuyến máy chủ cấp (toàn bộ hoặc
  chỉ mạng nội bộ cơ quan) đi qua máy chủ VPN của cơ quan bạn - như khi dùng FortiClient/GlobalProtect.
- **Connect Together**: danh sách thiết bị đã ghép đôi + khóa dài hạn của từng cặp máy
  (`profiles\connect_peers.json`, khóa được bảo vệ bằng Windows DPAPI theo tài khoản của bạn), định danh máy
  bạn tự sinh ngẫu nhiên (`profiles\connect_identity.json`, không gắn với danh tính thật). Khi đang kết nối,
  văn bản bạn sao chép được gửi (mã hóa) sang các máy đã kết nối trong LAN; từ bản 1.19.23 có ô "Đồng bộ clipboard"
  trong cửa sổ Connect Together để TẮT việc này (ví dụ trước khi sao chép mật khẩu) - ô này bật lại mỗi lần mở cửa sổ.
- **WiFi Connection**: hồ sơ/mật khẩu WiFi đọc trực tiếp từ Windows (API WLAN chính thức) - không sao
  chép ra nơi khác ngoài những gì bạn chủ động xuất (export).
- **VPN & Location**: khóa L2TP/IPsec (PSK) nhập khi thêm hồ sơ (từ 1.19.23) chỉ được chuyển cho Windows lúc tạo kết nối
  (Windows tự lưu như khi bạn thêm VPN trong Cài đặt), ứng dụng không ghi nó ra tệp nào. Nhãn quốc gia/tên đăng nhập bạn
  tự khai báo (`profiles\vpn_profiles.json`); mật khẩu
  VPN KHÔNG được ghi ra đĩa dưới bất kỳ hình thức nào.
- **Đổi IP theo quốc gia (VPN Gate)**: ứng dụng tạo một hồ sơ VPN TẠM của Windows ("OneForAll-VPNGate") tới máy chủ
  bạn chọn - SSTP (mặc định từ 1.19.31) hoặc L2TP/IPsec - với tài khoản CÔNG KHAI của VPN Gate ("vpn"), và tự xóa hồ sơ
  khi ngắt. Không cần quyền Administrator, không chạy chương trình ngoài. Khi kết nối, TOÀN BỘ lưu lượng mạng của bạn đi
  qua máy chủ tình nguyện của VPN Gate (bên thứ ba) - đây là bản chất của mọi VPN công cộng miễn phí; chỉ dùng khi bạn
  chấp nhận điều đó. (Bản 1.19.30 từng chạy `openvpn.exe` đóng gói kèm - đã gỡ hoàn toàn ở 1.19.31.)
- **Downloader**: cài đặt (`profiles\downloader_settings.json`: thư mục tải xuống, số tệp đồng thời, giới hạn mục, bật/tắt trình duyệt ẩn, chế độ cookie cùng TÊN trình duyệt hoặc ĐƯỜNG DẪN tệp `cookies.txt` - ứng dụng KHÔNG đọc hay lưu nội dung cookie) và lịch sử tải (`profiles\downloader_history.json`, tối đa 2000 mục: tên, đường dẫn tệp, link nguồn, link tệp, thời gian, kích thước, kết quả). Link nguồn có thể chứa mã/token của trang bạn đã tải - xóa từng mục hoặc toàn bộ trong tab "Lịch sử". Các tệp đã tải nằm ở thư mục bạn chọn, không bị xóa khi xóa lịch sử.
- **Log ứng dụng**: `logs\*.log` - nhật ký hoạt động/chẩn đoán lỗi, ở lại trên máy bạn, không gửi đi đâu
  (trừ khi chính bạn chủ động gửi để báo lỗi). Log không ghi nội dung văn bản Auto Click gõ hộ bạn.

**Downloader - đánh dấu tệp tải từ Internet (từ 1.19.23)**: giống trình duyệt, mỗi tệp tải xong được gắn "Mark of the
Web" - luồng dữ liệu NTFS `<tệp>:Zone.Identifier` ghi `ZoneId=3` cùng địa chỉ tệp và trang nguồn (ĐÃ BỎ phần truy vấn
`?...` và thông tin đăng nhập). Dữ liệu này nằm ngay trên tệp trong máy bạn, để Windows SmartScreen/Office Protected View
cảnh báo khi mở; không gửi đi đâu.

## Kết nối mạng ra bên ngoài ứng dụng CÓ thực hiện (và vì sao)
Các tính năng sau cần kết nối Internet để hoạt động - đây là các dịch vụ **bên thứ ba độc lập**, không
phải máy chủ của nhóm phát triển, và chỉ gọi khi bạn chủ động dùng tính năng tương ứng:
| Tính năng | Gọi tới đâu | Dữ liệu gửi đi |
|---|---|---|
| Tự kiểm tra bản cập nhật (1 lần/lúc mở ứng dụng) | GitHub Releases API (`api.github.com`); khi bạn bấm "Cập nhật ngay" thì tải tệp cài đặt từ `github.com` | Không gì ngoài yêu cầu HTTP tiêu chuẩn (không gửi kèm thông tin máy/định danh) |
| VPN & Location - xem vị trí theo IP; QR Tools - nút "Lấy vị trí hiện tại" khi tạo mã QR Vị trí | `ipwho.is` (dịch vụ tra cứu IP công khai) | Địa chỉ IP công khai hiện tại của bạn (vốn đã lộ với MỌI trang web bạn truy cập, không riêng gì tính năng này) |
| QR Tools - dán link RÚT GỌN Google Maps ở trang tạo mã QR Vị trí | `maps.app.goo.gl` (và các địa chỉ thuộc Google mà link chuyển hướng tới, tối đa 5 bước) | Chính đường dẫn rút gọn bạn dán (là nội dung yêu cầu HTTP) + địa chỉ IP công khai của bạn; chỉ đọc header chuyển hướng, không tải nội dung trang |
| QR Tools - nút "Mở bản đồ" cho mã QR Vị trí | `google.com/maps` (mở bằng trình duyệt mặc định của bạn, không gọi ngầm) | Tọa độ nằm trong đường dẫn được mở - giống bấm vào bất kỳ liên kết Google Maps nào khác |
| VPN & Location - tab "Đổi IP theo quốc gia" (từ 1.19.26) | `www.vpngate.net` (API công khai của VPN Gate - ĐH Tsukuba) khi bấm "Tải thêm máy chủ"; khi KẾT NỐI, lưu lượng của bạn đi qua máy chủ relay TÌNH NGUYỆN bạn chọn (như mọi VPN). Tab "Kiểm tra kết nối" mở một kết nối TCP tới đúng máy chủ+cổng BẠN nhập | Một yêu cầu HTTP(S) tải danh sách (không kèm định danh máy). Khi đã kết nối, traffic của bạn đi qua máy chủ của tình nguyện viên - họ có thể thấy lưu lượng như bất kỳ máy chủ VPN nào; chỉ dùng relay công cộng cho việc bạn chấp nhận rủi ro đó. Danh sách máy chủ đã tải được lưu cục bộ tại `profiles/vpngate_servers.json` |
| WiFi - đo tốc độ mạng | `speed.cloudflare.com` | Dữ liệu đo tốc độ tiêu chuẩn (giống mọi công cụ speedtest khác) |
| Downloader - hàng đợi/quét trang/video nền tảng | Chỉ các địa chỉ do CHÍNH BẠN dán vào và những địa chỉ trang đó dẫn tới (tệp, ảnh, iframe/trình phát nhúng, luồng video, CDN); Google Drive/Docs → `google.com`/`googleusercontent.com`; `yt-dlp.exe` kết nối tới nền tảng tương ứng (YouTube, Facebook, TikTok, Instagram...) | Yêu cầu HTTP(S) tới đúng các địa chỉ đó, kèm `Referer` = trang nguồn và `User-Agent` giống trình duyệt để máy chủ ảnh/video không chặn. Nếu bạn BẬT "lấy cookie từ trình duyệt" hoặc chọn `cookies.txt`, yt-dlp (chạy cục bộ) đọc cookie đăng nhập của bạn và gửi kèm yêu cầu tới CHÍNH nền tảng đó như trình duyệt của bạn vẫn làm - không gửi tới nơi nào khác |
| Android Phone Control, Connect Together | Thiết bị/máy khác trên mạng LAN của bạn (không qua Internet) | Dữ liệu điều khiển/màn hình - không rời khỏi mạng cục bộ |

**Trình duyệt ẩn (Downloader)**: khi các cách khác không tìm ra tệp, ứng dụng chạy Microsoft Edge hoặc Google Chrome ĐÃ CÓ trên máy ở chế độ ẩn (`--headless`) với **hồ sơ tạm riêng** (không dùng hồ sơ, tài khoản hay cookie của bạn) để nạp trang và ghi nhật ký mạng, rồi xóa hồ sơ tạm. Trang đó thấy một trình duyệt mới như lần truy cập đầu tiên. Tắt tính năng này trong tab Cài đặt của Downloader.

**Tài liệu Google Drive chỉ xem (Downloader, từ v1.19.22)**: nếu link Drive là tài liệu mà chủ tệp đã tắt "tải xuống" và "Dùng trình duyệt ẩn" đang bật, ứng dụng mở trang xem `https://drive.google.com/file/d/<ID>/view` trong Edge/Chrome ẩn với hồ sơ tạm riêng (KHÔNG dùng cookie/tài khoản của bạn, chỉ chạy được với link công khai), cổng điều khiển DevTools chỉ mở trên `127.0.0.1` (cổng ngẫu nhiên) trong lúc chụp, rồi chụp ảnh từng trang mà Google đã gửi tới trình duyệt đó và ghép PDF tại máy bạn. Không có dữ liệu nào gửi đi nơi khác; trình duyệt và hồ sơ tạm bị xóa sau mỗi lần. Tính năng này không khuyến khích dùng cho mục đích vi phạm pháp luật hoặc bản quyền - chỉ dùng cho tài liệu bạn có quyền sử dụng; tắt bằng cách bỏ chọn "Dùng trình duyệt ẩn" ở tab Cài đặt của Downloader.

**Cookie `cookies.txt` (Downloader, từ v1.19.18)**: nếu bạn chọn tệp `cookies.txt` ở tab Cài đặt, chính ứng dụng cũng đọc nó (cục bộ, tối đa 5 MB) để quét link và tải tệp bạn có quyền (vd Google Drive bạn là chủ sở hữu). Cookie chỉ nằm trong bộ nhớ tiến trình, không ghi/chép đi đâu, và Qt chỉ gửi tới đúng miền/đường dẫn khớp với cookie (kể cả sau chuyển hướng). Nhật ký `app.log` ghi địa chỉ đã phân giải (máy chủ + đường dẫn, KHÔNG ghi chuỗi truy vấn) cùng kết quả/lý do lỗi để chẩn đoán - xóa được cùng thư mục dữ liệu.

## Mã nguồn mở - tự kiểm chứng được
Toàn bộ mã nguồn công khai tại [github.com/DinhDuyen2810/ALLinONE](https://github.com/DinhDuyen2810/ALLinONE)
(giấy phép MIT) - bất kỳ ai cũng có thể đọc lại chính xác ứng dụng làm gì, không cần tin theo lời khẳng
định ở tài liệu này.

## Liên hệ
Có câu hỏi về quyền riêng tư - mở một Issue tại trang GitHub của dự án.
