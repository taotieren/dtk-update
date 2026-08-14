# AGENTS.md

面向 **dtk-update** 项目的 AI 编程助手与贡献者指南。在动手改动前请先阅读本文件，
它沉淀了架构、硬性约束，以及已经踩过的坑，避免重复犯错。

## 项目概览

跨发行版的 DTK 更新管理器托盘插件。包操作委托给可插拔的后端抽象（apt/dnf/linyaps），
绝不绑定某一发行版或包管理器。命名空间 `DtkUpdate`；日志类别 `dtk.update.*`。
DConfig appId 为 `org.deepin.dtk-update`。

二进制 / D-Bus 服务：`dtk-update-gui`、`dtk-update-daemon`（D-Bus 名 `com.dtk.update.Daemon`）、
`dtk-update-tray`（deepin Dock 插件）、`dtk-update-tray-generic`（独立通用托盘）。

## 架构地图（代码位置）

- `src/core` — 与 UI 无关的纯业务逻辑，已完整单元测试覆盖。**严禁**在此引入
  Dock/Tray/UI 头文件。
  - `package/` — `PackageBackend`（抽象基类，公共实现已下沉此处）·
    `AptBackend` · `DnfBackend` · `LinyapsBackend` · `BackendFactory`（注册表 +
    自动探测）· `PackageParser`。
  - `monitor/` — `UpdateMonitor`（聚合后端、状态机、并发锁、D-Bus/网络监听）。
  - `dependency/` — `DependencyResolver`（后端 dry-run 输出解析）。
  - `healthcheck/` — `PreUpdateCheck` / `PostUpdateCheck`（仅检查，不修改）。
  - `security/` — `SecurityAdvisor`（上游安全公告拉取，超时安全）。
- `src/common` — `iniparser`、`distroprobe`、`presetconfig`、`backendconfig`、
  `appconfig`、`systeminfo`。无第三方依赖。
- `src/indicator` — `UpdateIndicator`（桌面无关的共享核心，静态库，被两个托盘继承）
  + `UpdateDialogs`（共享的 DDialog 构建器）。
- `src/tray` — deepin/UOS Dock 插件（仅当探测到 `dde-dock` SDK 时才构建）。
- `src/tray-generic` — 基于 `QSystemTrayIcon` 的独立托盘，无 deepin 私有依赖。
- `src/ui` — `MainWindow`（DTK）。`src/daemon` — D-Bus 服务。两者均不依赖 dde-dock。

## 硬性约束（不可违反）

1. **提权**：所有写操作经由 `PackageBackend` 通过 `pkexec` 执行，绝不在进程内
   `system("sudo ...")`。
2. **多后端**：新增包管理器以 `PackageBackend` 子类形式加入；monitor 不得硬编码 apt/dnf。
3. **绝不替用户做决定**：前检/后检仅做检查；应用更新必须弹出明确的用户确认框，
   且默认焦点落在**取消**按钮上。
4. **`src/core` 保持无 UI**：不得包含 Dock/UI/Tray 相关头文件。
5. **异步写**：安装/卸载等通过 `runPrivilegedAsync`（后台线程 +
   `operationFinished` 信号）执行。绝不在 GUI/托盘主线程 `waitForFinished(-1)`，
   否则会冻结整个 DDE 桌面。

## 约定

- 子类化 `PackageBackend`；仅覆盖语义虚函数 + `privilegedPrefix()` + 健康探针。
  在 `BackendFactory::registry()` 登记，**同时**把 id 加入 `PresetConfig::knownBackendIds()`。
- `isAvailable()` 必须探测**全部**关键命令**并**做轻量冒烟测试——缺失 `rpm`/`dpkg-query`
  必须返回 false，而非“命令存在即可用”（即“伪可用”陷阱）。
- 健康探针默认 `support=false`，按后端覆盖。`needs-restarting` 退出码 `1` 表示建议重启、
  `0` 表示不需要；探针必须用 `runProbe`（读取退出码）而非 `runQuery`（会丢弃非零输出）。
- 区域设置：解析机器可读的包输出前注入 `LC_ALL=C`（`applyStableLocale`），否则非英文
  区域会让解析失败。
- 配置为只读的 `AppConfig`；优先级 = 用户 `backend.conf` > DConfig > 发行版预设。
  `iniparser` 键保留原始大小写、查询大小写不敏感；命中判定用 `!isEmpty()`
  （空字符串是合法值，不代表“缺失”）。
- 本地化：使用 `tr()`；刷新时用 `lupdate` 后接 `python3 translations/_gen.py`
  （回填 zh_CN/es/fr/de，剔除未完成的条目）。共五种语言。
- 代码风格：`.clang-format`（LLVM/Allman，4 空格，100 列）。改动文件请跑
  `clang-format -i`；CI 内有 `clang-tidy`。

## 可用 Skills（AI 助手应优先调用的技能）

当任务涉及对应领域时，应主动加载以下技能以提升正确性与效率（通过 `use_skill`
工具，参数为技能名）：

- **dtk-development**：DTK（deepin Tool Kit）桌面应用开发维护，同时支持 DTK5 与 DTK6。
  凡涉及创建/修改 DTK QWidget/QML 应用、主题、调色板、DCI 图标、DConfig、D-Bus、
  通知、单实例、窗口效果、平台适配，或编译调试 DTK 源码时启用。本项目即基于 DTK6，
  改 UI/托盘/daemon 时优先用此技能。
- **dde-shell-development**：deepin/UOS v25 的 dde-shell Applet、Containment、Panel
  插件及 LayerShell 窗口开发。涉及 Dock/顶栏/侧栏等 Shell 面板、Shell QML API、
  插件桥接，或贴靠屏幕边缘的 Wayland 窗口时使用。
- **dde-tray-development**：dde-tray-loader 托盘与快捷面板插件开发。实现
  `PluginsItemInterfaceV2`、托盘图标、`PluginFlags`、快捷面板控件与详情页、Dock
  消息协议、右键菜单或排查插件加载显示问题时使用。本项目的 `src/tray`（deepin Dock
  插件）即属于此范畴。
- **dde-control-center-development**：deepin/UOS v25 的 DDE 控制中心框架与插件开发维护。
  当需要新增/修改控制中心设置模块、模块树与搜索、DccObject、控制中心 QML 组件、
  D-Bus 数据交互、插件构建安装或加载调试时使用。（本项目暂未直接依赖，但若扩展
  更新设置项到控制中心可参考。）
- **github-actions-templates**：生成生产级 GitHub Actions 工作流，用于自动化测试、
  构建与部署。本项目的 `build.yml`/`test.yml`/release 流程改动时优先参考，确保多架构
  CI 写法规范。
- **codebuddy-md-improver**：审计并改进仓库内的 `CODEBUDDY.md`/`AGENTS.md` 类文件。
  本次重写本文件后，可再用此技能做一轮合规审计。

> 说明：技能会注入领域知识、标准化工作流（SOP）与可执行脚本/工具。加载技能后须遵循
> 其指令。若任务可被通用推理可靠完成，则不强制加载技能。

## 已踩过的坑（避免重犯）

- **内存**：`QLockFile` 不是 `QObject`——不要把它 `new` 成成员却不写析构 `delete`；
  优先用值成员。可选的子对象所持有的后端（如 `UpdateMonitor::m_linyaps`）用 `QPointer`，
  父对象删除时自动置空。跨对象的裸指针（`m_backend`、`m_config`）由外部所有，持有方
  不得 `delete`。
- **单元测试确定性**：不要在 `UpdateMonitor` 构造内自动接入真实后端（如 Linyaps），
  这会破坏 `CheckNow` 测试。前端应显式调用
  `BackendFactory::attachLinyaps(m_monitor, m_config, this)`。
- **Qt6 D-Bus**：`QDBusConnection::connect` 没有 functor 重载——D-Bus 信号必须用旧式
  `SLOT()` 字符串连接，且槽参数要匹配（`onPrepareForSleep(bool)`、`onNmStateChanged(uint)`）。
  无参 `SLOT()` 会在运行时静默丢弃参数（例如断开时也触发一次检查）。`NM_STATE_CONNECTED_GLOBAL` = 70。
- **DependencyResolver**：按 `backendId()` 分流解析（APT 用 `^Inst/`/`^Remv`，DNF 用
  `Installing:/Removing:`）；无结构化输出的后端降级为仅目标包而非失败。`parseSimulateOutput`
  是 `static` 方法——内部不得访问非静态的 `m_backend`。
- **CI 打包**：deb 来自 **debootstrap deepin beige rootfs + chroot**（amd64/arm64 用原生
  runner；loong64 用 `qemu-user-static`），而非 `ubuntu:devel`。chroot 内执行
  `apt-get build-dep -y /src`（不要手动镜像 Build-Depends，否则会漏装 `pkg-config`）。
  在架构匹配的 chroot 内**不要**给 `dpkg-buildpackage` 传 `-a<arch>`（会强制交叉构建模式
  从而失败）。`debian/rules` 必须 `+x`。`upload-artifact` 永远压成 zip；真正的 `.deb`
  文件走 Release 资产，而非 artifact。
- **测试**：必须显式注册 `Qt::Test`（`find_package(Qt${QT_MAJOR_VERSION}Test)`），
  否则测试文件被静默跳过。不要跨编译单元定义两个 `QCoreApplication` 实例或两个同名 mock
  类。涉及 `attachLinyaps` / 真实后端的测试，在管理器未安装时应 `SKIP` 而非伪通过。
- **死代码**：删除功能时，要连其 config 键、getter/setter、DConfig 条目、`showConfig()`
  行一起删。不要留下“虚拟”开关却无人读取。诚实的零/空胜过看起来合理但实际的占位数字。
- **文档漂移**：重构后要在同一改动里修正 README（英文 + 简中）——打包脚本
  （`ci/package-deb.sh`，不是 `multiarch-build.sh`）、接线
  （`BackendFactory::attachLinyaps`，不是构造内自接）、以及 `backendId()` 与
  `backendType()` 的引用都曾发生过漂移。

## 构建与验证

```bash
sudo apt build-dep .            # 宿主依赖；CI 内 chroot 镜像此步骤
mkdir -p build && cd build && cmake .. && make -j$(nproc)
ctest --output-on-failure        # 预期 66 passed + 3 skipped（非 debian 开发机
                                  # apt/dnf/linyaps 不可用 -> SKIP）
```

本地机通常为 Arch/DTK6，apt/dnf 后端 `isAvailable()` 返回 false，相关测试 `SKIP`——
这是预期，不是回归。deepin Dock 托盘（`src/tray`）本地被跳过（无 `dde-dock` SDK），
在 CI 中构建。

## 长任务后检查清单（防遗漏 / 未实现 / 死代码）

任何跨多文件、多轮次的重构或功能开发收尾时，必须逐项核对，避免“文档写了但代码没做”
或“代码删了但文档/配置还引用”两类漂移：

1. **空目录 / 孤儿文件**：删除已无源码的目录（如本轮的 `src/core/cleanup/`，空目录 git
   不跟踪，需手动 `rm -rf` 并确认未被 `CMakeLists.txt` 引用）。检查 `tests/`、`src/**`
   下有无不被任何 target 编译的 `.cpp/.h`。
2. **声明 vs 实现**：README/AGENTS/控制文件里提到的能力，逐一对照 `src/` 确认真的有实现，
   尤其“清理 / cleanup / 安全公告 / 后检报告”等名词要与模块名（如 `healthcheck/`）一致，
   不要留“residue cleaner”这类旧称。
3. **冗余状态 / 死成员**：找出“只在构造里赋值、调用处又重复判断”的成员变量（如被移除的
   `SecurityAdvisor::m_available`），优先用调用时实时探测替代一次性快照。
4. **可疑写法**：`const_cast`、裸 `new`、非 RAII 句柄、无参 `SLOT()` 等，参见上方「已踩过的坑」。
5. **配置键一致性**：新增/删除功能时，`AppConfig::backendOptions`、`showConfig()` 输出、
   DConfig schema、示例 `data/backend.conf.example`、README 后端扩展指南要同步增删，
   不要留无人读取的“虚拟”开关。
6. **构建与测试零回归**：`cmake + make + ctest` 必须全绿（预期 66 passed + 3 skipped）；
   跑 `clang-format -i` 覆盖改动文件；`read_lints` 无错误；`git status` 确认无遗留草稿文件。
7. **文档同步**：本文件与 README（en / zh-CN）在同一改动里更新；changelog 追加版本条目，
   措辞与代码模块名对齐。
8. **按功能分组提交**：不同性质的改动（清理 / 修复 / 文档）拆成独立 commit，commit message
   用 `cleanup:`/`fix:`/`refactor:`/`docs:` 前缀，便于回溯，不要混成一团。

