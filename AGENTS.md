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
    `AptBackend` · `DnfBackend` · `LinyapsBackend` · `SnapBackend` · `FlatpakBackend`
    · `BackendFactory`（注册表 + 自动探测）· `PackageParser`。
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
  完整注册清单（新增后端切勿遗漏任一项，否则会出现"文档/代码引用漂移"）：
  1. `src/core/package/` 新增 `XxxBackend.{h,cpp}`，仿 `linyapsbackend.cpp` 写语义虚函数与 `privilegedPrefix()`；
  2. `package/CMakeLists.txt` 的 `dtk_update_core` 源文件列表加入 `xxxbackend.h`/`xxxbackend.cpp`；
  3. `BackendFactory::registry()` 增加 `{id, [](){ return new XxxBackend(); }}` 条目；
  4. `PresetConfig::knownBackendIds()` 加入该 id（与 registry 对齐）；
  5. 若该后端属**沙箱式应用商店**（见下），还需在 `BackendFactory::sandboxIds()` 列表追加 id，
     并由 `attachSandboxBackends` 自动探测接入（无需改 UI 调用点）。
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
  真实系统探测（已补，勿删）。
- **死代码**：删除功能时，要连其 config 键、getter/setter、DConfig 条目、`showConfig()`
  行一起删。不要留下“虚拟”开关却无人读取。诚实的零/空胜过看起来合理但实际的占位数字。
- **CI lint 门禁（clang-format dry-run）**：build.yml 有
  `find src tests \( -name '*.cpp' -o -name '*.h' \) | xargs clang-format --dry-run --Werror`，
  **任一文件不符合即 exit 123 红 CI**。曾因手改 include 顺序（`backendfactory.cpp` 里
  `"core/monitor/updatemonitor.h"` 排到了 `"dnfbackend.h"` 之后）触发——该文件本地编译
  无误、仅在 CI lint 阶段失败，错误信息指向文件首行 `#include`，极易误判为其他问题。
  **所有改动文件提交前必须本地跑 `clang-format --dry-run --Werror` 自检**；include 顺序
  交给 `clang-format -i` 自动重排，勿手排（见「约定」代码风格段）。注意：**clang-format
  版本差异**可能改变格式化结果（如本机 clang-format 22 与 CI 版本），本地 `-i` 后立即
  `git diff` 复核再提交最稳妥。
- **文档漂移**：重构后要在同一改动里修正 README（英文 + 简中）——打包脚本
  （`ci/package-deb.sh` 是 **唯一** 的 CI 脚本，旧的 `ci/multiarch-build.sh` 已删除，
  凡文档/代码引用到它的都属漂移须清理）、接线
  （`BackendFactory::attachLinyaps`，不是构造内自接）、以及 `backendId()` 与
  `backendType()` 的引用都曾发生过漂移。
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
  `debian/tmp/.../libdtk-update-tray.so` 存在时手动 `dh_install -pdtk-update` 装入。dcc 图标
  `dcc-dtk-update.dci` 由 CMake 顶层安装（tray 跳过也照装），正常列在 `debian/install` 即可。

## 构建与验证

```bash
sudo apt build-dep .            # 宿主依赖；CI 内 chroot 镜像此步骤
mkdir -p build && cd build && cmake .. && make -j$(nproc)
ctest --output-on-failure        # 预期 66 passed + 3 skipped（非 debian 开发机
                                  # apt/dnf/linyaps 不可用 -> SKIP）
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
6. **构建与测试零回归**：`cmake + make + ctest` 必须全绿（预期 66 passed + 3 skipped）；
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

