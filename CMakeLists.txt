cmake_minimum_required(VERSION 3.20)
project(OneForAll VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

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
