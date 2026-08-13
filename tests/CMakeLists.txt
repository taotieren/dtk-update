# 单元测试：core 层（与 UI 无关）
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    # 回退到 pkg-config / 直接查找
    find_library(GTEST_LIB NAMES gtest libgtest)
    find_library(GTEST_MAIN_LIB NAMES gtest_main libgtest_main)
endif()

# 注册 Qt Test 模块（顶层仅 find_package Core，未自动包含 Test target）。
# QSignalSpy 依赖 Qt::Test；缺失时信号相关用例跳过。
find_package(Qt${QT_VERSION_MAJOR}Test QUIET)

# 仅当确实可链接 GTest 时才构建测试，否则优雅跳过（避免破坏整体编译）
set(HAVE_GTEST FALSE)
if(GTest_FOUND AND TARGET GTest::gtest)
    set(HAVE_GTEST TRUE)
elseif(GTEST_LIB AND EXISTS ${GTEST_LIB})
    set(HAVE_GTEST TRUE)
endif()

if(HAVE_GTEST)
    set(TEST_SOURCES
        test_aptbackend.cpp
        test_dnfbackend.cpp
        test_backendfactory.cpp
        test_dependencyresolver.cpp
        test_securityadvisor.cpp
        test_iniparser.cpp
        test_distroprobe.cpp
        test_backendconfig.cpp
        test_healthcheck.cpp
        test_runprobe.cpp
    )
    # QSignalSpy 需要 Qt Test 模块；缺失时（如本地未装）跳过信号相关用例
    if(TARGET Qt${QT_VERSION_MAJOR}::Test)
        list(APPEND TEST_SOURCES test_updatemonitor.cpp test_runprivasync.cpp)
    else()
        message(WARNING "Qt Test module not found, skip test_updatemonitor/test_runprivasync (QSignalSpy)")
    endif()

    add_executable(test-core ${TEST_SOURCES})
    target_include_directories(test-core PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/src/core
    )
    target_link_libraries(test-core PRIVATE
        dtk-update-core
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::DBus
    )
    # GTest 链接：优先使用 imported target，否则回退原始库路径
    if(GTest_FOUND AND TARGET GTest::gtest)
        target_link_libraries(test-core PRIVATE GTest::gtest GTest::gtest_main)
    else()
        target_link_libraries(test-core PRIVATE ${GTEST_LIB} ${GTEST_MAIN_LIB})
    endif()
    # Qt Test 模块（QSignalSpy）可选：存在则链接
    if(TARGET Qt${QT_VERSION_MAJOR}::Test)
        target_link_libraries(test-core PRIVATE Qt${QT_VERSION_MAJOR}::Test)
    endif()
    add_test(NAME test-core COMMAND test-core)
else()
    message(WARNING "GTest not found, skipping unit tests")
endif()
