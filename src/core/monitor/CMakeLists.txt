set(MONITOR_SOURCES
    updatemonitor.h
    updatemonitor.cpp
)

add_library(dtk-update-monitor STATIC ${MONITOR_SOURCES})

target_include_directories(dtk-update-monitor PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(dtk-update-monitor PUBLIC
    dtk-update-common
    dtk-update-package
    dtk-update-security
    dtk-update-healthcheck
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
)
