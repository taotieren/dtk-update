# 通用 freedesktop 托盘（跨发行版备用方案，无 dde-dock 依赖）
# 使用 Qt6 原生 QSystemTrayIcon，任何提供 DTK6 + Qt6 的发行版均可构建。
add_executable(dtk-update-tray-generic
    genericindicator.h
    genericindicator.cpp
    main.cpp
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
