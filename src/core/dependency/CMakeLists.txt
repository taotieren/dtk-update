set(DEPENDENCY_SOURCES
    dependencyresolver.h
    dependencyresolver.cpp
)

add_library(dtk-update-dependency STATIC ${DEPENDENCY_SOURCES})

target_include_directories(dtk-update-dependency PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
)

target_link_libraries(dtk-update-dependency PUBLIC
    dtk-update-common
    Qt${QT_VERSION_MAJOR}::Core
)
