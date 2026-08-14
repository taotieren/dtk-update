# 通用基础库：日志 / 配置 / 翻译
add_subdirectory(common)

# 业务核心（UI 无关，可独立单测）
add_subdirectory(core)

# 跨发行版指示器共享核心（与桌面环境解耦；tray / tray-generic 复用）
add_subdirectory(indicator)

# dde-tray-loader 插件（deepin/UOS，依赖 dde-dock SDK；缺失则跳过）
add_subdirectory(tray)

# 通用 freedesktop 托盘（跨发行版备用方案，无 dde-dock 依赖）
add_subdirectory(tray-generic)

# 独立主窗口
add_subdirectory(ui)

# 后台常驻服务（可选，D-Bus）
add_subdirectory(daemon)
