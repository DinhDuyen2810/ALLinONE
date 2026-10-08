cmake_minimum_required(VERSION 3.20)

# Đọc số phiên bản từ file VERSION ở gốc dự án - nguồn DUY NHẤT cho version (trước đây CMakeLists/
# installer/README mỗi nơi tự ghi một con số khác nhau, dễ lệch - xem src/core/update/Version.h.in).
# UpdateChecker so sánh giá trị sinh ra từ đây với tag bản phát hành mới nhất trên GitHub Releases.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" APP_VERSION_STRING LIMIT_COUNT 1)
string(STRIP "${APP_VERSION_STRING}" APP_VERSION_STRING)

project(OneForAll VERSION ${APP_VERSION_STRING} LANGUAGES C CXX)

configure_file(
    src/core/update/Version.h.in
    ${CMAKE_BINARY_DIR}/generated/core/Version.h
    @ONLY
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia Svg Network)

# ---- Lõi QR (không phụ thuộc widget) + thư viện bên thứ ba ----
add_library(qr_core STATIC
    src/tools/qr/QRCodec.h
    src/tools/qr/QRCodec.cpp
    src/tools/qr/QRPayload.h
    src/tools/qr/QRPayload.cpp
    src/third_party/qrcodegen/qrcodegen.cpp
    src/third_party/quirc/quirc.c
    src/third_party/quirc/decode.c
    src/third_party/quirc/identify.c
    src/third_party/quirc/version_db.c
)
target_include_directories(qr_core PUBLIC src)
target_link_libraries(qr_core PUBLIC Qt6::Core Qt6::Gui)
# quirc mặc định chỉ có 254 vùng liên thông (pixel 8-bit) nên mã QR mật độ cao (version >= ~26) không được tìm thấy
target_compile_definitions(qr_core PRIVATE QUIRC_MAX_REGIONS=65534)
# Code bên thứ ba: không cảnh báo
set_source_files_properties(
    src/third_party/quirc/quirc.c src/third_party/quirc/decode.c
    src/third_party/quirc/identify.c src/third_party/quirc/version_db.c
    PROPERTIES COMPILE_FLAGS "-w")

# ---- Kiểm thử lõi QR: cmake --build build --target qr_tests ----
add_executable(qr_tests EXCLUDE_FROM_ALL tests/qr_tests.cpp)
target_link_libraries(qr_tests PRIVATE qr_core Qt6::Svg)

# ---- Giao diện QR (static lib để dùng chung với bộ kiểm thử UI) ----
add_library(qr_ui STATIC
    src/tools/qr/QRWindow.h
    src/tools/qr/QRWindow.cpp
    src/tools/qr/QRGenerateTab.h
    src/tools/qr/QRGenerateTab.cpp
    src/tools/qr/QRScanTab.h
    src/tools/qr/QRScanTab.cpp
    src/tools/qr/QRHistoryTab.h
    src/tools/qr/QRHistoryTab.cpp
    src/tools/qr/QRHistoryStore.h
    src/tools/qr/QRHistoryStore.cpp
    src/tools/qr/QRImageView.h
    src/tools/qr/QRImageView.cpp
    src/tools/qr/ScreenSnipOverlay.h
    src/tools/qr/ScreenSnipOverlay.cpp
    src/tools/qr/QRUiStyle.h
)
target_include_directories(qr_ui PUBLIC src)
target_link_libraries(qr_ui PUBLIC qr_core Qt6::Widgets Qt6::Multimedia)

# ---- Kiểm thử UI QR: cmake --build build --target qr_ui_tests ----
add_executable(qr_ui_tests EXCLUDE_FROM_ALL tests/qr_ui_tests.cpp src/core/Logger.cpp)
target_link_libraries(qr_ui_tests PRIVATE qr_ui)

# ---- Lõi WiFi (XML hồ sơ WLAN, thuần Qt, không phụ thuộc Win32) ----
add_library(wifi_core STATIC
    src/tools/wifi/model/WifiSecurity.h
    src/tools/wifi/model/WifiNetwork.h
    src/tools/wifi/model/WifiProfile.h
    src/tools/wifi/engine/WlanProfileXml.h
    src/tools/wifi/engine/WlanProfileXml.cpp
)
target_include_directories(wifi_core PUBLIC src)
target_link_libraries(wifi_core PUBLIC Qt6::Core)

# ---- Kiểm thử lõi WiFi: cmake --build build --target wifi_tests ----
add_executable(wifi_tests EXCLUDE_FROM_ALL tests/wifi_tests.cpp)
target_link_libraries(wifi_tests PRIVATE wifi_core)

# ---- Giao diện WiFi + WlanController (gọi Windows WLAN API, chỉ build được trên Windows) ----
add_library(wifi_ui STATIC
    src/tools/wifi/engine/WlanController.h
    src/tools/wifi/engine/WlanController.cpp
    src/tools/wifi/engine/SpeedTestRunner.h
    src/tools/wifi/engine/SpeedTestRunner.cpp
    src/tools/wifi/WifiUiStyle.h
    src/tools/wifi/ConnectDialog.h
    src/tools/wifi/ConnectDialog.cpp
    src/tools/wifi/NetworksTab.h
    src/tools/wifi/NetworksTab.cpp
    src/tools/wifi/ProfilesTab.h
    src/tools/wifi/ProfilesTab.cpp
    src/tools/wifi/SpeedTestTab.h
    src/tools/wifi/SpeedTestTab.cpp
    src/tools/wifi/WifiWindow.h
    src/tools/wifi/WifiWindow.cpp
)
target_include_directories(wifi_ui PUBLIC src)
target_link_libraries(wifi_ui PUBLIC wifi_core Qt6::Widgets Qt6::Network wlanapi)

# ---- Kiểm thử "khói" UI WiFi: cmake --build build --target wifi_ui_tests ----
add_executable(wifi_ui_tests EXCLUDE_FROM_ALL tests/wifi_ui_tests.cpp src/core/Logger.cpp)
target_link_libraries(wifi_ui_tests PRIVATE wifi_ui)

# ---- Lõi Connect Together (mã hóa, giao thức, mã ghép đôi) - thuần Qt + Windows CNG, test được ----
add_library(connect_core STATIC
    src/tools/connect/model/ScreenSide.h
    src/tools/connect/model/PeerInfo.h
    src/tools/connect/model/PairedPeer.h
    src/tools/connect/engine/PairingCode.h
    src/tools/connect/engine/PairingCode.cpp
    src/tools/connect/engine/ProtocolMessage.h
    src/tools/connect/engine/ProtocolMessage.cpp
    src/tools/connect/engine/CryptoSession.h
    src/tools/connect/engine/CryptoSession.cpp
    src/tools/connect/engine/PeerStore.h
    src/tools/connect/engine/PeerStore.cpp
    src/tools/connect/engine/NetworkSession.h
    src/tools/connect/engine/NetworkSession.cpp
    src/tools/connect/engine/PeerDiscovery.h
    src/tools/connect/engine/PeerDiscovery.cpp
    src/tools/connect/engine/EdgeDetector.h
    src/tools/connect/engine/EdgeDetector.cpp
    src/tools/connect/engine/InputInjector.h
    src/tools/connect/engine/InputInjector.cpp
    src/tools/connect/engine/InputHook.h
    src/tools/connect/engine/InputHook.cpp
    src/tools/connect/engine/LocalIdentityStore.h
    src/tools/connect/engine/LocalIdentityStore.cpp
    src/tools/connect/engine/ConnectSessionController.h
    src/tools/connect/engine/ConnectSessionController.cpp
)
target_include_directories(connect_core PUBLIC src)
target_link_libraries(connect_core PUBLIC Qt6::Core Qt6::Gui Qt6::Network bcrypt)

# ---- Kiểm thử lõi Connect Together: cmake --build build --target connect_tests ----
add_executable(connect_tests EXCLUDE_FROM_ALL tests/connect_tests.cpp)
target_link_libraries(connect_tests PRIVATE connect_core)

# ---- Giao diện Connect Together ----
add_library(connect_ui STATIC
    src/tools/connect/ConnectUiStyle.h
    src/tools/connect/PairingTab.h
    src/tools/connect/PairingTab.cpp
    src/tools/connect/PeersTab.h
    src/tools/connect/PeersTab.cpp
    src/tools/connect/ConnectWindow.h
    src/tools/connect/ConnectWindow.cpp
)
target_include_directories(connect_ui PUBLIC src)
target_link_libraries(connect_ui PUBLIC connect_core Qt6::Widgets)

# ---- Kiểm thử "khói" UI Connect Together: cmake --build build --target connect_ui_tests ----
add_executable(connect_ui_tests EXCLUDE_FROM_ALL tests/connect_ui_tests.cpp src/core/Logger.cpp)
target_link_libraries(connect_ui_tests PRIVATE connect_ui)

# ---- Lõi Disk Cleanup (quét/xóa/dung lượng ổ đĩa) ----
add_library(diskcleanup_core STATIC
    src/tools/diskcleanup/model/CleanupCategory.h
    src/tools/diskcleanup/model/CleanupItem.h
    src/tools/diskcleanup/engine/CategoryRegistry.h
    src/tools/diskcleanup/engine/CategoryRegistry.cpp
    src/tools/diskcleanup/engine/CleanupScanner.h
    src/tools/diskcleanup/engine/CleanupScanner.cpp
    src/tools/diskcleanup/engine/RecycleBinOps.h
    src/tools/diskcleanup/engine/RecycleBinOps.cpp
    src/tools/diskcleanup/engine/CleanupExecutor.h
    src/tools/diskcleanup/engine/CleanupExecutor.cpp
    src/tools/diskcleanup/engine/DiskSpaceInfo.h
    src/tools/diskcleanup/engine/DiskSpaceInfo.cpp
    src/tools/diskcleanup/engine/LargeFileScanner.h
    src/tools/diskcleanup/engine/LargeFileScanner.cpp
    src/tools/diskcleanup/engine/DuplicateFinder.h
    src/tools/diskcleanup/engine/DuplicateFinder.cpp
    src/tools/diskcleanup/engine/PartitionManager.h
    src/tools/diskcleanup/engine/PartitionManager.cpp
    src/tools/diskcleanup/engine/PartitionResizer.h
    src/tools/diskcleanup/engine/PartitionResizer.cpp
    src/core/WinElevation.h
    src/core/WinElevation.cpp
    src/core/PowerShellRunner.h
    src/core/PowerShellRunner.cpp
)
target_include_directories(diskcleanup_core PUBLIC src)
target_link_libraries(diskcleanup_core PUBLIC Qt6::Core shell32 advapi32)

# ---- Kiểm thử lõi Disk Cleanup: cmake --build build --target diskcleanup_tests ----
add_executable(diskcleanup_tests EXCLUDE_FROM_ALL tests/diskcleanup_tests.cpp)
target_link_libraries(diskcleanup_tests PRIVATE diskcleanup_core)

# ---- Giao diện Disk Cleanup ----
add_library(diskcleanup_ui STATIC
    src/tools/diskcleanup/DiskUiStyle.h
    src/tools/diskcleanup/CleanupTab.h
    src/tools/diskcleanup/CleanupTab.cpp
    src/tools/diskcleanup/LargeFilesTab.h
    src/tools/diskcleanup/LargeFilesTab.cpp
    src/tools/diskcleanup/DuplicateFilesTab.h
    src/tools/diskcleanup/DuplicateFilesTab.cpp
    src/tools/diskcleanup/PartitionTab.h
    src/tools/diskcleanup/PartitionTab.cpp
    src/tools/diskcleanup/DiskCleanupWindow.h
    src/tools/diskcleanup/DiskCleanupWindow.cpp
)
target_include_directories(diskcleanup_ui PUBLIC src)
target_link_libraries(diskcleanup_ui PUBLIC diskcleanup_core Qt6::Widgets)

# ---- Kiểm thử "khói" UI Disk Cleanup: cmake --build build --target diskcleanup_ui_tests ----
# PartitionTab.cpp gọi ToolManager::instance() (chặn "chạy lại với quyền Admin" nếu tool khác đang bận
# không an toàn để hủy) - thêm ToolManager.cpp làm nguồn phụ, cùng lý do với Logger.cpp (không phải static
# lib riêng để link vào).
add_executable(diskcleanup_ui_tests EXCLUDE_FROM_ALL tests/diskcleanup_ui_tests.cpp src/core/Logger.cpp src/core/ToolManager.cpp)
target_link_libraries(diskcleanup_ui_tests PRIVATE diskcleanup_ui)

# ---- Lõi Android Phone Control (gọi adb.exe/scrcpy.exe đóng gói kèm - xem vendor/scrcpy, THIRD_PARTY.md) ----
add_library(android_core STATIC
    src/tools/android/model/AndroidDeviceInfo.h
    src/tools/android/engine/AdbController.h
    src/tools/android/engine/AdbController.cpp
    src/tools/android/engine/ScrcpyLauncher.h
    src/tools/android/engine/ScrcpyLauncher.cpp
    src/core/WinProcessTree.h
    src/core/WinProcessTree.cpp
)
target_include_directories(android_core PUBLIC src)
target_link_libraries(android_core PUBLIC Qt6::Core)

# ---- Kiểm thử lõi Android: cmake --build build --target android_tests ----
add_executable(android_tests EXCLUDE_FROM_ALL tests/android_tests.cpp)
target_link_libraries(android_tests PRIVATE android_core)

# ---- Giao diện Android Phone Control ----
add_library(android_ui STATIC
    src/tools/android/AndroidUiStyle.h
    src/tools/android/WirelessPairDialog.h
    src/tools/android/WirelessPairDialog.cpp
    src/tools/android/DevicesTab.h
    src/tools/android/DevicesTab.cpp
    src/tools/android/AndroidControlWindow.h
    src/tools/android/AndroidControlWindow.cpp
)
target_include_directories(android_ui PUBLIC src)
target_link_libraries(android_ui PUBLIC android_core Qt6::Widgets)

# ---- Kiểm thử "khói" UI Android: cmake --build build --target android_ui_tests ----
add_executable(android_ui_tests EXCLUDE_FROM_ALL tests/android_ui_tests.cpp src/core/Logger.cpp)
target_link_libraries(android_ui_tests PRIVATE android_ui)

# ---- Lõi VPN & Location (quản lý kết nối VPN hệ thống Windows qua PowerShell/rasdial) ----
add_library(vpn_core STATIC
    src/tools/vpn/model/VpnProfile.h
    src/tools/vpn/engine/VpnController.h
    src/tools/vpn/engine/VpnController.cpp
    src/tools/vpn/engine/VpnConnector.h
    src/tools/vpn/engine/VpnConnector.cpp
    src/tools/vpn/engine/PublicIpChecker.h
    src/tools/vpn/engine/PublicIpChecker.cpp
    src/core/WinElevation.h
    src/core/WinElevation.cpp
    src/core/PowerShellRunner.h
    src/core/PowerShellRunner.cpp
)
target_include_directories(vpn_core PUBLIC src)
target_link_libraries(vpn_core PUBLIC Qt6::Core Qt6::Network advapi32 shell32)

# ---- Kiểm thử lõi VPN: cmake --build build --target vpn_tests ----
add_executable(vpn_tests EXCLUDE_FROM_ALL tests/vpn_tests.cpp)
target_link_libraries(vpn_tests PRIVATE vpn_core)

# ---- Giao diện VPN & Location ----
add_library(vpn_ui STATIC
    src/tools/vpn/VpnUiStyle.h
    src/tools/vpn/AddVpnProfileDialog.h
    src/tools/vpn/AddVpnProfileDialog.cpp
    src/tools/vpn/VpnTab.h
    src/tools/vpn/VpnTab.cpp
    src/tools/vpn/VpnControlWindow.h
    src/tools/vpn/VpnControlWindow.cpp
)
target_include_directories(vpn_ui PUBLIC src)
target_link_libraries(vpn_ui PUBLIC vpn_core Qt6::Widgets)

# ---- Kiểm thử "khói" UI VPN: cmake --build build --target vpn_ui_tests ----
add_executable(vpn_ui_tests EXCLUDE_FROM_ALL tests/vpn_ui_tests.cpp src/core/Logger.cpp)
target_link_libraries(vpn_ui_tests PRIVATE vpn_ui)

# ---- Lõi Security Gateway (cổng lệnh PowerShell + bảo vệ web + quét mã độc qua Windows Defender) ----
# InputController.h/.cpp được liệt kê lại ở đây (giống WinElevation/PowerShellRunner đã dùng chung cho
# diskcleanup_core/vpn_core) vì CommandLauncher cần gửi Ctrl+V thật - tránh phụ thuộc vòng vào các
# nguồn trực tiếp của OneForAll (AutoClick không đóng gói thành thư viện riêng).
add_library(security_core STATIC
    src/tools/security/model/ThreatRecord.h
    src/tools/security/engine/CommandAnalyzer.h
    src/tools/security/engine/CommandAnalyzer.cpp
    src/tools/security/engine/CommandLauncher.h
    src/tools/security/engine/CommandLauncher.cpp
    src/tools/security/engine/DefenderController.h
    src/tools/security/engine/DefenderController.cpp
    src/tools/security/engine/DefenderScanWorker.h
    src/tools/security/engine/DefenderScanWorker.cpp
    src/tools/security/engine/HostsBlocklist.h
    src/tools/security/engine/HostsBlocklist.cpp
    src/tools/autoclick/engine/InputController.h
    src/tools/autoclick/engine/InputController.cpp
    src/core/WinElevation.h
    src/core/WinElevation.cpp
    src/core/PowerShellRunner.h
    src/core/PowerShellRunner.cpp
)
target_include_directories(security_core PUBLIC src)
target_link_libraries(security_core PUBLIC Qt6::Core Qt6::Gui advapi32 shell32 user32)

# ---- Kiểm thử lõi Security Gateway: cmake --build build --target security_tests ----
add_executable(security_tests EXCLUDE_FROM_ALL tests/security_tests.cpp)
target_link_libraries(security_tests PRIVATE security_core)

# ---- Giao diện Security Gateway ----
add_library(security_ui STATIC
    src/tools/security/SecurityUiStyle.h
    src/tools/security/CommandGatewayTab.h
    src/tools/security/CommandGatewayTab.cpp
    src/tools/security/WebProtectionTab.h
    src/tools/security/WebProtectionTab.cpp
    src/tools/security/MalwareScanTab.h
    src/tools/security/MalwareScanTab.cpp
    src/tools/security/SecurityGatewayWindow.h
    src/tools/security/SecurityGatewayWindow.cpp
)
target_include_directories(security_ui PUBLIC src)
target_link_libraries(security_ui PUBLIC security_core Qt6::Widgets)

# ---- Kiểm thử "khói" UI Security Gateway: cmake --build build --target security_ui_tests ----
# WebProtectionTab.cpp/MalwareScanTab.cpp gọi ToolManager::instance() (chặn "chạy lại với quyền Admin"
# nếu tool khác đang bận không an toàn để hủy) - thêm ToolManager.cpp làm nguồn phụ, cùng lý do Logger.cpp.
add_executable(security_ui_tests EXCLUDE_FROM_ALL tests/security_ui_tests.cpp src/core/Logger.cpp src/core/ToolManager.cpp)
target_link_libraries(security_ui_tests PRIVATE security_ui)

# ---- Lõi Downloader (tải trực tiếp HTTP + quét media trang web + yt-dlp cho video nền tảng) ----
add_library(downloader_core STATIC
    src/tools/downloader/model/DownloadItem.h
    src/tools/downloader/model/MediaLink.h
    src/tools/downloader/model/VideoFormatInfo.h
    src/tools/downloader/model/VideoInfo.h
    src/tools/downloader/engine/FileDownloader.h
    src/tools/downloader/engine/FileDownloader.cpp
    src/tools/downloader/engine/PageMediaScanner.h
    src/tools/downloader/engine/PageMediaScanner.cpp
    src/tools/downloader/engine/YtDlpController.h
    src/tools/downloader/engine/YtDlpController.cpp
    src/tools/downloader/engine/YtDlpInfoWorker.h
    src/tools/downloader/engine/YtDlpInfoWorker.cpp
    src/tools/downloader/engine/YtDlpDownloadWorker.h
    src/tools/downloader/engine/YtDlpDownloadWorker.cpp
    src/core/WinProcessTree.h
    src/core/WinProcessTree.cpp
)
target_include_directories(downloader_core PUBLIC src)
target_link_libraries(downloader_core PUBLIC Qt6::Core Qt6::Network)

# ---- Kiểm thử lõi Downloader: cmake --build build --target downloader_tests ----
add_executable(downloader_tests EXCLUDE_FROM_ALL tests/downloader_tests.cpp)
target_link_libraries(downloader_tests PRIVATE downloader_core)

# ---- Lõi tự cập nhật (hỏi GitHub Releases API + tải/cài bản mới) - dùng lại FileDownloader của
# downloader_core thay vì viết lại logic tải HTTP, xem UpdateInstaller.h ----
add_library(update_core STATIC
    src/core/update/UpdateChecker.h
    src/core/update/UpdateChecker.cpp
    src/core/update/UpdateInstaller.h
    src/core/update/UpdateInstaller.cpp
)
target_include_directories(update_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(update_core PUBLIC downloader_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

# ---- Kiểm thử lõi tự cập nhật: cmake --build build --target update_tests ----
# update_core (khác các *_core thuần khác) có gọi Logger::instance() (ghi log kiểm tra cập nhật) VÀ
# ToolManager::instance() (UpdateInstaller tự kiểm tra "có tool nào đang bận không an toàn để hủy" trước
# khi âm thầm chạy trình cài đặt) - thêm thẳng cả 2 làm nguồn phụ, đúng mẫu đã dùng cho mọi *_ui_tests
# (Logger.cpp/ToolManager.cpp chỉ nằm trong SOURCES chính của OneForAll, không phải static lib riêng).
add_executable(update_tests EXCLUDE_FROM_ALL tests/update_tests.cpp src/core/Logger.cpp src/core/ToolManager.cpp)
target_link_libraries(update_tests PRIVATE update_core)

# ---- Giao diện Downloader ----
add_library(downloader_ui STATIC
    src/tools/downloader/DownloaderUiStyle.h
    src/tools/downloader/DirectDownloadTab.h
    src/tools/downloader/DirectDownloadTab.cpp
    src/tools/downloader/PageScanTab.h
    src/tools/downloader/PageScanTab.cpp
    src/tools/downloader/PlatformVideoTab.h
    src/tools/downloader/PlatformVideoTab.cpp
    src/tools/downloader/DownloaderWindow.h
    src/tools/downloader/DownloaderWindow.cpp
)
target_include_directories(downloader_ui PUBLIC src)
target_link_libraries(downloader_ui PUBLIC downloader_core Qt6::Widgets)

# ---- Kiểm thử "khói" UI Downloader: cmake --build build --target downloader_ui_tests ----
add_executable(downloader_ui_tests EXCLUDE_FROM_ALL tests/downloader_ui_tests.cpp src/core/Logger.cpp)
target_link_libraries(downloader_ui_tests PRIVATE downloader_ui)

set(SOURCES
    src/main.cpp
    src/core/Tool.h
    src/core/ToolManager.h
    src/core/ToolManager.cpp
    src/core/IconHelper.h
    src/core/Logger.h
    src/core/Logger.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.cpp
    src/tools/qr/QRTool.h
    src/tools/qr/QRTool.cpp
    src/tools/wifi/WifiTool.h
    src/tools/wifi/WifiTool.cpp
    src/tools/connect/ConnectTool.h
    src/tools/connect/ConnectTool.cpp
    src/tools/diskcleanup/DiskCleanupTool.h
    src/tools/diskcleanup/DiskCleanupTool.cpp
    src/tools/android/AndroidControlTool.h
    src/tools/android/AndroidControlTool.cpp
    src/tools/vpn/VpnControlTool.h
    src/tools/vpn/VpnControlTool.cpp
    src/tools/security/SecurityGatewayTool.h
    src/tools/security/SecurityGatewayTool.cpp
    src/tools/downloader/DownloaderTool.h
    src/tools/downloader/DownloaderTool.cpp
    src/tools/autoclick/AutoClickTool.h
    src/tools/autoclick/AutoClickTool.cpp
    src/tools/autoclick/AutoClickWindow.h
    src/tools/autoclick/AutoClickWindow.cpp
    src/tools/autoclick/model/Action.h
    src/tools/autoclick/model/Action.cpp
    src/tools/autoclick/model/ActionChain.h
    src/tools/autoclick/model/ActionChain.cpp
    src/tools/autoclick/storage/ActionSerializer.h
    src/tools/autoclick/storage/ActionSerializer.cpp
    src/tools/autoclick/engine/InputController.h
    src/tools/autoclick/engine/InputController.cpp
    src/tools/autoclick/engine/ActionRunner.h
    src/tools/autoclick/engine/ActionRunner.cpp
    src/tools/autoclick/capture/CoordinateOverlay.h
    src/tools/autoclick/capture/CoordinateOverlay.cpp
    src/tools/autoclick/capture/MouseCapture.h
    src/tools/autoclick/capture/MouseCapture.cpp
    src/tools/autoclick/capture/HotkeyCapture.h
    src/tools/autoclick/capture/HotkeyCapture.cpp
    src/tools/autoclick/capture/DragGestureCapture.h
    src/tools/autoclick/capture/DragGestureCapture.cpp
    src/tools/autoclick/overlay/RuntimeOverlay.h
    src/tools/autoclick/overlay/RuntimeOverlay.cpp
    src/tools/autoclick/widgets/ChainListWidget.h
    src/tools/autoclick/widgets/ChainListWidget.cpp
    src/tools/autoclick/widgets/ActionListWidget.h
    src/tools/autoclick/widgets/ActionListWidget.cpp
    src/tools/autoclick/widgets/ActionEditorWidget.h
    src/tools/autoclick/widgets/ActionEditorWidget.cpp
    assets/resources.qrc
    assets/app_icon.rc
)

add_executable(OneForAll WIN32 ${SOURCES})

target_include_directories(OneForAll PRIVATE
    src
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(OneForAll PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    qr_ui
    wifi_ui
    connect_ui
    diskcleanup_ui
    android_ui
    security_ui
    downloader_ui
    vpn_ui
    update_core
    user32
    dwmapi
)
