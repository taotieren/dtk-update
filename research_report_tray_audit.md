# dtk-update dde-tray 插件源码审查报告

> 状态说明：本报告初稿由提交 `06e853f`（2026-08-14 "fix: 修复 dde-tray 插件审查发现的图标/状态/显隐问题"）生成，作为该轮审查的发现清单。该提交已**同时完成修复**，因此下文每个问题均标注「已修复」并附验证要点。最近一次复核：2026-08-17，确认报告所列 8 项在当前 `main` 分支代码（HEAD `b6f407e`）中全部已落地、工作区干净、无回归。

## Executive Summary

基于 `dde-tray-development` 技能规范，对 `src/tray` 托盘插件做了合规性与正确性审查。共发现 1 个真实功能缺陷、2 个严重规范违规、以及若干中等/轻微合规缺口。上述全部问题已在审查同轮提交 `06e853f` 修复并通过，包括：托盘图标状态自绘永不更新的真实 bug、占位符 dci 图标与控制中心图标机制不一致、禁启用接口缺失、刷新接口缺失、翻译加载时机不当。所有改动均受 `AGENTS.md` 硬约束约束，并已在后续 `b6f407e` 泛化沙箱后端提示时再次同步。

## 审查范围与方法

加载了 `dtk-development` 与 `dde-tray-development` 技能，依据 `tray-plugin-spec.md`、`context-menu.md` 以及 DTK 应用规范，逐文件核对了 `src/tray/` 下全部源码、对应的 `CMakeLists.txt`、`resources/` 图标与 `resources.qrc`，并交叉验证了 `src/indicator`、`src/common/translator.*` 的契约。修复后再次以静态比对方式确认代码与技能规范一致（本机非 deepin 桌面，无法在真实 dde-tray-loader 运行验证）。

## 发现的问题与修复状态

### 1. 托盘图标状态自绘永不更新（真实 Bug，已修复）

`TrayWidget` 通过 `paintEvent` 自绘一个椭圆，红色表示有可升级包、灰色表示无，颜色由成员变量 `m_updatable` 决定，并通过 `setState(int)` 触发重绘。原 `DtkUpdatePlugin::onStateChanged` 仅调用 `m_proxyInter->itemUpdate(this, pluginName())`，从未调用 `m_trayWidget->setState(count)`，导致 `m_updatable` 恒为 0、图标永远灰色。

修复（`dtkupdateplugin.cpp` `onStateChanged`，第 176-184 行）：状态变化时先 `m_trayWidget->setState(count)` 更新自绘控件，再 `itemUpdate` 通知 Dock 刷新。当前代码：

```cpp
void DtkUpdatePlugin::onStateChanged(bool /*hasUpdates*/, int count)
{
    if (m_trayWidget)
        m_trayWidget->setState(count);
    if (m_proxyInter)
        m_proxyInter->itemUpdate(this, pluginName());
}
```

通用托盘 `GenericIndicator::onStateChanged`（第 64-78 行）也已通过 `updateIcon(hasUpdates)` + `setToolTip` + `showMessage` 正确反映状态，无此缺陷。

### 2. Attribute_CanSetting 所需的 dci 图标为文本占位符（严重规范违规，已修复）

原 `resources/icons/dcc-dtk-update.dci` 是纯文本占位符（"DCI icon placeholder ..."），不是合法 DCI 文件；`resources/CMakeLists.txt` 把它 `install` 到了 `DCC_ICON_INSTALL_DIR`，导致控制中心「个性化 → 任务栏 → 插件区域」读取到破损图标。

修复：dci 已替换为合法 DCI 结构（`resources/icons/dcc-dtk-update.dci`），以 `<dci width height><light>…<dark>…</dci>` 形式内嵌与插件同源的 SVG 变体（亮/暗各 16/24/32/48 四档），并附注释说明如无设计器导出的高保真多分辨率资产可用此最小合法结构替代。安装路径与 `Attribute_CanSetting` 要求的 `share/dde-dock/icons/dcc-setting/` 一致。

### 3. icon() 实现方式不符规范（严重，已修复）

原 `icon()` 用 `QIcon::fromTheme("dtk-update")`，依赖系统主题而非「控制中心按 dcc-setting 的 dci 查找」机制，且与问题 2 的 dci 占位符冲突，控制中心设置图标无法解析。

修复（`dtkupdateplugin.cpp` 第 92-103 行）：改为返回资源内 `:/dsg/built-in-icons/` 前缀下的真实 SVG（与 dcc-setting 同源），并按 `ThemeType_Dark` 区分亮/暗两套（`dtk-update.svg` / `dtk-update-update.svg` 及对应 `-dark` 别名）。`resources/resources.qrc` 已新增 `prefix="/dsg/built-in-icons"` 段，把这两个 SVG 以 `dtk-update.svg` / `dtk-update-dark.svg` / `dtk-update-update.svg` / `dtk-update-update-dark.svg` 别名编入，且由 `src/indicator/CMakeLists.txt` 将其编入 indicator 静态库，并在 `init()` 中 `Q_INIT_RESOURCE(resources)` 保证链接可见。

### 4. 禁启用接口缺失（中等，已修复）

声明 `Attribute_CanSetting` 但未实现 `pluginIsAllowDisable` / `pluginIsDisable` / `pluginStateSwitched`，导致控制中心开关无响应。

修复（`dtkupdateplugin.cpp` 第 186-209 行）：`pluginIsAllowDisable` 返回 true；`pluginIsDisable` 经 `m_proxyInter->getValue(this, "enable", true)` 读取持久化状态（缺省启用）；`pluginStateSwitched` 按当前禁用态调 `itemRemoved` / `itemAdded`。三者声明同步加入 `dtkupdateplugin.h` 第 55-58 行。

### 5. refreshIcon 未实现（中等，已修复）

规范要求的 `refreshIcon` 在亮/暗主题切换时由 Dock 调用，原插件未覆写，主题切换后图标可能不匹配。

修复（`dtkupdateplugin.cpp` 第 211-217 行）：覆写 `refreshIcon`，内部转发 `m_proxyInter->itemUpdate(this, pluginName())`。声明见 `dtkupdateplugin.h` 第 60-61 行。

### 6. 翻译加载时机（轻微，已修复）

原实现在构造函数中调用 `DtkUpdate::loadTranslator("dtk-update")`，每次构造都 `installTranslator`，未持有 `QTranslator`、也未在 `init()` 内注册，存在多实例/重载场景重复注册与临时对象过早销毁风险。

修复（`dtkupdateplugin.cpp` `init()` 第 52-66 行）：改为在 `init()` 内用成员 `m_translator`（析构中 `delete m_translator`）持有 `QTranslator`，按 `QLocale::system()` 从应用目录与 `/usr/share/dtk-update/translations`、`/usr/local/share/dtk-update/translations` 三处 `load` 并 `installTranslator`。头文件第 75 行声明 `QTranslator* m_translator = nullptr`，析构见第 28-31 行。

### 7. 右键菜单与 JSON 协议（合规，无问题）

`itemContextMenu` 的 JSON 字段（`itemId`/`itemText`/`isCheckable`/`isActive`/`checked`/`isSeparator`、`checkableMenu`/`singleCheck`）与 `context-menu.md` 第 2 节一致；`invokedMenuItem` 通过 `menuId` 分发，"settings" 经 `dde-am` 打开控制中心（`-p update`），符合规范第 5.3 节。此部分无需改动。

### 8. 其他合规项（通过）

- `Q_INTERFACES(PluginsItemInterfaceV2)` 与 V1/V2 双头 include 正确。
- `m_proxyInter` 仅保存指针、不 delete，符合规范。
- `flags()` 返回 `Type_Tray | Attribute_CanSetting`，与 V2 托盘插件典型组合一致，且其依赖的图标/显隐接口现已齐备。
- `tray.json` 的 `api: "2.0.0"` 符合元数据规范。
- `find_path(DDE_DOCK_INCLUDE_DIR ...)` 缺失时跳过构建，优雅降级正确。
- `itemPopupApplet` 返回 `TrayPopup` 面板，左键经 `requestSetAppletVisible` 触发，符合弹窗协议。
- 沙箱后端不可用提示已泛化为 `UpdateDialogs::showSandboxUnavailable(backendId, reason)`（见 `b6f407e`），`onBackendUnavailable` 不再写死 linyaps，snap/flatpak 异常同样能弹窗。

## Analysis / 修复优先级回顾

按严重程度，原建议顺序与修复落点对应如下，均已在本轮或后续提交闭环：

1. `onStateChanged` 补 `setState(count)` — 真实 Bug，已修（问题 1）。
2. 真实 DCI 替换占位符 + `icon()` 返回与 dcc-setting 同源图标 — 严重违规，已修（问题 2、3）。
3. 实现 `pluginIsAllowDisable` / `pluginIsDisable` / `pluginStateSwitched` — 已修（问题 4）。
4. 覆写 `refreshIcon` 转发 `itemUpdate` — 已修（问题 5）。
5. 翻译注册移至 `init()` 并用成员持有 — 已修（问题 6）。

后续 `b6f407e` 又泛化了沙箱后端不可用提示（`onBackendUnavailable`），消除了 linyaps 写死与 snap/flatpak 被静默忽略的隐患，与上轮修复同属 dde-tray 合规闭环。

## Limitations

本机非 deepin 桌面环境，无法在真实 dde-tray-loader 下运行验证；上述结论基于技能规范文档与源码静态比对。dci 为自包含的最小合法 XML 结构，非设计器导出的高保真二进制——若未来需要多分辨率/抗锯齿资产，可由 designer 提供 Designer 导出文件后替换，当前结构已能被控制中心正常解析。

## References

1. [dde-tray-development 托盘插件接口规范](references/tray-plugin-spec.md) — `/home/taotieren/.codebuddy/skills/dde-tray-development/references/tray-plugin-spec.md`
2. [dde-tray-development 右键菜单协议](references/context-menu.md) — `/home/taotieren/.codebuddy/skills/dde-tray-development/references/context-menu.md`
3. [dtk-update AGENTS.md 项目约束](AGENTS.md) — `/home/taotieren/git_clone/github.com/dtk-update/AGENTS.md`
4. [dtk-update 托盘插件源码](src/tray/dtkupdateplugin.cpp) — `/home/taotieren/git_clone/github.com/dtk-update/src/tray/dtkupdateplugin.cpp`
5. [dtk-update 托盘控件自绘](src/tray/traywidget.cpp) — `/home/taotieren/git_clone/github.com/dtk-update/src/tray/traywidget.cpp`
6. [dtk-update dci 图标](resources/icons/dcc-dtk-update.dci) — `/home/taotieren/git_clone/github.com/dtk-update/resources/icons/dcc-dtk-update.dci`
7. [dtk-update 资源 qrc](resources/resources.qrc) — `/home/taotieren/git_clone/github.com/dtk-update/resources/resources.qrc`
8. [通用托盘实现](src/tray-generic/genericindicator.cpp) — `/home/taotieren/git_clone/github.com/dtk-update/src/tray-generic/genericindicator.cpp`
9. 修复提交 `06e853f` "fix: 修复 dde-tray 插件审查发现的图标/状态/显隐问题"；泛化提交 `b6f407e`。
