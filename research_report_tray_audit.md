# dtk-update dde-tray 插件源码审查报告

## Executive Summary

基于更新后的 deepin DTK 与 dde-tray-development 技能规范，对 `src/tray` 托盘插件源码进行了合规性与正确性审查。发现 1 个真实功能缺陷（托盘图标状态自绘永不更新）、2 个严重规范违规（`Attribute_CanSetting` 所需的 dci 图标是文本占位符、`icon()` 实现方式不符规范）、以及若干中等/轻微合规缺口（禁启用接口缺失、`refreshIcon` 未实现、翻译加载时机）。这些问题在 CI 编译阶段不会报错，但会在 deepin 桌面真实运行或控制中心设置时暴露。

## 审查范围与方法

加载了 `dtk-development` 与 `dde-tray-development` 技能，依据 `tray-plugin-spec.md`、`context-menu.md` 以及 DTK 应用规范，逐文件核对了 `src/tray/` 下全部源码、对应的 `CMakeLists.txt`、`resources/` 图标与 `resources.qrc`，并交叉验证了 `src/indicator`、`src/common/translator.*` 的契约。

## 发现的问题

### 1. 托盘图标状态自绘永不更新（真实 Bug，中等严重）

`TrayWidget` 通过 `paintEvent` 自绘一个椭圆，红色表示有可升级包、灰色表示无（`traywidget.cpp` 第 27-35 行），其颜色由成员变量 `m_updatable` 决定，并通过 `setState(int)` 触发重绘。但审查 `DtkUpdatePlugin::onStateChanged`（`dtkupdateplugin.cpp` 第 147-152 行）发现：状态变化时仅调用了 `m_proxyInter->itemUpdated(this, pluginName())`，**从未调用 `m_trayWidget->setState(count)`**。由于 `m_updatable` 初始为 0，`paintEvent` 永远绘制灰色椭圆，有更新时托盘图标不会变红或显示角标。

这是一个在桌面环境真实运行时的功能缺陷：用户无法从托盘图标本身感知更新状态，只能靠左键弹窗或控制中心。

### 2. Attribute_CanSetting 所需的 dci 图标为文本占位符（严重规范违规）

`dtkupdateplugin.h` 第 21、62 行声明了 `Dock::Type_Tray | Dock::Attribute_CanSetting`。规范 `tray-plugin-spec.md` 第 100、128 行明确要求：声明 `Attribute_CanSetting` 时**必须**实现 `icon()` 并**安装真实 `.dci` 图标**到 `share/dde-dock/icons/dcc-setting/`，供控制中心「个性化 → 任务栏 → 插件区域」读取。

实际 `resources/icons/dcc-dtk-update.dci` 的内容是纯文本占位符（"DCI icon placeholder ... Replace this text file with the real binary .dci ..."），不是二进制 DCI 文件。`resources/CMakeLists.txt` 第 16-19 行确实把它 `install` 到了 `DCC_ICON_INSTALL_DIR`，但安装的是占位文本。后果：控制中心设置页读取不到合法图标，显示破损图标或空白；且 `icon()` 返回的 `QIcon::fromTheme("dtk-update")` 依赖系统主题，与 dcc-setting 的 dci 查找机制并非同一路径，控制中心的设置图标条目无法正确解析。

### 3. icon() 实现方式不符规范（严重，与问题 2 关联）

规范第 108-124 行的示例以 `QIcon(":/dsg/built-in-icons/myplugin.svg")` 或资源/dcc-setting 路径返回图标。`DtkUpdatePlugin::icon()` 当前用 `QIcon::fromTheme(name)`（第 73 行），`name` 为 `dtk-update` 或 `dtk-update-update`。系统主题里虽有 hicolor 的 `dtk-update.svg` / `dtk-update-update.svg`，但这与「控制中心设置列表按 dcc-setting 的 dci 查找」机制不一致；且问题 2 的 dci 占位符使得 dcc 侧完全缺失。建议改为返回安装到 dcc-setting 的真实 dci 图标（并提供亮/暗两套）。

### 4. 禁启用接口缺失（中等）

规范第 8.3 节要求支持在控制中心禁用/启用插件：实现 `pluginIsAllowDisable()` 返回 true、`pluginIsDisable()` 查询状态、`pluginStateSwitched()` 响应开关并调用 `itemRemoved`/`itemAdded`。当前插件只声明了 `Attribute_CanSetting`（标榜可设置显隐），却未实现上述任一接口。结果是控制中心开关可能无响应或行为失控，与 `Attribute_CanSetting` 的语义不自洽。

### 5. refreshIcon 未实现（中等）

规范第 52 行 `refreshIcon` 在系统图标主题（亮/暗）变化时由 Dock 调用，用于刷新图标。当前插件仅在 `onStateChanged` 调 `itemUpdated`，未覆写 `refreshIcon`。在亮暗主题切换后，`icon()` 不一定被 Dock 重新调用，可能导致托盘/设置图标主题不匹配。

### 6. 翻译加载时机（轻微）

规范第 43 行建议插件在 `init()` 中创建并持有 `QTranslator`。当前 `DtkUpdatePlugin` 在构造函数中调用 `DtkUpdate::loadTranslator("dtk-update")`（`dtkupdateplugin.cpp` 第 22 行）。`loadTranslator` 用 `new QTranslator(QCoreApplication::instance())` 作为父对象，生命周期跟随 app 实例，不会过早销毁（这一点合规），但每次构造都执行一次 `installTranslator`，且未在 `init()` 内；若未来存在多实例/重载场景会重复注册。建议将翻译注册收敛到 `init()`。

### 7. 右键菜单与 JSON 协议（合规）

`itemContextMenu` 的 JSON 字段（`itemId`/`itemText`/`isCheckable`/`isActive`/`checked`、`checkableMenu`/`singleCheck`）与 `context-menu.md` 第 2 节完全一致；`invokedMenuItem` 通过 `menuId` 分发，且 `dde-am` 打开控制中心的方式符合规范第 5.3 节。此部分无问题。

### 8. 其他合规项（通过）

- `Q_INTERFACES(PluginsItemInterfaceV2)` 与 V1/V2 双头 include 已正确（这是上一轮修复的成果）。
- `m_proxyInter` 仅保存指针、不 delete，符合规范第 29、76 行。
- `flags()` 返回 `Type_Tray | Attribute_CanSetting` 符合 V2 托盘插件典型组合。
- `tray.json` 的 `api: "2.0.0"` 符合元数据规范。
- `find_path(DDE_DOCK_INCLUDE_DIR ...)` 缺失时跳过构建，优雅降级正确。
- `itemPopupApplet` 返回面板，左键通过 `requestSetAppletVisible` 触发，符合快捷面板/弹窗显示协议。

## Analysis / 修复优先级建议

按严重程度排序，建议：

1. 在 `onStateChanged` 内补 `m_trayWidget->setState(count)`，修复图标状态不更新（真实 Bug）。
2. 用真实 DCI 二进制替换 `resources/icons/dcc-dtk-update.dci` 占位符，并保证 `icon()` 返回与 dcc-setting 一致的真实图标（严重规范违规 + 控制中心破损）。
3. 实现 `pluginIsAllowDisable` / `pluginIsDisable` / `pluginStateSwitched`，使 `Attribute_CanSetting` 语义完整（中等）。
4. 覆写 `refreshIcon` 调用 `itemUpdated`（中等）。
5. 将 `loadTranslator` 调用移至 `init()`（轻微）。

这些改动均落在本仓库已建立的 dde-tray 开发约定内，且受 `AGENTS.md` 硬约束约束——后续实际修复时应再次加载 `dde-tray-development` 技能并跑 `clang-format --dry-run --Werror`。

## Limitations

本机非 deepin 桌面环境，无法在真实 dde-tray-loader 下运行验证；上述结论基于技能规范文档与源码静态比对。dci 二进制替换需设计资源工具链导出，建议由 designer 提供真实文件后再验收。

## References

1. [dde-tray-development 托盘插件接口规范](references/tray-plugin-spec.md) — `/home/taotieren/.codebuddy/skills/dde-tray-development/references/tray-plugin-spec.md`
2. [dde-tray-development 右键菜单协议](references/context-menu.md) — `/home/taotieren/.codebuddy/skills/dde-tray-development/references/context-menu.md`
3. [dtk-update AGENTS.md 项目约束](AGENTS.md) — `/home/taotieren/git_clone/github.com/dtk-update/AGENTS.md`
4. [dtk-update 托盘插件源码](src/tray/dtkupdateplugin.cpp) — `/home/taotieren/git_clone/github.com/dtk-update/src/tray/dtkupdateplugin.cpp`
5. [dtk-update 托盘控件自绘](src/tray/traywidget.cpp) — `/home/taotieren/git_clone/github.com/dtk-update/src/tray/traywidget.cpp`
6. [dtk-update dci 占位图标](resources/icons/dcc-dtk-update.dci) — `/home/taotieren/git_clone/github.com/dtk-update/resources/icons/dcc-dtk-update.dci`
