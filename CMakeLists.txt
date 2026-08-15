cmake_minimum_required(VERSION 3.13)

project(dtk-update
    VERSION 0.1.0
    DESCRIPTION "DTK tray applet for monitoring and managing system package updates across distributions"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 本项目含静态库（common/core/indicator）会被链接进共享对象（dde-dock 托盘插件
# libdtk-update-tray.so）。aarch64/loong64 等架构在链接 .so 时强制要求所有目标文件以
# -fPIC 编译，否则报 "unsupported relocation / recompile with -fPIC"。开启全局 PIC 使所有
# target 一致可重定位，避免架构相关的链接失败。
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---- 自动探测 Qt / DTK 主版本（兼容 DTK5/Qt5 与 DTK6/Qt6）----
# 本地无 DTK6 的开发机可设 -DDEEPIN_UPDATE_FORCE_QT5=ON 用 Qt5/DTK5 验证编译
if(DEEPIN_UPDATE_FORCE_QT5)
    set(QT_VERSION_MAJOR 5)
    find_package(Qt5 REQUIRED COMPONENTS Core Widgets DBus Concurrent)
    set(QT_VERSION_MAJOR 5)
else()
    find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core)
endif()
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS
    Core
    Widgets
    DBus
    Concurrent
)

if(QT_VERSION_MAJOR EQUAL 6)
    set(DTK_SUFFIX 6)
else()
    set(DTK_SUFFIX "")
endif()

find_package(Dtk${DTK_SUFFIX}Core REQUIRED)
find_package(Dtk${DTK_SUFFIX}Gui REQUIRED)
find_package(Dtk${DTK_SUFFIX}Widget REQUIRED)

# ---- 全局编译选项 ----
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
    # 关闭部分噪音警告（第三方头文件引发）
    add_compile_options(-Wno-unused-parameter)
endif()

# ---- 应用名（参与二进制/DBus/桌面/服务命名与资源安装路径）----
add_definitions(-DAPP_NAME=\"dtk-update\")

# ---- 安装路径约定 ----
include(GNUInstallDirs)
set(PLUGIN_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/dde-dock/plugins")
set(DCC_ICON_INSTALL_DIR "share/dde-dock/icons/dcc-setting")
set(TRANSLATION_INSTALL_DIR "share/dtk-update/translations")

# ---- 子目录 ----
add_subdirectory(src)
add_subdirectory(resources)
add_subdirectory(translations)
add_subdirectory(data)

# ---- 测试 ----
option(BUILD_TESTING "Build unit tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
