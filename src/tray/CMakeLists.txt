set(TRAY_SOURCES
    dtkupdateplugin.h
    dtkupdateplugin.cpp
    traywidget.h
    traywidget.cpp
    traypopup.h
    traypopup.cpp
    tray.json
)

# dde-tray-loader 插件接口头文件路径（系统提供的 dock 插件 SDK）
find_path(DDE_DOCK_INCLUDE_DIR
    NAMES pluginsiteminterface.h
    PATH_SUFFIXES dde-dock
)

# 本地无 dde-dock SDK 时跳过（CI 镜像会提供，不影响发布）
if(NOT DDE_DOCK_INCLUDE_DIR)
    message(WARNING "dde-dock SDK not found, skip building tray plugin")
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
