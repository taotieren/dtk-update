set(SOURCES
    preupdatecheck.cpp
    postupdatecheck.cpp
)

add_library(dtk-update-healthcheck STATIC ${SOURCES})
target_include_directories(dtk-update-healthcheck PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
target_link_libraries(dtk-update-healthcheck PUBLIC
    dtk-update-common
    Qt${QT_VERSION_MAJOR}::Core
)
