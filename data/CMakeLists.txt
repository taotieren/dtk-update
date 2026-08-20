# 应用桌面入口与托盘自启
install(FILES dtk-update.desktop
    DESTINATION share/applications
)
# 仅通用托盘自启（dde-dock 插件由 Dock 加载、GUI 由用户手动打开，均无需 autostart）。
# 历史遗留 dtk-update-tray.desktop（Exec=dtk-update-gui）已删除：其会导致开机弹出 GUI
# 主窗口并与 generic 托盘重复自启。
install(FILES dtk-update-tray-generic.desktop
    DESTINATION /etc/xdg/autostart
)

# DConfig 元数据（配置项集中定义，供控制中心/命令行调整）
install(FILES dconfig/org.deepin.dtk-update.json
    DESTINATION share/dsg/configs/org.deepin.dtk-update
)

# 后端配置文件示例（用户可复制到 /etc/dtk-update/backend.conf 或 ~/.config/dtk-update/backend.conf 修改）
install(FILES backend.conf.example
    DESTINATION share/dtk-update
    RENAME backend.conf.example
)

# DBus 激活服务描述（daemon 以 Type=dbus 启动时需要）
install(FILES com.dtk.update.Daemon.service
    DESTINATION share/dbus-1/services
)

# systemd 用户服务单元（相对 CMAKE_INSTALL_PREFIX=/usr，勿写 usr/lib/... 否则拼成 /usr/usr）
install(FILES ${CMAKE_SOURCE_DIR}/systemd/user/dtk-update.service
    DESTINATION lib/systemd/user
)
