set(UI_SOURCES
    mainwindow.h
    mainwindow.cpp
    main.cpp
)

add_executable(dtk-update-gui ${UI_SOURCES})

target_include_directories(dtk-update-gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(dtk-update-gui PRIVATE
    dtk-update-core
    Qt${QT_VERSION_MAJOR}::Widgets
    Dtk${DTK_SUFFIX}::Widget
    Dtk${DTK_SUFFIX}::Gui
)

install(TARGETS dtk-update-gui RUNTIME DESTINATION bin)
