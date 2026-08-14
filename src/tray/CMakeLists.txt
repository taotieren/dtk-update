set(TRAY_SOURCES
    dtkupdateplugin.h
    dtkupdateplugin.cpp
    traywidget.h
    traywidget.cpp
    traypopup.h
    traypopup.cpp
    tray.json
)

# dde-tray-loader 插件是 deepin/UOS 专属组件，dde-dock SDK 仅存在于 deepin 系源。
# 设为可选依赖：非 deepin 环境（Debian/Ubuntu/Fedora/Arch 等）不装该 SDK 也能完整编译
# 其余 target（通用托盘、GUI、daemon、core + tests）；仅 dde-tray 插件被跳过。
option(BUILD_DDE_TRAY "Build the dde-dock tray plugin (requires dde-dock SDK)" ON)

# dde-tray-loader 插件接口头文件路径（系统提供的 dock 插件 SDK）
find_path(DDE_DOCK_INCLUDE_DIR
    NAMES pluginsiteminterface.h
    PATH_SUFFIXES dde-dock
)

# 显式关闭、或本地无 dde-dock SDK 时跳过（不影响其余 target 编译）
if(NOT BUILD_DDE_TRAY)
    message(STATUS "BUILD_DDE_TRAY=OFF, skip building dde-dock tray plugin")
    return()
endif()

if(NOT DDE_DOCK_INCLUDE_DIR)
    message(STATUS "dde-dock SDK not found, skip building dde-dock tray plugin "
                   "(install dde-dock-dev / dde-tray-loader-dev to enable it)")
    return()
endif()

add_library(dtk-update-tray MODULE ${TRAY_SOURCES})

target_include_directories(dtk-update-tray PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${DDE_DOCK_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(dtk-update-tray PRIVATE
    dtk-update-indicator
    Qt${QT_VERSION_MAJOR}::Widgets
    Dtk${DTK_SUFFIX}::Widget
    Dtk${DTK_SUFFIX}::Gui
    Dtk${DTK_SUFFIX}::Core
)

set_target_properties(dtk-update-tray PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins
)

install(TARGETS dtk-update-tray
    LIBRARY DESTINATION ${PLUGIN_INSTALL_DIR}
)
