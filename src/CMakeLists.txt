# 通用基础库：日志 / 配置 / 翻译
add_subdirectory(common)

# 业务核心（UI 无关，可独立单测）
add_subdirectory(core)

# 托盘插件（dde-tray-loader V2）
add_subdirectory(tray)

# 独立主窗口
add_subdirectory(ui)

# 后台常驻服务（可选，D-Bus）
add_subdirectory(daemon)
