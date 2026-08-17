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
    `AptBackend` · `DnfBackend` · `PacmanBackend` · `ZypperBackend` · `LinyapsBackend`
    · `SnapBackend` · `FlatpakBackend` · `BackendFactory`（注册表 + 自动探测）· `PackageParser`。
  - `monitor/` — `UpdateMonitor`（聚合后端、状态机、并发锁、D-Bus/网络监听）。
  - `dependency/` — `DependencyResolver`（后端 dry-run 输出解析）。
  - `healthcheck/` — `PreUpdateCheck` / `PostUpdateCheck`（仅检查，不修改）。
  - `security/` — `SecurityAdvisor`（安全公告聚合：deepin 安全中心 D-Bus + 发行版官方源 +
    离线启发式兜底；另含发行版「最近新闻 / 通知」抓取，与包名无关仅展示）。
- `src/common` — `iniparser`、`distroprobe`、`presetconfig`、`backendconfig`、
  `appconfig`、`systeminfo`。无第三方依赖。
- `src/indicator` — `UpdateIndicator`（桌面无关的共享核心，静态库，被两个托盘继承）
  + `UpdateDialogs`（共享的 DDialog 构建器）。
- `src/tray` — deepin/UOS Dock 插件（仅当探测到 `dde-dock` SDK 时才构建）。
- `src/tray-generic` — 基于 `QSystemTrayIcon` 的独立托盘，不依赖 dde-dock / dde-shell 私有接口（可用 DTK 框架做亮暗主题刷新，属正常框架依赖，非私有 SDK）。
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
  完整注册清单（新增后端切勿遗漏任一项，否则会出现"文档/代码引用漂移"）：
  1. `src/core/package/` 新增 `XxxBackend.{h,cpp}`，仿 `linyapsbackend.cpp` 写语义虚函数与 `privilegedPrefix()`；
  2. `package/CMakeLists.txt` 的 `dtk_update_core` 源文件列表加入 `xxxbackend.h`/`xxxbackend.cpp`；
  3. `BackendFactory::registry()` 增加 `{id, [](){ return new XxxBackend(); }}` 条目；
  4. `PresetConfig::knownBackendIds()` 加入该 id（与 registry 对齐）；
  5. 若该后端属**沙箱式应用商店**（见下），还需在 `BackendFactory::sandboxIds()` 列表追加 id，
     并由 `attachSandboxBackends` 自动探测接入（无需改 UI 调用点）。
  - **系统级后端登记完成提示**：`PacmanBackend`（Arch/Manjaro）与 `ZypperBackend`
    （openSUSE/SLES）**已实现并完整登记**：`registry()`、`knownBackendIds()`、
    `package/CMakeLists.txt` 源列表、`BackendType` 枚举均含二者，且 `PresetConfig::defaultBackendFor()`
    的 `arch→pacman` / `suse→zypper` 预留已连通。它们与 apt/dnf 同为**系统级后端**
    （强绑定发行系），**不**进入 `sandboxIds()`，无需改 `attachSandboxBackends`。pacman 无 recommends
    概念、`backendOptions()` 已移除 `noInstallRecommends`；zypper 复用 rpm 查询并支持
    `checkConfigFilesToReview`（*.rpmnew/*.rpmsave/*.rpmorig）。
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
- **字符串变更联动（硬纪律）**：凡是改动源码中面向用户的可见字符串（UI 文案、托盘提示、
  对话框、通知、安全公告确认/后检报告、错误信息等），必须同步刷新翻译文件
  （`lupdate` + `python3 translations/_gen.py`，重填并剔除 unfinished 条目），并在同一改动里
  更新文档（README 的 en / zh-CN、必要时 AGENTS.md）中引用到该文案/术语/能力描述的段落。
  勿留"代码已改、ts 未刷、文档还写旧措辞"的漂移——这是高频复发问题。新增/删除能力时，
  `tr()` 字符串、`ts` 文件、README 后端扩展指南、`backend.conf.example` 要一并增删，
  不要留无人读取的"虚拟"开关或孤立文案。
- 代码风格：`.clang-format`（LLVM/Allman，4 空格，100 列，
  `IncludeBlocks: Regroup` + `SortIncludes: true`）。**改动文件必须跑
  `clang-format -i`**；CI 有 `find src tests \( -name '*.cpp' -o -name '*.h' \) | xargs
  clang-format --dry-run --Werror` 门禁（失败 exit 123），提交前务必本地先过一遍。
  特别注意：**include 顺序由工具按字母序自动重排**，引号 include（如
  `"core/monitor/updatemonitor.h"`）会排在 `"dnfbackend.h"` 之前，手改 include
  顺序极易触发该 CI 失败，直接交给 `clang-format -i` 处理即可。CI 内另含 `clang-tidy`。

## 可用 Skills（AI 助手应优先调用的技能）

> **硬性约束**：本项目的**所有修复（fix）与开发（feature/refactor）工作，都必须先通过
> `use_skill` 加载对应的 deepin/DTK 技能**，再动手改动代码或文档。技能会注入领域知识、
> 标准化工作流（SOP）与可执行脚本，能显著降低踩坑概率。绝不可用通用推理替代技能加载——
> 只有当任务确实落在任一技能覆盖领域之外时，才允许跳过加载。

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
- **Issue / PR 处理**：环境具备 `gh` CLI 或 GitHub MCP（如本会话的 `GitHub` MCP 服务器，
  提供 `list_issues`/`get_issue`/`create_issue`/`list_pull_requests`/`get_pull_request_*`/
  `add_issue_comment` 等）时，优先用之拉取与回复仓库 Issue / PR；无该能力须显式告知用户并
  请求授权，严禁假装已核查（详见下方「Issue / PR 监听与闭环处理」章节）。

> 说明：技能会注入领域知识、标准化工作流（SOP）与可执行脚本/工具。加载技能后须遵循
> 其指令。本项目的修复与开发任务均须优先加载对应技能（见上方硬性约束），仅当任务确无
> 任何技能覆盖时才允许不加载。

## 已踩过的坑（避免重犯）

- **内存**：`QLockFile` 不是 `QObject`——不要把它 `new` 成成员却不写析构 `delete`；
  优先用值成员。可选的子对象所持有的后端（如 `UpdateMonitor::m_linyaps`）用 `QPointer`，
  父对象删除时自动置空。跨对象的裸指针（`m_backend`、`m_config`）由外部所有，持有方
  不得 `delete`。
- **异步网络**：`SecurityAdvisor` 所有网络访问均为异步（`QNetworkAccessManager` + 信号槽 +
  超时 `QTimer::singleShot` 中断），绝不 `QEventLoop::exec()` 阻塞调用线程。上游安全公告
  在 `UpdateMonitor::checkNow` 拿到可升级列表后 `prefetchUpstream` 异步预取并缓存，
  `applyUpdates` 内的 `fetchAdvisories` 同步合并缓存（永不发起网络）。发行版「最近新闻 /
  通知」经 `fetchDistroNotices` 异步拉取，结果由 `UpdateIndicator::distroNoticesReady` 转发
  到前端弹窗。`m_fetchUpstream` 默认 false，需用户显式开启（`AppConfig::fetchUpstreamAdvisories`）。
- **单元测试确定性**：不要在 `UpdateMonitor` 构造内自动接入真实后端（如 Linyaps），
  这会破坏 `CheckNow` 测试。前端应显式调用
  `BackendFactory::attachSandboxBackends(m_monitor, m_config, this)`（一次性接入 linyaps/snap/flatpak
  全部沙箱后端；旧的 `attachLinyaps` 保留为仅接 linyaps 的兼容封装）。
- **沙箱式应用商店后端（linyaps / snap / flatpak）共性约定**：
  - 它们与系统包管理器（apt/dnf）**正交**：跨发行系、只管沙箱应用、更新不触内核/系统服务。
    `UpdateMonitor` 用 `m_sandboxBackends`（`QList<QPointer<PackageBackend>>`）持有**所有**沙箱后端，
    `checkNow` 逐个聚合、`proceedUpdate` 按 `backendId` 通用分组路由，切勿再硬编码 `linyaps`。
  - **沙箱后端与系统后端的本质区别（必须区别对待）**：系统包管理器（apt/dnf）与发行版**强绑定**——
    一个发行系通常只有一种、且在安装该系统时即确定存在；其可用性是"发行系身份"的推论，
    逻辑上可视为"恰好一个 / 必存在"。沙箱式应用商店则**与发行系无关**：一台机器上可能
    **一个都没有**（纯净最小化系统未装 snapd/flatpak/linglong），也可能**同时存在多个**
    （如同时装了 snap + flatpak + linyaps），且每个都依赖自己独立运行环境健康
    （daemon 在跑、有远端、运行时未损坏）。因此沙箱后端绝不可像系统后端那样假设"必有且仅有一个"，
    而必须**逐个独立探测 + 运行时可用才接入**：`attachSandboxBackends` 遍历 `sandboxIds()`，
    每个 `isAvailable()` 为真才 `setSandboxBackend` 接入参与聚合，为假直接丢弃，**不报错、不回退**
    到任何"默认沙箱后端"。UI 与 monitor 不得对沙箱后端数量做任何假设（0/1/N 都合法）：
    可升级列表、更新确认、后检报告都按"这些后端当前真实可用集合"动态生成，按 `backendId`
    分组路由，绝不写死 linyaps 或假定 snap/flatpak 一定在。这种"正交 + 多实例 + 按需探测"
    的模型是沙箱后端与 apt/dnf 类系统后端最关键的架构差异，新增沙箱后端须沿用此范式。
  - 提权：`privilegedPrefix()` 返回**空**（snapd/flatpak 经自身 polkit 策略提权，不套 pkexec）。
  - 四探针（重启/服务/残留配置/失败 unit）**全部 `support=false`**：沙箱应用不触系统层。
  - `isAvailable()` 必须探测真实运行环境，防"命令在但 daemon 没起"的伪可用陷阱：
    snap 冒烟 `snap list --unicode=never`；flatpak 需 `flatpak remotes` 至少存在一个远端（纯净最小化
    系统可能装了 flatpak 却无任何远端，remote-ls 无意义）。
  - 解析稳健性：snap `refresh --list` 跳过首行表头、取前两列（name/version）；
    flatpak `remote-ls --updates --columns=application,version,branch` 用 **tab** 分隔，且版本列为空时
    视为"已是最新"跳过（避免把无新版本的已装应用刷进可升级列表）。
  - 写操作无原生 dry-run：snap 用 `snap info`、flatpak 用 `flatpak remote-info` 作可行性兜底（仅探测
    包存在性）；`operationArgs` 中 `Autoremove`/`CleanCache` 返回空（沙箱自管理依赖与缓存）。
  - 新增沙箱后端后，CI `ubuntu:devel` 容器默认无 snap/flatpak，相关 `isAvailable()` 返回 false，
    测试应 `SKIP` 而非伪通过（参考 `LinyapsBackendTest::NotAvailableWithoutLlCli`）。
- **Qt6 D-Bus**：`QDBusConnection::connect` 没有 functor 重载——D-Bus 信号必须用旧式
  `SLOT()` 字符串连接，且槽参数要匹配（`onPrepareForSleep(bool)`、`onNmStateChanged(uint)`）。
  无参 `SLOT()` 会在运行时静默丢弃参数（例如断开时也触发一次检查）。`NM_STATE_CONNECTED_GLOBAL` = 70。
- **daemon 须订阅 PrepareForSleep / NetworkManager 信号（P2）**：daemon 是后台常驻 D-Bus 服务，
  原实现仅 `checkNow`/`status` 两槽，未订阅系统唤醒/联网信号，系统唤醒或网络恢复后不会自动重检。
  已修复（`Daemon::Daemon()` 内用旧式 `SLOT()` 连接 login1 `PrepareForSleep` 与 NM `StateChanged`，
  槽 `onPrepareForSleep(bool)` 仅在 `sleeping==false` 时、`onNmStateChanged(uint)` 仅在 `state==70` 时
  调 `m_monitor->checkNow()`，commit 0b361bf）。新增系统信号须沿用 Qt6 旧式 SLOT 带参连接，勿无参连接。
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
- **测试坑 · `SecurityAdvisorTest::UpstreamPrefetchAsyncSignals` 挂死 CI（高频复发）**：
  该测试调 `SecurityAdvisor::prefetchUpstream`，其内部 `asyncGet` 走 `QNetworkAccessManager`
  异步网络。若测试 TU 内**没有 `QCoreApplication`**（即漏调 `ensureApp()`），则 `QSignalSpy::wait()`
  无法驱动 Qt 事件循环 → 网络回调与 5s `QTimer` 超时都永不触发 → **整套件在 CI `Run tests`
  步骤永久挂起、6h 超时 cancelled**。修复铁律：**凡用到 `SecurityAdvisor` 异步信号的测试必须
  先调 `ensureApp()`**（在 `test_runprivasync.cpp` 定义，跨 TU 用 `extern void ensureApp();` 声明）。
  禁用分支（`m_fetchUpstream=false`）为**同步** emit 空缓存，断言用 `EXPECT_EQ(spy.count(), 1)`
  而非 `spy.wait()`；异步分支才用 `spy.wait(5000)`。
- **测试坑 · `MonitorFakeBackend` 需覆盖预检探针**：`UpdateMonitor::applyUpdates` 会调
  `PreUpdateCheck::run(m_backend)`，而 `PackageBackend` 基类的 `checkFailedUnits`/`checkServicesNeedingRestart`
  默认实现对**有 systemd 的宿主**会执行 `systemctl --failed` / `needs-restarting`，若宿主恰有
  failed unit 则返回非空 → `pre.hasAnything()` 为真 → 进入"需用户确认"分支、不直装，导致
  `ApplyUpdatesWithoutAdvisorInstallsDirectly` / `MultiBackendAggregatesAndRoutes` 等断言失败。
  这类失败在 CI 容器（无 failed unit）不出现、仅本地有 failed unit 的机器暴露。正确写法：
  在 `MonitorFakeBackend` 中显式覆盖四个 `check*` 探针返回 `false`/清空列表，使假后端不触发
  真实系统探测（已补，勿删）。基类四个探针为 `checkRebootRequired` / `checkServicesNeedingRestart`
  / `checkConfigFilesToReview` / `checkFailedUnits`，新增假后端或测试后端时须**全部**覆盖，
  不可只覆盖其中两个。
- **死代码**：删除功能时，要连其 config 键、getter/setter、DConfig 条目、`showConfig()`
  行一起删。不要留下“虚拟”开关却无人读取。诚实的零/空胜过看起来合理但实际的占位数字。
- **陈旧注释 / 虚代码 / 占位标记（开发完成必清理，防漂移复发）**：功能开发**完成**后，必须
  把对应的"预留 / 尚未实现 / 占位脚本 / TODO / FIXME / 临时"等标记清理掉——否则下一轮
  agent 会基于"未实现"的过期注释误判，重复开发或漏接文档，这是本轮（pacman/zypper）暴露的
  高频复发问题。具体纪律：
  - 后端 `isAvailable()` 注释里"仅提供 `xxx` 占位脚本"描述的是**真实探测陷阱**（如 Arch 上
    tinyget 仿真），属有效技术背景，可保留；但 `presetconfig.cpp` / `backendfactory.cpp` 里
    "Arch→pacman 尚未实现" 之类指向上游已落地功能的注释，必须改为"当前宿主环境命令缺失即
    如实返回 nullptr"等现状描述。
  - 凡 `// 预留，尚未实现后端`、`// 占位` 等指向上游已实现/已删除功能的注释，视为**死注释**，
    与死代码同等处理：实现到位即删除或改写为现状。
  - 收尾自查用 `grep -rnE "尚未|未实现|占位|not yet|not implemented|TODO|FIXME|HACK" src tests`
    扫一遍，确认无指向"已做却说没做"的残留；历史审计报告（`research_report_*.md`）里提到的
    "占位符/未实现"是已修复记录，不算活跃代码残留，无需改。
- **CI lint 门禁（clang-format dry-run）**：`lint.yml` 的 `format` job 有
  `find src tests \( -name '*.cpp' -o -name '*.h' \) | xargs clang-format --dry-run --Werror`
  （**不是** `build.yml`，`build.yml` 只负责 deb 打包；此门禁曾因记忆误归 build.yml 而漂移，已纠正），
  **任一文件不符合即 exit 123 红 CI**。曾因手改 include 顺序（`backendfactory.cpp` 里
  `"core/monitor/updatemonitor.h"` 排到了 `"dnfbackend.h"` 之后）触发——该文件本地编译
  无误、仅在 CI lint 阶段失败，错误信息指向文件首行 `#include`，极易误判为其他问题。
  **所有改动文件提交前必须本地跑 `clang-format --dry-run --Werror` 自检**；include 顺序
  交给 `clang-format -i` 自动重排，勿手排（见「约定」代码风格段）。注意：**clang-format
  版本差异**可能改变格式化结果（如本机 clang-format 22 与 CI 版本），本地 `-i` 后立即
  `git diff` 复核再提交最稳妥；`lint.yml` 当前用 `apt-get install clang-format`（ubuntu:devel
  浮动版本），若本地与 CI 频繁不一致可钉死具体版本（如 `clang-format-22`）以减少噪声。
- **文档漂移**：重构后要在同一改动里修正 README（英文 + 简中）——打包脚本
  （`ci/package-deb.sh` 是 **唯一** 的 CI 脚本，旧的 `ci/multiarch-build.sh` 已删除，
  凡文档/代码引用到它的都属漂移须清理）、接线
  （`BackendFactory::attachLinyaps`，不是构造内自接）、以及 `backendId()` 与
  `backendType()` 的引用都曾发生过漂移。
- **提交后必须关注 CI 结果（引入新问题要修并在文档说明）**：每次 `git push` 后，必须主动
  查看 GitHub Actions（`build.yml` 打包 + `test.yml` 单测，`lint.yml` 静态检查）的实际
  运行结果，**绝不能"提交即结束"**。若 CI 因本轮改动挂红，必须：① 定位根因（优先排查 clang-format
  版本差异、clang-tidy 新告警、测试断言在 CI 容器/多架构下行为差异）；② 本地复现并修复；
  ③ 把该 CI 坑**追加进「已踩过的坑」**（用坑登记模板，标注触发场景/根因/修复 commit/复发判定），
  并在当日工作记忆与提交说明里记录，避免再犯这类问题。clang-format 门禁对全量 `src/tests`
  文件生效——哪怕只改了一行注释，只要该文件进入 diff 就要过 `--dry-run --Werror`，
  故提交前对所有改动文件跑 `clang-format -i` 是硬前提。
- **禁止提交编译产物（仓库卫生硬纪律）**：构建目录与中间文件**严禁入库**。
  - `build/`、`build-*/`、`build-asan/` 等 CMake 构建目录是本地调试产物（如 `build-asan`
    是用 AddressSanitizer 编译的本地内存错误检测构建，**只在本地用于跑 `test-core` 复现
    use-after-free / 泄漏**，不是源码），已被 `.gitignore` 忽略，**任何改动都不得 `git add` 它们**。
  - 其他禁止入库的编译产物：`*.o` / `*.obj` / `*.a` / `*.so*` / `moc_*.cpp` / `qrc_*.cpp` /
    `ui_*.h` / `*.moc` / `*_autogen/` / `compile_commands.json` / `Testing/Temporary/*`
    （CTest 日志）/ 各 `.dir/*.o.d` 依赖文件等。
  - **`git add` 纪律**：绝不用 `git add -A`、不 `git add` 整个目录或 `.`；必须**显式指定源码路径**
    （如 `git add src/core/security/securityadvisor.cpp`），提交前用 `git status` 复核暂存区
    只有源码/doc/配置，无构建产物。曾经因 `git add -A` 把整个 `build-asan/`（498 个文件）误提交进
    历史，被迫 `git rebase` 重写 11 笔提交才清除——此坑**严禁重现**。
  - 若误把构建产物纳入了某次提交：立即 `git rm --cached -r <路径>` 从索引移除（保留本地目录），
    amend 或新提交修正；若已 push 且需彻底清除历史，再走 rebase + `push --force-with-lease`
    （共享分支强推需谨慎，确认无人基于旧历史开发）。

## 常态化严苛审查工作流（防垃圾代码污染源码）

**核心纪律：绝不随意提交未经审查的代码。** 每完成一轮功能开发 / 修复 / 重构，或用户要求"检查已实现功能"时，
必须按如下多 agent 工作流执行一轮严苛审查，发现问题先分析确认、再重构落地，禁止带病提交。
该流程已在 dde-dock/通用托盘、core/common/ui/daemon 审查中验证有效。

1. **拆分审查域**：把已实现代码按模块领域分成 2-4 个独立审查流（如 core / common / ui+daemon+indicator）。
   每个流独立、无重叠，避免重复劳动。
2. **并行派审查子 agent**：用 `Task` 工具同时启动多个 `code-explorer` 子 agent（只读、出证据），
   每个 agent 逐文件 `read_file`、对照本 AGENTS.md 的「硬性约束」「已踩过的坑」做静态分析，
   输出**带 文件:行号 证据**的问题清单（问题类型 / 现象 / 违反约束编号 / 严重程度 / 修复方向）。
   严禁子 agent 凭空猜测文件内容——必须基于实际读取证据。
3. **主 agent 亲自核验、纠正误报**：子 agent 有高频误报倾向（曾误报 IID 值、resources.qrc 是否编入、
   onStateChanged 实现文本、把"已修复"说成"未修复"）。主 agent **必须**亲自 `read_file` 复核每一条高危项，
   区分"真问题"与"子 agent 误判"，只落地已核实的问题。
4. **分类重构**：把确认的问题分三档——
   - 高危（P0/P1）：硬性约束违反（提权/异步写/默认焦点/单实例/架构虚设服务），立即重构；
   - 中危（P2）：空值覆盖丢失、漏接沙箱后端、死注释、死代码，本轮内修；
   - 低危（P3）：命名/注释澄清，可记录待办不阻塞提交。
5. **落地与验证**：主 agent 亲自改代码（子 agent 无写权限，只出方案）；改动后必须：
   `clang-format -i` 全过 → `cmake --build` 编译通过 → `ctest` 无回归 → 关注 CI 结果（见上条）。
6. **文档与记忆同步**：每轮审查发现的新坑，追加进「已踩过的坑」；本次新增的"常态化审查工作流"本身也要随项目演进维护。
   工作记忆（` .codebuddy/memory/`）追加当日闭环记录。
   **AGENTS.md 更新纪律（硬）**：本文件（AGENTS.md）的「硬性约束」「已踩过的坑」「约定」一旦因审查/修复/重构而改动，
   **必须随对应代码改动一起提交**（不单独遗漏、也不无限期挂起未提交）。理由：AGENTS.md 是 AI 与贡献者的唯一权威约束来源，
   若代码已改而文档仍写旧措辞（如门禁归属、后端清单、死注释描述），下一轮 agent/贡献者会基于过期文档误判、重复踩坑。
   提交粒度与代码改动一致——同一次功能/修复提交里顺带 `git add AGENTS.md` 即可；若纯文档纠错（如门禁归属漂移），
   可独立成一笔 `docs(agents)` 提交，但不得长期留作未提交改动。

**子 agent 只读陷阱提示**：`code-explorer` 子 agent 是只读角色，无法落地代码；所谓"重构 agent"只能退回方案文本。
最终写代码须主 agent 基于**实际文件内容**审核后亲自执行，不可盲信子 agent 对文件内容的转述。

- **teardown 段错误：QCoreApplication 静态全局析构顺序错位（CI 容器必现，本地偶发）**：
  表象：**全部 74 个用例全 OK，ctest 却报 `***Exception: SegFault`**（`gh run view` 见
  `0% tests passed, 1 tests failed out of 1`）。真实根因**不是** SecurityAdvisor 异步链，
  而是 `QCoreApplication` 曾被写成**静态全局对象**（`static QCoreApplication g_app`），
  在 `gtest_main` 的 `RUN_ALL_TESTS()` 返回后随其他全局静态一起无序析构；而
  `runPrivilegedAsync` 走 `QtConcurrent::run`，依赖 Qt 全局静态 `QThreadPool`，二者析构
  顺序错位 → teardown 期 use-after-free。注意 `SecurityAdvisor::asyncGet` 给
  `QNetworkAccessManager` 挂父对象（`this`）是**正确且必要的防护**（避免栈对象早析构时
  异步回调访问已亡对象），但它**不是**本次段错误的根因，勿混淆。
  **正确修复（已落地）**：在 `tests/test_runprivasync.cpp` 提供自定义 `main()` 取代
  `gtest_main`，且 `tests/CMakeLists.txt` 改为只链接 `GTest::gtest`（不链 `gtest_main`）。
  `main()` 在栈上构造唯一的 `QCoreApplication`（`g_appInstance` 全局指针供其他 TU 的
  `ensureApp()` 引用），`RUN_ALL_TESTS()` 返回后**先 `QThreadPool::globalInstance()->waitForDone()`
  耗尽后台任务、再 `processEvents()`/`sendPostedEvents()` 排空事件、最后才让 app 随 main 栈帧
  自然析构**（晚于所有全局静态）。`main()` 开头必须 `testing::InitGoogleTest(&argc, argv)`。
  复发判定：CI `unit-test` 仍 `SegFault` 或本地 `ctest` 偶崩 → 先查是否把 `QCoreApplication`
  当静态全局、或是否再次链回 `gtest_main` 导致双 main/析构错位。验证：本地 `ctest` 全绿后
  必须 `gh run view <id> --log` 确认 CI 两架构 `unit-test` 均 `success` 才算闭环。
- **LinyapsBackend 写操作必须走基类异步模板（P0 高频坑）**：沙箱后端（linglong/snap/flatpak）**严禁**手写
  `runPrivileged(...)` + `waitForFinished(...)` + 主线程 `emit operationFinished` 的同步写（曾出现
  `linyapsbackend.cpp` 直接调 `runPrivileged` 阻塞 600s 并主线程发信号）。正确做法：只覆盖
  `operationArgs(Op, packages, error)` 返回该后端的命令参数（install→`install`、remove/purge→`uninstall`、
  autoremove/cleanCache→`prune`），基类 `runWriteOperation` 会自动经 `runPrivilegedAsync` 后台执行并
  通过 `operationFinished` 信号回调。新增/修改任何后端写路径前，先确认是否复用基类模板，勿重复造阻塞轮子。
- **升级确认框默认焦点必须落在取消（硬约束3，P0）**：`MainWindow` 的更新确认 `DDialog` 必须让 **Cancel**
  成为默认推荐按钮（`addButton("Cancel", true, ButtonRecommend)`），Update 为普通按钮。曾出现把 Update 设为
  `true`/`ButtonRecommend`、Cancel 设为 `false` 的倒退——等于默认高亮"升级"，属"替用户做决定"，违反
  "绝不替用户做决定、默认焦点在取消"的硬约束。改完用 `grep -n "ButtonRecommend" src/ui/mainwindow.cpp` 复核。
- **iniparser::value 空值覆盖陷阱（P2）**：`IniParser::value(key, def)` 对 `Section.Key` 点号查询，命中判定
  必须用"段内是否真含该键"，**不可**用 `if (!v.isEmpty()) return v` 回退——空字符串值（`Key =`）是合法覆盖，
  会被误判为"未命中"而错误回退全局默认值。正确：段内 `contains(key)` 即返回其 value（含空）。
  注 `sectionValue`/`globalValue` 内部用 `!isEmpty()` 判"未命中→返 def"是另一语义、无需改。
- **Daemon 必须接入沙箱后端且单实例（P2）**：`Daemon` 构造在 `new UpdateMonitor` 后必须调
  `BackendFactory::attachSandboxBackends(m_monitor, m_config, this)`，否则 daemon 上报的 `updatable`
  漏掉 linglong/snap/flatpak；`main.cpp` 用 `QLockFile`（RuntimeLocation 回退 /tmp，命名
  `dtk-update-daemon.lock`）防多 daemon 抢 DBus service 名。GUI 侧 `main.cpp` 同理加 `dtk-update-gui.lock`。
  （daemon 的 D-Bus `status/checkNow/applyUpdates` 当前无客户端调用，属架构预留：保留接口、勿删，但落地
  实际调用前不要声称"daemon 已打通更新闭环"。）
- **privilegedPrefix 已纯虚化（防沉默认 sudo 陷阱）**：`PackageBackend::privilegedPrefix()` 已改为**纯虚**，
  任何新增后端若不覆盖即编译失败（而非静默回落到 `{pkexec, sudo}` 拼出 `pkexec sudo apt-get` 的多余/失败路径）。
  现有后端均覆盖：apt→`{pkexec, apt-get}`、dnf→`{pkexec, dnf}`、pacman→`{pkexec, pacman}`、
  zypper→`{pkexec, zypper}`、linyaps/snap/flatpak→空（各自经 polkit 自提权）。新增后端**必须**覆盖此钩子。
- **沙箱后端升级语义必须走 refresh/update/upgrade（P1）**：`UpdateMonitor::proceedUpdate` 对所有后端统一调
  `upgrade()`（基类 `upgrade()` 默认把 `Op::Upgrade` 回落到 `operationArgs(Op::Install)`——系统后端 install 含
  upgrade 语义，无碍）。但 snap/flatpak/linyaps 的 install 命令（`snap install`/`flatpak install`/`ll-cli install`）
  **不含已装包升级语义**，对已装应用会报 `already installed` 失败，必须各自 `operationArgs` 的
  `case Op::Upgrade:` 显式覆盖：snap→`refresh`、flatpak→`update -y`（**不带** install 的 `flathub` 固定远端）、
  linyaps→`upgrade`。凡改 `proceedUpdate` 路由或沙箱后端写路径，必须确认升级分支非 install。
- **子 agent 死代码误报纠正（复审铁律）**：常态化审查时 `code-explorer` 子 agent 曾报告
  `IniParser::sectionValue()` 为"零调用死代码"。主 agent 复核发现该 API 被 `tests/test_iniparser.cpp` 调用，
  属公开测试接口、非死代码——此类"漏算测试调用方"的高频误报必须主 agent 亲自 grep 全仓（含 `tests/`）复核后再落地，
  严禁盲删（删除会直接破坏测试编译）。同理，`value()` 的 `Section.Key` 点号查询会回落段内，与 `sectionValue`
  语义有别，二者并存合理，勿以"功能重复"为由删其一。
- **通用托盘资源静态链接陷阱（P1，仅 generic 适用）**：`src/tray-generic` 是独立可执行文件，静态链接
  `dtk-update-indicator` 静态库里的 `resources.qrc` 会被链接器惰性丢弃（`undefined reference to
  qInitResources_resources`）。正确做法：generic 自身用 `qt_add_resources` 直接编译 `resources.qrc`
  进可执行文件，并在 `main.cpp` **全局命名空间**调 `Q_INIT_RESOURCE(resources)`（`Q_INIT_RESOURCE`
  在 `namespace DtkUpdate` 内展开会错位符号名→链接失败）。图标回退路径真实前缀是 `/icons`
  （`resources.qrc` 定义）与 `/dsg/built-in-icons/`（dde-dock loader 要求），**不是** `/resources`——
  `QIcon::fromTheme` 取不到时回退 `:/icons/%1.svg`。
  **dde-tray 不同**：`src/tray`（dde-dock 插件 .so）直接链接 `dtk-update-indicator` 静态库，而
  `src/indicator/CMakeLists.txt` 已将 `${CMAKE_SOURCE_DIR}/resources/resources.qrc` 编入该库
  **未指定 NAMESPACE**，故 qrc 符号实际落在 `DtkUpdate` 命名空间；dde-tray 在 `namespace DtkUpdate`
  内调 `Q_INIT_RESOURCE(resources)` 可正确解析（CI deepin beige chroot 已成功产出 .deb 间接印证）。
  故"namespace 内 Q_INIT_RESOURCE 会错位"仅对 generic 成立，对 dde-tray 属误判——常态化审查子 agent
  曾据此误报 dde-tray P1，主 agent 须以 CI 链接结果为据纠正，勿盲改 dde-tray。
- **Flatpak simulateInstall 不可写死 flathub（P1）**：`FlatpakBackend::simulateInstall`（依赖解析可行性兜底）
  若硬编 `flatpak remote-info flathub <pkg>`，应用位于其他远端（fedora/gnome-nightly/verified 等）时
  remote-info 失败，`DependencyResolver::resolve` 直接 return false 阻断整条依赖解析。必须遍历
  `flatpak remotes --columns=name` 所有远端逐个尝试，最后才兜底 flathub。同理 snap 也应遍历已登录远端。
- **DependencyResolver::parseSimulateOutput 须真分流（P2）**：原实现 if/else-if 链中 APT 正则
  (`^Inst\s+`/`^Remv\s+`) 未受 `dnfFormat` 门控，DNF 格式下仍会先用 APT 正则匹配每行，存在跨格式
  误命中风险（声明"按后端类型分流"、实现"APT 正则全局生效 + DNF 正则附加"，属声明/实现不符）。
  已修复为对称门控（`!dnfFormat` 门控 APT 分支、`dnfFormat` 门控 DNF 分支），并补两条 DNF 单测
  （`ParseSimulateOutputDnfFormat` / `ParseSimulateOutputDnfIgnoresAptLines`）锁定行为。新增后端复用
  此函数时务必保持格式严格分流，勿让某一格式的正则越界吞掉另一格式的输出。
- **PackageBackend 探针必须用 runProbe 读退出码（P1）**：健康探针默认 `support=false`，按后端覆盖；
  `needs-restarting` 退出码 1=需重启/0=否、`systemctl --failed` 退出码 1=有 failed unit/0=否，**探针必须用
  `runProbe`（读取退出码）而非 `runQuery`（非零退出时丢弃 stdout、返回 false，导致 failed unit 永远漏报、
  support 误判 false）**。原 `checkFailedUnits` 基类默认实现误用 `runQuery`，已修复为 `runProbe` 按退出码判定
  （commit 0b361bf），与 `checkServicesNeedingRestart` 对称。新增探针须沿用 runProbe，切勿因"命令存在即成功"
  的直觉误用 runQuery。
- **SecurityAdvisor 三处 setFetchUpstream 接线必须同步（P1）**：`m_fetchUpstream` 由
  `AppConfig::fetchUpstreamAdvisories()` 驱动，UI（`ui/main.cpp`）、托盘（`indicator/updateindicator.cpp`）、
  daemon（`daemon/dtkupdated.cpp`）**三处**构造 SecurityAdvisor 后都须调 `setFetchUpstream(config.fetchUpstreamAdvisories())`。
  漏任一处即导致该运行形态下用户配置形同虚设。新增运行形态（如未来新前端）必须同步接线，否则属"声明/实现不一致"的虚设开关。
- **改安全公告 feed URL 须先跑 ctest 验证（回归铁律）**：`SecurityAdvisor::upstreamFeedUrl` / `distroNoticeUrl`
  的返回值被 `SecurityAdvisorTest::UpstreamPrefetchAsyncSignals` 间接依赖（本机 `DistroProbe::detectFamily()`
  决定走哪条 URL 分支，sync emit 与 asyncGet 路径行为不同，可能让 `spy.wait(5000)` 超时）。任何改动 feed URL
  或 `prefetchUpstream` 早期 return 分支，必须**先本地跑全量 ctest** 确认 `UpstreamPrefetchAsyncSignals` 仍绿，
  再提交；曾因把 `upstreamFeedUrl(Arch)` 改为返回空导致该用例 5s 超时回归（sync emit 路径在本机未触发），已回退。
  已知取舍：Arch 无官方安全公告 RSS，`upstreamFeedUrl(Arch)` 与 `distroNoticeUrl(Arch)` 同指 news feed，
  开启 `FetchUpstreamAdvisories` 时同 URL 双发（一次当安全公告、一次当通知）；默认关闭路径不触发，暂不去重。
- **测试恒真断言是伪通过（P2）**：`EXPECT_TRUE(x || !x)` 永远 true，掩盖逻辑未验证。健康/探针类测试应断言
  确定性结构（如未执行实际操作时 `report.hasAnything()` 应为 false），或 SKIP 环境缺失分支，勿用恒真占位。
- **CI 脚本清单（防漂移）**：`ci/` 下**仅保留 `package-deb.sh`**——由 `build.yml` 在
  deepin beige chroot 内调用，执行 `dpkg-buildpackage` 产 `.deb`。**已删除
  `ci/multiarch-build.sh`**（旧 ubuntu:devel 交叉编译方案遗留，CI 不再调用）。
  `test.yml` 改为在 ubuntu:devel 容器内**原生** `cmake -B build + cmake --build + ctest`
  跑单元测试，不依赖任何 ci/*.sh 脚本。凡新增脚本引用前先确认未被删除。
- **debhelper 13 单包打包陷阱（dh_install 目录错位）**：单包项目下 `dh_auto_install` 默认把
  文件装进 `debian/<pkg>`（包名目录），而 `dh_install` 的默认 sourcedir 是 `debian/tmp`，二者
  错位会让 `dh_install` 在 `debian/tmp` 找不到任何文件而 `missing files, aborting`。必须在
  `debian/rules` 里 `override_dh_auto_install: dh_auto_install --destdir=debian/tmp` 让二者一致。
  若误用 `dh_install --sourcedir=debian/<pkg>` 反而触发 self-copy（同目录 cp）错误。**验证打包务必
  跑完整 `dpkg-buildpackage`，不能只到 `cmake + make`**——编译错误会掩盖后面的 dh_install/dh_missing
  阶段问题。
- **可选插件的打包写法**：dde-tray 插件 `libdtk-update-tray.so` 依赖 deepin 专属 dde-dock SDK，
  非 deepin 环境不产出。它**不能**写进 `debian/install`（否则 dh_install 因找不到而 abort），也不能
  仅靠 `debian/not-installed` 兜底（`not-installed` 只管"tmp 有但 install 没列"，管不了"install 列了但
  tmp 无"）。正确做法：`debian/install` 不列它；在 `override_dh_install` 里 `dh_install` 完成后，仅当
  `debian/tmp/.../libdtk-update-tray.so` 存在时手动 `dh_install -pdtk-update` 装入。  dcc 图标
  `dcc-dtk-update.dci` 由 CMake 顶层安装（tray 跳过也照装），正常列在 `debian/install` 即可。
- **CMake install 用相对 DESTINATION（P1，打包真坑）**：`data/CMakeLists.txt` 的 `install(FILES ...
  DESTINATION /etc/xdg/autostart)` 写成**绝对路径**，会忽略 `$DESTDIR` 直接写到构建机真实 `/etc`
  （本地 `make install` 污染系统），且 `dpkg-buildpackage` 下 dh_auto_install 把它装到真 `/etc/xdg/autostart`
  而非 `debian/tmp/etc/...`，debhelper 无法纳入包、dh_install 也据此找不到文件而 abort。**所有 install
  DESTINATION 必须相对路径**（`etc/xdg/autostart`、`usr/lib/systemd/user`、`usr/share/...`），与 `debian/install`
  的路径前缀一致。另一处 `DESTINATION lib/systemd/user` 缺 `usr` 前缀，也会让 `debian/install` 的
  `usr/lib/systemd/user/dtk-update.service` 在 `debian/tmp` 找不到 → dh_install 报错。改路径后须跑完整
  `dpkg-buildpackage` 验证 dh_install/dh_missing 阶段不 abort（不能只到 cmake+make）。
- **tr() 文案必须进 ts、lupdate+_gen.py 闭环（P1，翻译漂移）**：凡源码新增/改动面向用户可见字符串（如
  `PackageBackend::stageText` 的 `tr("Upgrading")`），必须 `lupdate ../src -ts *.ts -source-language en_US`
  重新抽取，再 `python3 translations/_gen.py` 把 `zh_CN/es/fr/de` 字典条目回填（en_US 作源语言保持源串）。
  漏跑会导致非英语用户看到裸英文。**`_gen.py` 字典是单一事实源**：任何新 visible 字符串的译法必须先加进
  `_gen.py` 四语言字典，Agent 不得只改 ts 文件（会被下次 `_gen.py` 覆盖）。验证：grep 源码 `tr("Xxx")`
  在 5 个 ts 均存在对应 `<message>`。
- **clang-format 门禁在 lint.yml 而非 build.yml（P2，文档漂移已纠正）**：`find src tests ... | xargs
  clang-format --dry-run --Werror` 属 `lint.yml` 的 `format` job；`build.yml` 只负责 deepin beige chroot
  内 `dpkg-buildpackage` 产 deb。曾误记归 build.yml，已修正。lint.yml 当前 `apt-get install clang-format`
  （ubuntu:devel 浮动版本），若本地 clang-format 与 CI 频繁不一致，可钉死具体版本（如 `clang-format-22`）。

## 构建与验证

```bash
sudo apt build-dep .            # 宿主依赖；CI 内 chroot 镜像此步骤
mkdir -p build && cd build && cmake .. && make -j$(nproc)
ctest --output-on-failure        # 当前共 74 个 TEST 用例；SKIP 数量随宿主环境变化
                                  # （apt/dnf/linyaps 不可用、非容器等触发 GTEST_SKIP，
                                  # 当前源码共 6 处 GTEST_SKIP，详见「测试坑」章节）
```

本地机通常为 Arch/DTK6，apt/dnf 后端 `isAvailable()` 返回 false，相关测试 `SKIP`——
这是预期，不是回归。deepin Dock 托盘（`src/tray`）在本地无 `dde-dock` SDK 时被跳过，
这是设计内的优雅降级，非错误。`dde-dock-dev` 是**可选**依赖（仅 deepin/UOS 源提供，
beige 中由 `dde-tray-loader-dev` 以虚拟包形式提供），已**移除**出 `debian/control`
的 `Build-Depends`，因此非 deepin 环境 `apt-get build-dep` 也能成功。**`build.yml` 在
beige chroot 内由 `ci/package-deb.sh` 主动安装 `dde-tray-loader-dev`**，所以官方 `deb`
默认仍编译并进 dde-tray 插件；本地手动编译若想编出该插件需自装 `dde-dock-dev`
（`dde-tray-loader-dev`）。

### deepin 容器内从源码编译的依赖提示

在 deepin 容器（非完整桌面环境）里编译时，可能遇到两个 CMake 提示，按如下补齐依赖即可：

- **`Could NOT find XKB`**：缺 `libxkbcommon-dev`（提供 libxkbcommon，Qt6 GUI 需要）。
  通常随 `qt6-base-dev` 带入，但精简容器会缺失。安装：`apt-get install -y libxkbcommon-dev`。
  该警告本身**不阻断编译**，但装上更干净、避免后续 xcb 平台插件问题。
- **`dde-dock SDK not found, skip building dde-dock tray plugin`**：缺 deepin Dock 插件 SDK
  `dde-dock-dev`（beige 上由 `dde-tray-loader-dev` 提供，含
  `/usr/include/dde-dock/pluginsiteminterface.h`，即 `PluginsItemInterfaceV2`）。
  安装：`apt-get install -y dde-dock-dev`（或 beige 上 `dde-tray-loader-dev`）。
  不装也能编译，只是跳过 `src/tray`（dde-dock 插件），其余 target（通用托盘、GUI、daemon、
  核心 + 测试）照常产出——这是设计内的优雅降级，非错误。

完整依赖（与 `debian/control` 的 `Build-Depends` 完全一致，dde-dock SDK 已改为可选）：
`cmake pkg-config qt6-base-dev qt6-tools-dev libdtk6core-dev libdtk6gui-dev
libdtk6widget-dev libdtk6log-dev libgtest-dev libpolkit-qt6-1-dev libxkbcommon-dev`。
如需 dde-tray 插件，额外部署 `dde-dock-dev`（`dde-tray-loader-dev`）。详见 README 的
`## Build` / `## 构建` 章节。

> **测试职责分离**：`build.yml` 打包阶段通过 `ci/package-deb.sh` 内
> `DEB_BUILD_OPTIONS=nocheck` 关闭 `dh_auto_test`，不跑单元测试（避免 chroot 内慢且重复的
> 双层测试）；单元测试统一由 `test.yml` 在 `ubuntu:devel` 容器内原生 `ctest` 负责，结果
> 真实可失败。`debian/rules` 的 `override_dh_auto_test` 已去掉 `|| true`，本地若直接
> `dpkg-buildpackage` 测试会真实暴露失败。

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
6. **构建与测试零回归**：`cmake + make + ctest` 必须全绿（当前共 73 个 TEST 用例，SKIP 数随环境变化，非固定 3 个）；
   对所有改动文件跑 `clang-format -i`，并额外 `clang-format --dry-run --Werror` 自检零违规
   （**这是 CI 门禁，任一文件不符即 exit 123 红 CI**，见「已踩过的坑」CI lint 门禁条）；
   `read_lints` 无错误；`git status` 确认无遗留草稿文件。
7. **文档同步**：本文件与 README（en / zh-CN）在同一改动里更新；changelog 追加版本条目，
   措辞与代码模块名对齐。
8. **网络源时效**：`SecurityAdvisor::upstreamFeedUrl` / `distroNoticeUrl` 里的发行版官方 RSS/
   Atom 端点可能随官网改版失效。长任务或定期自查时，对每个 `DistroProbe::Family` 分支的 URL
   做一次可访问性确认（web_fetch 探测），失效则更新；无稳定源的发行系（如 Fedora 安全公告）
   保持返回空并注释，绝不指向无关地址。
9. **按功能分组提交**：不同性质的改动（清理 / 修复 / 文档）拆成独立 commit，commit message
   用 `cleanup:`/`fix:`/`refactor:`/`docs:` 前缀，便于回溯，不要混成一团。
10. **有修改必然有提交（硬纪律）**：**只要动了代码 / 文档 / 配置，收尾就必须 `git commit`**，
    绝不允许"改完不提交就结束"——上一轮 clang-format 修复、tray 头文件修复、ci 脚本清理曾
    因漏提交被用户提醒。完成所有检查项（含 `git status` 确认无遗留）后，立即按第 9 条分组提交；
    除非用户明确说"先别提交"。commit 后 `git status` 复核工作区干净再收工。
11. **修复/功能完成后必须清理仓库垃圾（硬纪律）**：收尾阶段主动清理一切构建 / 打包临时产物，
    不让它们遗留在工作区，也不要混入提交。典型包括：本地 `build/`（cmake 构建目录）、
    `compile_commands.json`、以及 `dpkg-buildpackage` 在 `debian/` 下生成的 `tmp/`、`dtk-update/`
    目录、`debhelper-build-stamp`、`*.debhelper.log`、`*.substvars`、`files`、`*.post*.debhelper` 等。
    这些已统一列入 `.gitignore`，可用 `git clean -fdx debian/`（或 `rm -rf build`）安全移除；
    清理后用 `git status` 复核，确认只剩应当跟踪的源码 / 配置改动，再提交。绝不在仓库里残留
    "未跟踪的草稿 / 半成品" 文件。

## Issue / PR 监听与闭环处理（硬纪律）

仓库托管在 GitHub（origin = `github.com/dtk-update`）。任何一轮工作启动前与工作收尾时，
**必须**主动查看仓库的 Issue 与 PR 状态，做到"发现问题 → 回复 → 修复 → 验证 → 闭环"
全链路不遗漏。具体约定：

1. **主动拉取**：用 `gh` CLI（或可用的 GitHub MCP 工具）周期性执行 `gh issue list`、`gh pr list`
   （含 `-s all`/`-s open`），确认是否有新 Issue / 待审 PR 与本轮改动相关。环境无 `gh` 或
   MCP 时，应提示用户并请求授权，不得假装已核查。
2. **有则必应**：对与本仓库相关的 Issue / PR **必须回复**（哪怕只是确认收到、说明复现步骤、
   或引用已修复的 commit 号），不得沉默忽略。回复要引用具体代码位置 / commit，禁止"已修复"
   之类空话。
3. **纳入计划**：起草实现 / 修复计划时，把命中本轮范围的 Issue / PR **显式列入任务清单**，
   安排修复或文档更新，并在收尾时逐条勾掉，保证"有问题有闭环"。
4. **闭环验证**：修复后必须 `cmake + make + ctest` 全绿（当前共 73 个 TEST 用例，SKIP 数随宿主环境变化，非固定 3 个）并
   `clang-format --dry-run --Werror` 零违规，再在 Issue / PR 中贴出验证结论与 commit 号，
   最后按用户意愿关闭或请求作者验证。未验证不得宣称已解决。
5. **不越界**：PR review 只针对本项目代码与约定；不替作者做未授权的强制推送，merge / 强制
   推送等重大操作须先征询用户。

## 多 Agent 协作模式（硬纪律：子 agent 开发 / 主 agent 验收）

本项目严禁"主 agent 一个人闷头写又自己审"的自欺闭环——长上下文下极易产生幻觉式
"看起来合理、实际未实现 / 与约定冲突"的结论。凡是多任务、跨文件、或中等规模以上的开发，
**必须用子 agent（subagent）分工，主 agent 只负责规划、派活、验收与核查**。具体两种模式：

### 模式一：多任务并行开发（主 agent 管验收，三个子 agent 管写）

当一轮工作可拆成 3 个左右相对独立、可并行的子任务（如"新增后端 A"+"改托盘显隐"+
"补文档/翻译"），主 agent 应：

1. **先拆后派**：主 agent 完成需求拆解与接口契约定义（明确每个子任务的输入/产出/约束），
   然后用 `Task` 工具**同时拉起三个子 agent** 并行开发，每个子 agent 只拿自己那块清晰指令，
   互不重叠、互不依赖对方未完成代码。
2. **子 agent 职责**：三个子 agent 各自负责落地实现、本地 `clang-format -i` 自检、跑通
   相关最小验证；返回"改了哪些文件 + 验证结论 + 遗留风险"，**不负责全局架构判断**。
3. **主 agent 验收（不可 delegated）**：主 agent **必须**逐文件 `git diff` 复核每一份产出，
   对照本文件硬性约束与"声明 vs 实现"逐项核对；发现虚设开关、占位空壳、与契约不符的，
   打回对应子 agent 重做，禁止"看起来差不多"就合并。
4. **防打断 / 防幻觉**：主 agent 不被中途"看起来完成了"的汇报带偏，坚持完整验收链路；
   任一子 agent 结论与其 diff 不符（如声称修复但代码无对应改动），视为幻觉，必须回到源码
   实锤后才认可。验收期间不因用户一句"应该行了吧"而跳过 diff 核查。

### 模式二：长任务后三 agent 交叉安全审查（提交前闸门）

连续多轮、跨多文件的功能开发或重构收尾时，主 agent 在提交前**必须**拉起**三个相互独立
的审查子 agent（互不共享上下文）** 做交叉验证：

1. **触发条件**：本轮新增 / 修改源码超过约 200 行，或跨 3 个以上文件，或涉及 `src/core`
   抽象 / 后端注册 / 提权 / 异步写等硬性约束时，强制触发。
2. **三个审查子 agent 的分工（各自独立读源码、独立给结论）**：
   - **子 agent A（规范符合性）**：逐条对照本 AGENTS.md 的硬性约束、已踩过的坑、字符串/翻译/
     文档联动纪律，核查是否违规或漂移。
   - **子 agent B（实现正确性）**：基于源码静态分析，验证"声称实现的能力是否真的有对应代码"，
     重点排查死代码、虚设开关、声明/实现不一致、异步/内存/提权陷阱。
   - **子 agent C（安全与依赖面）**：审查提权路径（pkexec 而非进程内 sudo）、D-Bus/网络异步
     是否阻塞、输入解析（LC_ALL=C、tab/退出码）、第三方依赖与权限边界，确认无引入风险。
3. **交叉对质**：主 agent 汇总三份报告，对**相互矛盾**或**指向同一风险**的结论优先处理；
   任一审查子 agent 标记"未实现 / 可疑"的项，主 agent 必须回到源码实锤并修复或澄清，
   禁止以"应该没问题"搪塞。三份审查结论与最终处置（修复 commit 号 / 已澄清项）须记录在
   当日工作记忆与提交说明。
4. **闸门**：三 agent 审查发现的高危项未闭环前，**不提交、不推送**。

> **总则（贯穿所有工作）**：只写解决问题所必需的代码，严格对齐本文件约定的架构与接口。
> **禁止为凑改动而写与需求无关的"陪衬代码"、伪实现或占位空壳**——宁可诚实留空、标注 TODO，
> 也不要用看似合理的无关代码糊弄。每一行代码都应能回答"它解决什么问题、符合哪条约定"。
> **所有 claims 都必须有源码 diff 或 commit 作为证据，主 agent 须亲自核验，不接受子 agent 的口头保证。**

### 模式三：常态化自查 / 自更新 / 自迭代（文档生命力保障）

本文件不是一次性写完就锁死的静态文档——它是随代码演进而必须同步生长的"活指南"。
模式一/二解决"开发时防幻觉"，本模式解决"文档随代码过时"这一更隐蔽的漂移：代码改了、
坑踩了、约束新增了，若 AGENTS.md 不回流，下一轮 agent 会基于过期约定犯错。因此 AGENTS.md
**自身也必须被定期交叉验证、自主更新、持续迭代**，且同样通过子 agent 分工 + 主 agent 验收
的闭环完成（不依赖单人记忆）。

#### 触发条件（何时跑自审 / 自更新）

- **T1（必触发）每次新增 / 删除包后端后**：对照"约定"章节的注册清单 5 项（后端子类 /
  `package/CMakeLists.txt` 源列表 / `BackendFactory::registry()` / `PresetConfig::knownBackendIds()`
  / 沙箱后端的 `sandboxIds()`）逐处核查 AGENTS.md 与源码是否一致；同时核查 `defaultBackendFor`
  的预留 id 是否产生了新的"代码有、文档无"漂移。
- **T2（必触发）模式二闸门通过或 PR 合并后**：将三 agent 审查结论里的"新增坑 / 漂移 / 新约束"
  按本模式 SOP 回流进 AGENTS.md，使模式二第 378 行"记录在当日工作记忆"从软约定升级为硬性回流步骤。
- **T3（定期）每个 Release / 每约 10 个开发轮次 / 用户明确要求"更新维护"时**：跑一次 AGENTS.md
  全量自审（见执行步骤），不依赖具体代码改动。
- **T4（专项）`SecurityAdvisor` 外网 RSS/Atom 端点时效**：纳入本机制，定期 `web_fetch` 探测
  各 `DistroProbe::Family` 分支的 URL 可访问性（呼应"约定"第 8 条），失效即更新。

#### 执行步骤（SOP：子 agent 写、主 agent 验收）

1. **拉起文档自审子 agent**（复用模式二子 agent A「规范符合性」视角，独立读源码）：用 grep /
   read_file 逐条 diff AGENTS.md 的声明（registry / knownBackendIds / CMakeLists 源列表 / 硬性约束
   / 已踩过的坑 / 测试预期数字）与 `src/` `tests/` `debian/` `ci/` 真实内容，输出**漂移清单**
   （每条含"文档声明 → 代码实况 → 一致/漂移 + 证据路径:行号"）。
2. **读取事实来源**：主 agent 读取 `.codebuddy/memory/` 最新日日志与 `MEMORY.md`，提取本轮
   "新增坑 / 纠正记录 / 新约束 / 提交哈希"，作为自更新的权威输入（记忆已就位，本步骤是回流通道）。
3. **回填文档**：按下方"坑登记模板"把新增坑追加进「已踩过的坑」；同步修正架构地图、注册清单、
   字符串联动涉及的 README（en / zh-CN）、`backend.conf.example`；必要时更新 `MEMORY.md` 速查表。
4. **主 agent 验收（不可 delegated）**：逐条 git diff 复核自审子 agent 的改动，确认每条漂移修复
   都有源码证据；禁止"看起来合理"的凭空补充。自审结论与最终处置（修复 commit 号 / 已澄清项）
   记入当日工作记忆与提交说明。
5. **提交**：建议 `docs(agents):` 独立 commit，关联对应的业务 commit 哈希，便于回溯。

#### 轻量自更新钩子（降低人工遗忘成本）

以下可复用命令 / 信号，建议由 codebuddy `automations` 在 git commit 后或 CI 失败时触发，
把"硬纪律"变成可失败门禁：

- **引用文件存在性校验**（防文档死引用）：对 AGENTS.md 引用的 `ci/package-deb.sh`、
  `translations/_gen.py`、`data/backend.conf.example`、`README*.md` 等跑 `test -f`，缺失即报错。
- **后端注册四处一致性 grep**：后端 id 须同时出现在 `BackendFactory::registry()`、
  `PresetConfig::knownBackendIds()`、`package/CMakeLists.txt` 源列表、AGENTS.md 注册清单，缺任一即漂移。
- **i18n 同步门禁**（可选增强）：改动 `src/**/*.{cpp,h}` 后自动跑 `lupdate` +
  `python3 translations/_gen.py` 并 `git diff --exit-code` 检查 `.ts` 是否同步（当前为手动纪律，无自动入口）。
- **clang-tidy 真正阻断**（可选增强）：将 `lint.yml` 的 `tidy` job 中 `|| true` 改为 `|| exit 1`，
  使静态检查成为真实门禁（当前不阻断 CI）。

#### 坑登记模板（标准化，供子 agent 自动追加）

新增坑一律按此结构追加到「已踩过的坑」，保证可机器解析、可回溯：

```
- **<坑标题>（YYYY-MM-DD）**：<一句话现象>。
  - 触发场景：<什么操作 / 测试触发>
  - 根因：<一句话>
  - 修复 commit：<hash>
  - 复发判定：<如何检测是否复发，如 CI 挂死 / exitCode 误判 / 测试断言失败>
  - 关联约束：<硬性约束第 N 条 / 约定第 M 项 / 模式 X>
```

#### 与模式一 / 模式二的关系

- 模式一（并行开发）：子 agent 各自交付后，主 agent 验收时**并行**触发 T1 文档自审，不额外增人。
- 模式二（提交前闸门）：三 agent 审查出的"新增坑 / 漂移"项，按本模式 SOP 回流 AGENTS.md。
- 本模式是模式一/二的**元层闭环**——保证 AGENTS.md 不会随代码演进而静默过时，是项目长期可靠性
  的兜底机制。后续凡涉及"更新维护相关内容"的请求，默认走本模式，无需用户重复说明。

