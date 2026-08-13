set(DAEMON_SOURCES
    dtkupdated.h
    dtkupdated.cpp
    main.cpp
)

add_executable(dtk-update-daemon ${DAEMON_SOURCES})

target_include_directories(dtk-update-daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(dtk-update-daemon PRIVATE
    dtk-update-core
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
    Dtk${DTK_SUFFIX}::Core
)

install(TARGETS dtk-update-daemon RUNTIME DESTINATION lib/dtk-update)
