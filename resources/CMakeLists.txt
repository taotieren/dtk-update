# 资源：图标与翻译
# SVG 图标同时编进 resources.qrc（前缀 icons 与 dsg-built-in-icons），供
# dtk-update-indicator 静态库及 dde-tray 插件 icon() 使用。

# 安装 SVG 到 hicolor 图标主题，作为 QIcon::fromTheme 的备选
install(FILES icons/dtk-update.svg
    DESTINATION share/icons/hicolor/scalable/apps
)
install(FILES icons/dtk-update-update.svg
    DESTINATION share/icons/hicolor/scalable/apps
)

# 安装控制中心设置图标（Attribute_CanSetting 必须，供控制中心读取）
install(FILES icons/dcc-dtk-update.dci
    DESTINATION ${DCC_ICON_INSTALL_DIR}
)
