# 包管理器后端抽象
add_subdirectory(package)
# 依赖解析
add_subdirectory(dependency)
# 安全提示
add_subdirectory(security)
# 预检/后检（pre/post update 分离）
add_subdirectory(healthcheck)
# 更新监控
add_subdirectory(monitor)

# 核心聚合库
add_library(dtk-update-core STATIC)

target_include_directories(dtk-update-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(dtk-update-core PUBLIC
    dtk-update-common
    dtk-update-package
    dtk-update-dependency
    dtk-update-security
    dtk-update-healthcheck
    dtk-update-monitor
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
)
