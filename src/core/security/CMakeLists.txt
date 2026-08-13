set(SECURITY_SOURCES
    securityadvisor.h
    securityadvisor.cpp
)

add_library(dtk-update-security STATIC ${SECURITY_SOURCES})

target_include_directories(dtk-update-security PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(dtk-update-security PUBLIC
    dtk-update-common
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
    Qt${QT_VERSION_MAJOR}::Network
)
