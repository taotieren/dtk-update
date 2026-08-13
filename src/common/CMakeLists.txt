set(COMMON_SOURCES
    logger.cpp
    appconfig.cpp
    translator.cpp
    iniparser.cpp
    distroprobe.cpp
    presetconfig.cpp
    backendconfig.cpp
    systeminfo.cpp
)

add_library(dtk-update-common STATIC ${COMMON_SOURCES})

target_include_directories(dtk-update-common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(dtk-update-common PUBLIC
    Qt${QT_VERSION_MAJOR}::Core
    Dtk${DTK_SUFFIX}::Core
)
