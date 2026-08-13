# 资源：图标与翻译
# 应用内使用的 SVG 图标（dci 由设计资源提供）
set(RESOURCE_SOURCES
    resources.qrc
)

# 安装 SVG 到 hicolor 图标主题，使 QIcon::fromTheme("dtk-update") 可用
install(FILES icons/dtk-update.svg
    DESTINATION share/icons/hicolor/scalable/apps
)
install(FILES icons/dtk-update-update.svg
    DESTINATION share/icons/hicolor/scalable/apps
)

# 安装控制中心设置图标（Attribute_CanSetting 需要）
install(FILES icons/dcc-dtk-update.dci
    DESTINATION ${DCC_ICON_INSTALL_DIR}
    OPTIONAL
)
