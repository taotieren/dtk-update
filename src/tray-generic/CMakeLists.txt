# 通用 freedesktop 托盘（跨发行版备用方案，无 dde-dock 依赖）
# 使用 Qt6 原生 QSystemTrayIcon，任何提供 DTK6 + Qt6 的发行版均可构建。
# 注意：资源必须经本可执行文件直接编译，因为 indicator 静态库里的 resources.qrc
# 在静态链接下会被链接器惰性丢弃（generic 的 Q_INIT_RESOURCE 引用无法触发提取）。
qt_add_resources(GENERIC_QRC ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
add_executable(dtk-update-tray-generic
    genericindicator.h
    genericindicator.cpp
    main.cpp
    ${GENERIC_QRC}
)

target_include_directories(dtk-update-tray-generic PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(dtk-update-tray-generic PRIVATE
    dtk-update-indicator
    Qt${QT_VERSION_MAJOR}::Widgets
    Dtk${DTK_SUFFIX}::Widget
    Dtk${DTK_SUFFIX}::Gui
)

install(TARGETS dtk-update-tray-generic RUNTIME DESTINATION lib/dtk-update)
