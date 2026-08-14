# 跨发行版更新指示器共享核心（与桌面环境解耦，供 dde-tray / generic-tray 复用）
add_library(dtk-update-indicator STATIC
    updateindicator.h
    updateindicator.cpp
    updatedialogs.h
    updatedialogs.cpp
)

target_include_directories(dtk-update-indicator PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(dtk-update-indicator PUBLIC
    dtk-update-core
    Qt${QT_VERSION_MAJOR}::Core
    Dtk${DTK_SUFFIX}::Core
    Dtk${DTK_SUFFIX}::Widget
)
