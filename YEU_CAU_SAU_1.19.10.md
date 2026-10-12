# Toàn bộ yêu cầu người dùng gửi sau bản v1.19.10

Tài liệu này liệt kê đầy đủ các yêu cầu/phản hồi người dùng đã gửi, và những gì đã được thực hiện cho từng
bản, từ v1.19.10 (mốc tham chiếu "giao diện đẹp, không lỗi") tới v1.19.21 (bản mới nhất). Mục đích: để người
dùng thấy rõ TOÀN BỘ nội dung sẽ bị mất nếu quay lại đúng code v1.19.10 và xóa các commit/release sau đó.

Nguồn: `PROJECT_OVERVIEW.md` mục 4y → 4aj (lịch sử chi tiết, có bằng chứng kỹ thuật cho từng dòng dưới đây).

---

## v1.19.11 (2026-10-11) — commit `c8eff73`

**Yêu cầu người dùng**: gửi 2 ảnh chụp thật báo "v1.19.10 VẪN CHƯA hết góc đen" (cả ở menu chuột phải lẫn
danh sách xổ xuống "ms/giây/phút/giờ").

**Đã làm**:
- Tìm nguyên nhân thứ hai của góc đen popup: Windows tự vẽ bóng đổ hình chữ nhật (`CS_DROPSHADOW`) đè lên
  góc đã bo tròn — `WA_TranslucentBackground` một mình (v1.19.10) không đủ.
- Tạo widget dùng chung mới `PopupCornerFix` — cài một event filter ở cấp `QApplication`, tự áp dụng cho
  MỌI popup trong ứng dụng (kể cả popup Qt tự tạo bên trong `QComboBox` mà ứng dụng không sở hữu trực tiếp),
  không cần sửa từng nơi tạo popup.
- Mở rộng con trỏ ngón tay (`ComboBoxStyle::applyCursor()`) sang `QCheckBox` (ví dụ "Vô hạn (Infinite)").
- Đổi chữ thông báo lúc tự cập nhật theo đúng yêu cầu người dùng.

---

## v1.19.12 (2026-10-12) — commit `51ac6ab`

**Yêu cầu người dùng**: thêm một loại hành động MỚI cho Auto Click, ngang hàng với Click chuột/Kéo chuột/
Giữ chuột/Gõ văn bản/Tổ hợp phím/Nhấn phím/Cuộn — **"Ghi nhớ chuỗi"**: bấm "Bắt đầu ghi" thì ứng dụng tự thu
nhỏ, hiện minibox đếm thời gian + số bước, ghi lại TOÀN BỘ thao tác chuột+bàn phím THẬT trên toàn máy tới
khi bấm nút dừng trên minibox; kết quả ghi được LƯU LÀM 1 HÀNH ĐỘNG duy nhất trong chuỗi, phát lại đúng thứ
tự + đúng khoảng cách thời gian giữa các bước lúc chạy chuỗi.

**Đã làm** (tính năng lớn, không chỉ sửa giao diện):
- `Action.h`: thêm `RecordedEventType`/`RecordedEvent` (6 loại: MouseMove/Down/Up/Wheel, KeyDown/Up) +
  `ActionType::RecordedMacro`.
- File mới `capture/ActionRecorder.h/.cpp`: hook đồng thời `WH_MOUSE_LL`+`WH_KEYBOARD_LL`, Esc KHÔNG hủy ghi
  (khác mọi capture khác vì Esc có thể là phím cần ghi lại), giảm tải `MouseMove` (~50 mẫu/giây), loại trừ
  click rơi vào chính minibox, tự chèn sự kiện nhả cho phím/nút còn giữ dở lúc dừng.
- `InputController`: thêm `keyDown/keyUp/mouseWheelTick/replayRecordedEvents`.
- `ActionEditorWidget`: trang "Ghi nhớ chuỗi" mới, luôn hỏi xác nhận phạm vi ghi (toàn máy, có thể chứa mật
  khẩu, chỉ lưu cục bộ) trước khi bắt đầu.
- `RuntimeOverlay`: thêm `Mode::Recording`.
- `ActionSerializer`: ghi/đọc `recordedEvents`, hồ sơ cũ thiếu key vẫn đọc được (rỗng), không crash.
- 154/154 `autoclick_tests` (+30 mới), `core_stress_tests` 551/551 pass.

---

## v1.19.13 (2026-10-12) — commit `ae232ec`

**Yêu cầu người dùng**: gộp 12 phản hồi trải khắp QR Tools, Auto Click, theming dùng chung.

**Đã làm**:
1. QR Tools — WiFi: chọn nhanh từ hồ sơ đã lưu + nút "🔗 Kết nối WiFi" khi quét ra mã WiFi.
2. QR Tools — Vị trí: nút "Lấy vị trí hiện tại" (theo IP công khai) + "Mở bản đồ" (Google Maps).
3. QR Tools — Quét: ẩn khung "KẾT QUẢ" khi ảnh chỉ có đúng 1 mã; nhận diện email/SĐT nằm rải rác trong văn
   bản tự do; bo góc bảng đồng bộ 12px.
4. QR Tools — Tạo mã chỉ còn 5 loại (Văn bản/URL/WiFi/SĐT/Vị trí), bỏ Email/SMS/vCard khỏi danh sách tạo.
5. Cursor ngón tay mở rộng sang MỌI `QPushButton` toàn ứng dụng.
6. Thời gian hiện thông báo trước khi tự khởi động lại tăng từ 1.5s lên 3.5s.
7. Góc đen menu chuột phải: củng cố thêm `SetWindowPos(SWP_FRAMECHANGED)` cho `PopupCornerFix`.
8. Auto Click — chỉnh sửa chưa lưu khi đổi hàng xem không còn bị mất (cache `m_pendingEdits`).
9. Auto Click — minibox hết đứng yên sai lúc đang ghi chuỗi (sửa do `WM_TIMER` bị hook chuột toàn cục
   lấn át).

166/166 `autoclick_tests`, không hồi quy 16 bộ test tool khác, `core_stress_tests`/`qrwifi_stress_tests` pass.

---

## v1.19.14 (2026-10-12) — commit `14baffc`

**Yêu cầu người dùng**: gửi 6 ảnh chụp thật, nêu 5 điểm qua `/goal` — trong đó quan trọng nhất: **viền ĐEN
dày quanh cửa sổ, `QMessageBox` nền đen, menu chuột phải còn góc nhọn sót lại** dù đã "sửa" ở các bản trước.

**Đã làm**:
- **Viền đen quanh cửa sổ**: khung cửa sổ (Windows vẽ) theo Dark Mode máy, nội dung (Qt vẽ) luôn sáng — hai
  lớp lệch màu. Thêm file mới `WindowTheme.h/.cpp`: `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE=
  false)`, gọi ở cuối constructor của 13 cửa sổ/dialog.
- **`QMessageBox` nền đen**: style mặc định "windowsvista" tự đọc Dark Mode cho widget không có QSS riêng.
  Thêm `QApplication::setStyle(Fusion)` + `QPalette` sáng cố định trong `main.cpp`.
- **Góc nhọn còn sót ở popup**: UxTheme Windows tự vẽ thêm viền theo theme "Menu" độc lập với QSS. Thêm
  `SetWindowTheme(hwnd, L" ", L" ")` vào `PopupCornerFix`.
- Minibox "Ghi nhớ chuỗi" hết né chuột lúc ĐANG GHI (đảo ngược cơ chế thêm ở v1.19.13 — người dùng giải
  thích lại: lúc ghi phải đứng yên tuyệt đối để luôn bấm trúng nút dừng).
- QR Vị trí hết bị che ngang; bản đồ nhúng OpenStreetMap chọn tọa độ trực tiếp trong app.

---

## v1.19.15 (2026-10-12) — commit `2d3f548`

**Yêu cầu người dùng**: gửi 2 ảnh chụp thật ngay sau khi cài v1.19.14 — `AutoClickWindow` lộ vệt ĐEN chạy
TOÀN CHIỀU NGANG (cả thanh tiêu đề) ở hai dải không có widget con che phủ, "giao diện nát".

**Đã làm**: nghi ngờ nhiều nhất là `DWMWA_WINDOW_CORNER_PREFERENCE=DWMWCP_ROUND` (bo góc kiểu Windows 11)
thêm ở v1.19.14 gây lỗi compositing cho cửa sổ `QWidget` thường. Bỏ HẲN phần bo góc DWM, chỉ giữ phần ép
màu sáng, thêm `SetWindowPos(SWP_FRAMECHANGED)`.

---

## v1.19.16 (2026-10-12) — commit `f1399cc`

**Yêu cầu người dùng**: `/goal` "rà thật kỹ từng đoạn, tìm lỗi giao diện và sửa hết" — rà soát toàn diện,
không chờ báo lỗi cụ thể.

**Đã làm**: chia 5 agent rà song song theo nhóm công cụ. Lỗi nghiêm trọng nhất: `QMessageBox`/hộp thoại dựng
qua hàm tĩnh (70+ chỗ) vẫn còn thanh tiêu đề TỐI dù nội dung đã sáng — sửa bằng cách mở rộng event filter
của `PopupCornerFix` tự áp `applyLightChrome()` cho MỌI cửa sổ top-level. Checkbox thiếu `outline:none` (11
chỗ sót). `OsmMapWidget` nhãn "Đang tải..." sai điều kiện. `QRGenerateTab` đo `sizeHint()` sai lúc bản đồ
ẩn. Bảng `MalwareScanTab`/`PartitionTab` thiếu word-wrap/tràn khung nhìn. `QPalette` bổ sung role còn thiếu.

---

## v1.19.17 (2026-10-12) — commit `b6d20a3`

**Yêu cầu người dùng**: Stop hook của `/goal` từ chối coi v1.19.16 là đủ vì chưa thật sự xác nhận bằng ảnh
chụp.

**Đã làm**: tìm ra cách chụp ảnh THẬT một cửa sổ trong môi trường này (`PrintWindow`). Tìm và sửa lỗi THẬT:
gọi DWM API ĐỒNG BỘ ngay trong `QEvent::Show` làm hỏng bản vẽ đầu của cửa sổ — sửa bằng hoãn qua
`QTimer::singleShot`. Phát hiện thêm vệt đen ở `AutoClickWindow` CHƯA giải quyết được lúc đó. Thêm word-wrap
cho cột tên tệp Downloader.

---

## v1.19.18 (2026-10-12) — gộp trong commit `9e9f889`

**Yêu cầu người dùng**: gửi 3 ảnh chụp THẬT trên máy (không qua công cụ tự động hóa) xác nhận vệt đen
`AutoClickWindow` là lỗi thật, gợi ý so sánh code v1.19.10 (ảnh chụp không lỗi) với hiện tại để tìm đúng chỗ.

**Đã làm**: loại trừ lần lượt WindowTheme/Fusion/QPalette/PopupCornerFix (từng cái và cả 4 cùng lúc) — vẫn
đen. Không xác định chắc chắn được nguyên nhân gốc, nhưng tìm được cách sửa THẬT hoạt động: override
`paintEvent()` của `AutoClickWindow` tự vẽ nền `#f6f8fa` đúng `event->rect()`. Xác nhận hết vệt đen qua ảnh
chụp thật nhiều lần. Ghi nhận trung thực 2 vấn đề khác CHƯA sửa: viền xám quanh cửa sổ + nút vuông (ảnh 1),
góc đen menu chuột phải (ảnh 3).

---

## v1.19.19 (2026-10-12) — gộp trong commit `9e9f889`

**Yêu cầu người dùng**: tiếp tục hỏi thẳng đã sửa xong 3 ảnh chưa trước khi cho push/build.

**Đã làm**: lần đầu tái hiện được góc đen menu trong môi trường cô lập (chương trình Qt tối giản độc lập) —
chụp ảnh thật xác nhận góc TRÊN-TRÁI có tam giác đen. Phát hiện popup KHÔNG mang cờ `WS_EX_LAYERED` — dùng
đường DWM-native translucency riêng của Qt6. Sửa bằng `SetWindowRgn` (cắt cứng theo Win32).

---

## v1.19.20 (2026-10-12) — gộp trong commit `9e9f889`

**Yêu cầu người dùng**: xác nhận giữ `SetWindowRgn`, yêu cầu bắt tay làm ảnh 1 (thanh tiêu đề) theo cách AN
TOÀN (giữ resize/Aero Snap gốc Windows), sau đó `/goal` yêu cầu so sánh code v1.19.10 cho ảnh 3.

**Đã làm**: so sánh `git diff v1.19.10 HEAD` cho các file popup — xác nhận các cơ chế thêm sau v1.19.10
(`CS_DROPSHADOW`/`SetWindowTheme`/Fusion) chưa từng được xác nhận bằng ảnh chụp thật. Bắt đầu thử nghiệm
thanh tiêu đề tự vẽ cho `MainWindow` (widget mới `FramelessChrome`) — kỹ thuật "extend frame into client
area", giữ nguyên resize/Aero Snap gốc.

---

## v1.19.21 (2026-10-13) — commit `9e9f889` (bản hiện tại, đã push `main`)

**Yêu cầu người dùng**: gửi 3 ảnh chụp THẬT mới trên máy kèm phản ứng bức xúc — combo dropdown trần trụi
("Phím:", "ms/giây/phút/giờ"), 3 nút cửa sổ vuông viền rõ — yêu cầu "lấy lại toàn bộ giao diện 1.19.10".

**Đã làm**:
- **Bỏ hẳn `FramelessChrome`** (thử nghiệm thanh tiêu đề tự vẽ từ v1.19.20) — chưa kiểm chứng được trên màn
  hình thật dù đã thử 9 hướng khác nhau (`Qt::grab()` xác nhận Qt vẽ đúng nhưng `PrintWindow`/`BitBlt`/
  `GetDC(NULL)` đều không đọc được đúng vùng góc trên-phải — nghi Windows 11 Snap Layout). `MainWindow.cpp`
  quay lại dùng khung cửa sổ GỐC của Windows, code giống hệt v1.19.10 cho phần này.
- **Tìm đúng nguyên nhân combo dropdown xấu**: so sánh với 8 tool khác, phát hiện Auto Click thiếu đúng 1
  dòng CSS `QComboBox QAbstractItemView` (quyết định màu danh sách khi mở ra) — có ở `kComboStyle`,
  `m_typeCombo`, `DurationInput`. Sửa cả 3 chỗ, xác nhận bằng ảnh chụp thật (kỹ thuật mới: `PostMessage
  WM_LBUTTONDOWN/UP` mô phỏng click để mở đúng combo "Phím:").
- **Xác nhận dứt điểm góc đen menu** (ảnh 3, tồn đọng từ v1.19.19): `PrintWindow` từng báo sai do giới hạn
  riêng của chính nó — dùng `GetWindowDC`+`BitBlt` xác nhận cả 4 góc sạch hoàn toàn.
- Tạo tag `v1.19.21` + push rồi **tự xóa lại** theo đúng yêu cầu người dùng (chưa tự kiểm tra trên máy thật
  thì chưa được tạo release) — hiện KHÔNG có release nào tồn tại cho bản này.

---

## Tổng kết những gì sẽ MẤT nếu quay lại đúng v1.19.10 và xóa hết commit/release sau đó

1. **Toàn bộ tính năng "Ghi nhớ chuỗi"** (ghi macro chuột+bàn phím thật, phát lại) — v1.19.12, một tính năng
   lớn người dùng chủ động yêu cầu thêm, không phải sửa lỗi.
2. QR Tools: chọn WiFi đã lưu khi tạo mã, kết nối WiFi khi quét, vị trí theo IP + bản đồ nhúng chọn tọa độ,
   nhận diện email/SĐT trong văn bản tự do, chỉ còn 5 loại tạo mã.
3. Toàn bộ sửa lỗi dark-mode: viền cửa sổ đen, `QMessageBox` nền đen, thanh tiêu đề hộp thoại tối.
4. Mọi sửa lỗi combo/spinbox/checkbox: mũi tên còn sót, con trỏ ngón tay, outline focus xấu.
5. Auto Click: cache chỉnh sửa chưa lưu (chống mất dữ liệu khi đổi hàng), minibox né chuột đúng theo từng
   chế độ (ghi/chạy).
6. Rà soát toàn diện 9 công cụ (word-wrap, bo góc đồng bộ, bảng tràn khung nhìn...).
7. Sửa combo dropdown Auto Click + xác nhận dứt điểm góc đen menu (bản mới nhất, v1.19.21).

**Những gì CÒN ĐÚNG như v1.19.10** (chưa từng đổi khác): logic nghiệp vụ của cả 9 công cụ (WiFi/VPN/Disk
Cleanup/Android/Security/Downloader/Connect Together không có thay đổi giao diện gì đáng kể ngoài các sửa
lỗi dùng-chung ở trên).
