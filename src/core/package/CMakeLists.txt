set(PACKAGE_SOURCES
    packagebackend.h
    packagebackend.cpp
    packageinfo.h
    packageparser.h
    packageparser.cpp
    backendfactory.h
    backendfactory.cpp
    aptbackend.h
    aptbackend.cpp
    dnfbackend.h
    dnfbackend.cpp
    linyapsbackend.h
    linyapsbackend.cpp
    pacmanbackend.h
    pacmanbackend.cpp
    zypperbackend.h
    zypperbackend.cpp
    snapbackend.h
    snapbackend.cpp
    flatpakbackend.h
    flatpakbackend.cpp
)

add_library(dtk-update-package STATIC ${PACKAGE_SOURCES})

target_include_directories(dtk-update-package PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(dtk-update-package PUBLIC
    dtk-update-common
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Concurrent
)
