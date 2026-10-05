cmake_minimum_required(VERSION 3.20)
project(OneForAll VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia Svg)

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
    src/tools/autoclick/overlay/RuntimeOverlay.h
    src/tools/autoclick/overlay/RuntimeOverlay.cpp
    src/tools/autoclick/widgets/ChainListWidget.h
    src/tools/autoclick/widgets/ChainListWidget.cpp
    src/tools/autoclick/widgets/ActionListWidget.h
    src/tools/autoclick/widgets/ActionListWidget.cpp
    src/tools/autoclick/widgets/ActionEditorWidget.h
    src/tools/autoclick/widgets/ActionEditorWidget.cpp
    assets/resources.qrc
)

add_executable(OneForAll WIN32 ${SOURCES})

target_include_directories(OneForAll PRIVATE
    src
)

target_link_libraries(OneForAll PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    user32
    dwmapi
)
