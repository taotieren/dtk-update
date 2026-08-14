# dtk-update

跨发行版的 DTK 托盘小程序，用于监控系统软件包更新并管理更新操作，具备正确的依赖解析、
安装 / 卸载 / 清除 / 自动移除 / 清理等能力。包管理操作在 **可插拔后端抽象** 之下委托给系统
包管理器（apt/dpkg、dnf/rpm、linyaps/玲珑 等），因此项目不再局限于单一发行版或包管理器。

## 功能特性

- 面向 `dde-tray-loader` 的系统托盘插件（V2 接口）
- 更新 / 安装 / 卸载 / 清除 / 自动移除 / 清理操作
- 与发行版无关的 **多后端** 设计（APT、DNF、Linyaps，易于扩展）；**跨发行系的 Linyaps（玲珑）**
  独立于系统包管理器单独探测，不受发行版限制
- 基于后端 dry-run 解析的依赖解析
- 残留配置与缓存清理（`rc` 包、孤儿配置）
- 可选的安全公告（deepin 安全中心 D-Bus，离线启发式兜底）
- 应用更新前 **拉取上游官方安全公告**（按发行版自动选择源：Debian DSA / Ubuntu USN /
  openSUSE / Arch 等，可配置，超时优雅降级，异步预取绝不阻塞更新流程）
- **发行版官方「最近新闻 / 通知」抓取**（独立于包名，按发行版从官网 / 公告服务拉取，
  在托盘 / GUI 弹出信息性通知）
- **更新前 / 更新后健康检查**（内核待重启 / 服务待重启 / 配置待审阅 / **失败的 systemd 单元**）——仅检查，绝不自动执行
- **容器感知**：在容器内会跳过内核重启 / 服务 / 失败单元的检查，避免误报宿主状态
- 更新后报告**残留包与可清理下载缓存**，交由用户显式清理（绝不自动删除）
- 进程级并发锁，防止 GUI 与托盘同时触发系统写入
- 通过 systemd 用户服务后台监控
- 经由 DConfig 与用户可编辑的 `backend.conf`（INI/conf 风格，见 `--show-config`）实现透明配置
- **本地化**：简体中文、英语、西班牙语、法语、德语

## 开发技能

本项目基于以下 CodeBuddy 技能（skill）规范开发，以确保符合 deepin/UOS v25 生态：

- **dde-tray-development**：托盘插件遵循 `PluginsItemInterfaceV2`
  （V2 IID `com.deepin.dock.PluginsItemInterface_V2`），`flags` 使用
  `Type_Tray | Attribute_CanSetting`，`icon()` 返回主题图标，翻译在 `init()` 内自行加载。
- **dtk-development**：应用/插件使用 DTK6（兼容 DTK5 自动探测），遵循 `DApplication`、
  `DConfig`、DCI 图标、`DLogManager` 等规范；debian 打包依赖按 DTK 模块映射。
- **dtk-development**（widget）：主窗口基于 `DMainWindow`，进度/对话框使用 DTK 控件。

> 参考布局借鉴 [arch-update](https://github.com/Antiz96/arch-update) 的模块划分思路
> （预检/后检分离、托盘集成、配置透明化），但包管理实现针对多发行版通过可插拔
> 后端抽象重写，未直接照搬其 Rust 实现。

## 架构

```
src/core      业务逻辑（与 UI 无关，完整单元测试）
  package/      PackageBackend(抽象接口) · AptBackend(apt/dpkg) · DnfBackend(dnf/rpm)
                · LinyapsBackend(ll-cli/玲珑, 跨发行系) · BackendFactory(按发行系自动探测
                  + 始终独立探测 Linyaps) · PackageParser(纯解析)
  dependency/   DependencyResolver (后端 dry-run 解析)
  security/     SecurityAdvisor (deepin 安全中心 D-Bus + 发行版上游公告 + 最近通知抓取，可选)
  healthcheck/  PreUpdateCheck / PostUpdateCheck (预检/后检，只读探测)
  monitor/      UpdateMonitor (状态机 + 定时调度，聚合 Linyaps)
src/indicator  UpdateIndicator (与桌面环境解耦的共享核心，供两个托盘复用：构建后端 /
               monitor / advisor / linyaps，向具体前端暴露钩子)
               UpdateDialogs (共享 DDialog 构建器：玲珑不可用提示、安全公告确认、
               更新后报告——两个托盘共用)
src/tray       dde-tray-loader 插件 (PluginsItemInterfaceV2，仅 deepin/UOS，依赖 dde-dock SDK)
src/tray-generic  跨发行系 freedesktop 托盘 (QSystemTrayIcon，任意 DTK6 发行系，无 dde-dock 依赖)
src/ui         独立 DTK 主窗口 (DMainWindow)
src/daemon     后台 DBus 服务 (com.dtk.update.Daemon)
src/common     日志、配置 (DConfig + INI backend.conf)、翻译器
translations   .ts 源文件 (zh_CN / en_US / es / fr / de) + CMake 编译规则
tests          core 层 GoogleTest 测试
```

设计约束：

- `src/core` 禁止直接 include Dock/Tray/UI 头文件（与 UI 无关，可独立单测）。
- 包管理写操作一律经 `pkexec`（polkit）提权，不在进程内 `sudo`。
- 配置项经 `AppConfig`（DConfig）暴露，保持透明可配。
- `PackageBackend` 抽象接口描述"语义操作"，发行版相关命令、解析、探测全部下沉到
  具体后端实现；上层（UI / tray / monitor / dependency）只依赖接口。
- `PackageParser` 为纯函数解析层，与进程执行解耦，便于单元测试。
- **预检/后检分离**：`PreUpdateCheck` 在用户确认前运行，`PostUpdateCheck` 在更新
  成功后运行，二者均为只读探测（内核待重启/服务待重启/配置待审阅/**失败的 systemd 单元**），
  **绝不自动**重启或合并配置；是否处理交由用户决定。探测具备**容器感知**：容器内
  会跳过宿主内核/服务的检查，避免误报。
- 任何功能不替用户做选择：更新确认框默认聚焦「取消」，安全公告与预检结果展示后
  由用户显式确认才继续。
- **后端接线集中化**：跨发行系的玲珑（Linyaps）沙箱后端经由唯一的工厂辅助方法
  `BackendFactory::attachLinyaps` 接入 `UpdateMonitor`，前端（GUI / 两个托盘）只需一行
  调用，不再重复探测与接线样板。该接线保持显式（由各前端调用），而非隐藏在
  `UpdateMonitor` 构造内部，从而保证监视器的单元测试具备确定性。
- **并发安全**：运行时目录单一的 `QLockFile` 防止 GUI 与托盘同时对系统发起写操作。

两个托盘前端共用同一份 `UpdateIndicator` 核心：

- **dde-tray**（`src/tray`）：deepin/UOS 任务栏插件，经 `PluginsItemInterfaceV2` 接入；
  仅当 `dde-dock` SDK 存在时才构建，否则该 target 被跳过。
- **通用托盘**（`src/tray-generic`）：独立进程 `dtk-update-tray-generic`，使用 Qt6 原生
  `QSystemTrayIcon`，**无任何 deepin 私有依赖**，可在任意装有 DTK6 + Qt6 的发行系运行
  （Ubuntu / Arch / Fedora 等）。通过 `dtk-update-tray-generic.desktop` 自启，并设
  `NotShowIn=deepin`，避免 deepin 上出现重复托盘。

### 扩展新的包管理器后端

新增一个发行版的适配只需三步，无需改动 UI / monitor：

1. 继承 `PackageBackend`，实现后端相关的虚函数（`fetchUpgradable`、`simulateInstall`、
   `listResidualPackages`、`cacheDirectories`、`install`/`remove`/`purge`/`autoremove`/
   `cleanCache`、`isAvailable`、`backendId`/`backendName`/`backendType`）。
   发行版命令、输出解析、可用性探测均在本类内部完成。
   **公共基础设施已由基类提供**——请勿重新实现 `runQuery` / `runProbe` / `runPrivileged` /
   `commandExists` / `collectConfigFiles`，它们已是共享实现，差异仅在提权前缀；覆写唯一的
   虚函数 `privilegedPrefix()`（如 `{"pkexec","apt-get"}`）即可让 `runPrivileged` 知道如何为
   你的后端提权。**还需覆写健康检查探针**
   （`checkRebootRequired`、`checkServicesNeedingRestart`、`checkConfigFilesToReview`、
   `checkFailedUnits`）：若某探针不适用于该包管理器（如玲珑这类沙箱应用后端没有
   内核/服务/单元概念），返回 `support=false` 即可，但请勿直接留下默认空实现而不说明。
   记得容器感知：当 `SystemInfo::isContainer()` 为 true 时，跳过内核重启 / 服务 / 失败单元的检查。
2. 在 `BackendFactory::registry()` 中追加一条 `{id, ctor}` 记录，决定探测优先级。
3. 把新实现文件加入 `src/core/package/CMakeLists.txt`，并在 `PresetConfig::knownBackendIds()`
   中登记该 `id` 以便配置校验。
   若依赖解析输出格式与 APT 不同，可在 `DependencyResolver` 中按 `backendId()` 分流
  （基类已内置 APT 与 DNF 两种格式解析；玲珑等无结构化事务输出的后端由 `resolve()`
  降级为仅目标包）。

示例参考 `src/core/package/dnfbackend.cpp`（Fedora/RHEL 系）与
`src/core/package/linyapsbackend.cpp`（玲珑沙箱应用系）。

### 跨发行版后端（如 Linyaps / 玲珑）

部分后端**不绑定单一发行版**，必须独立于发行系探测。玲珑（ll-cli）是跨发行系的
沙箱应用管理器：只要安装了 `linglong` 运行时，在 deepin、Fedora、Ubuntu、Arch 等
任意发行版上均可使用。因此：

- `LinyapsBackend` **绝不能被 `DistroProbe::Family` 限制**：其 `isAvailable()`
  只判断 `ll-cli` 是否存在、运行时是否健康。
- `BackendFactory` 对玲珑采取**单独的、无条件的探测**（`createBackends()` /
  `availableBackendIds()`），与发行系对应的系统后端并列。在 Debian/Fedora 主机上
  可同时返回 `apt`/`dnf` 与 `linyaps`——二者正交（系统包 vs 沙箱应用）。
- 当 `isAvailable()` 因"非未安装"的原因返回 false（如 `ll-cli` 存在但运行时损坏 /
  权限不足），后端必须在 `availabilityError()` 中返回具体、可执行的诊断信息。
  UI 与托盘通过 `UpdateMonitor::backendUnavailable` 信号把这个原因呈现给用户，
  让用户知道**如何修复**，而不是笼统的"后端不可用"。
- 新增跨发行系后端同样遵循此原则：在 `BackendFactory::registry()` 中登记，
  并确保由 `createBackends()` 探测，而非隐藏在某个发行系的 `orderedEntries()` 之后。

## 构建

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)
ctest --output-on-failure   # 单元测试
sudo make install
```

CI 有两条流水线。**单元测试**流水线跑在 `ubuntu:devel` 镜像上（它是在 Ubuntu 官方源中
唯一提供完整 DTK6 开发栈——`libdtk6gui-dev`/`libdtk6widget-dev`/`libdtk6log-dev`——的套件），
构建核心/UI/守护进程并运行 `ctest`；托盘插件在该通用镜像上会被跳过，因为它依赖的
`dde-dock` SDK 是 deepin/UOS 组件、未进入 Ubuntu 源。**构建**流水线在 deepin 系打包环境下
产出含托盘插件的完整 `.deb`（见 `ci/package-deb.sh`——**唯一的 CI 脚本**——由
`.github/workflows/build.yml` 经 debootstrap deepin beige chroot + qemu 模拟 loong64 驱动）。
旧的 `ci/multiarch-build.sh` 已删除。

## 翻译

界面文案支持五种常用语言：简体中文（zh_CN）、英语（en_US）、西班牙语（es）、
法语（fr）、德语（de）。翻译源文件位于 `translations/`，构建时通过
`lupdate`/`lrelease`（或 Qt LinguistTools）编译为 `.qm` 并安装到
`share/dtk-update/translations`。

新增/修改界面字符串后刷新翻译模板：

```bash
lupdate src -ts translations/dtk-update_en_US.ts \
    translations/dtk-update_zh_CN.ts translations/dtk-update_es.ts \
    translations/dtk-update_fr.ts translations/dtk-update_de.ts -source-language en_US
```

翻译加载由 `DtkUpdate::loadTranslator("dtk-update")` 完成（GUI 与托盘插件均已调用），
按系统区域设置自动选择 `.qm`。

## 许可证

GPL-3.0-or-later
