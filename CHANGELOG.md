# dtk-update 发行日志

本文件记录 dtk-update 各发布版本的面向用户变更。包级变更细节同时维护在
`debian/changelog`（Debian 打包规范格式，二者顶部版本号必须保持一致）。

版本号遵循语义化版本（SemVer）：`主版本.次版本.修订`。`0.0.1` 为首个正式发布标记。

---

## 0.0.2 (2026-08-20)

### 新增功能

- **定时检测更新**：不开启 / 按小时 / 按天 / 按月（默认不开启，仅手动或事件触发检查）。
  可在 dde-dock 托盘菜单「Periodic Check…」、通用托盘「Periodic Check」子菜单与主窗口
  「设置」中配置，配置变更即时热更新调度。
- **自动更新**：默认关闭，需用户显式开启。开启后仅「定时检测」发现的更新会自动安装；
  存在安全公告或预检建议需关注时仍先征求用户确认（默认聚焦取消），绝不静默越权。
  手动或事件（唤醒 / 联网）触发的检查永不自动更新。
- 配置键变更：`checkIntervalMinutes` 由 `checkIntervalUnit`（disabled/hour/day/month）与
  `checkIntervalValue`（间隔数值）取代。

### 行为修正与完善

- **自动孤儿清理 / 缓存清理接线**：`AutoRemoveOrphans` 与 `AutoCleanCache` 此前是"虚开关"
  （配置存在但升级后从不执行）；现升级成功收尾后按配置触发后端的 `autoremove` /
  `cleanCache`（apt/dnf 自动移除孤儿与清理缓存；pacman/zypper 明确不支持孤儿自动移除；
  沙箱后端无此概念，自动跳过）。清理为后台维护性操作，不打断升级结果。
- **修复 DConfig 层布尔开关失效（终版）**：DConfig schema 键为 camelCase（
  `showSecurityAdvisory`、`fetchUpstreamAdvisories`、`noInstallRecommends`、
  `autoRemoveOrphans`、`autoCleanCache`），而读取端此前误以全小写键查询
  `keyList()`（大小写敏感永不命中），5 个布尔开关全部静默回落到默认值；现经显式映射
  表把 backend.conf 的 PascalCase 键换算为 schema 的 camelCase 键后查询，并以回归
  测试锁定 schema 一致性（backend.conf 大写键不受影响）。
- **手动/UI 触发的更新一律先征求确认**：此前用户关闭「显示安全公告」后，手动更新会
  绕过确认直接提权安装（确认闸门只保护自动更新路径）；现手动路径无条件弹出确认框
  （默认焦点在取消），自动更新路径保持「有安全公告/预检建议才确认」。
- **升级后清理回调防误判**：升级取消后，在途旧操作/清理的迟到回调曾可能被误判为本轮
  升级完成，导致提前解锁并发锁、重复提示"更新完成"；现以取消前在途清理计数转存 +
  无在途升级的游离回调守卫双重忽略。
- **孤儿清理仅在系统后端实际升级后执行**：此前升级的仅是沙箱应用时也可能对系统执行
  `autoremove`，有误删用户手动安装包的风险；现仅当系统后端本轮确实参与了升级才触发。
- 修复通用托盘析构时的二次释放（`QActionGroup` 为 `QMenu` 子对象，`delete m_menu`
  已级联释放，再 delete 即 double free）。
- 修复 daemon 对 login1/NetworkManager 信号的重复订阅（monitor 构造已订阅，daemon
  再订一遍导致同一事件触发两次检查）。
- 移除开机自启中指向 GUI 主窗口的错误桌面文件（`dtk-update-tray.desktop` 的
  `Exec=dtk-update-gui` 会让每次开机弹出主窗口并与通用托盘重复自启）。
- **修复 openSUSE 候选版本误映射**：zypper `zypper list-updates` 表头解析此前把
  `Current Version` 当候选版本列，导致 `candidateVersion` 返回已装版本；现优先匹配
  `Available Version`。
- 应用版本号修正为 `0.0.1`（此前 GUI 硬编码 `0.1.0`，与包版本不一致）。
- 修复通用托盘菜单对象在重建时的内存泄漏（QMenu 无父对象托管）。
- 修复翻译生成脚本误改写 `sourcelanguage`（把源语言也替换为目标语言）；无目标包/
  空参数的后端写操作补齐操作结果回传（契约必达）。
- **补齐 Arch 系升级后配置审阅能力**：`PacmanBackend` 此前未实现 `checkConfigFilesToReview`
  （`supportsResidualConfig()` 恒为 false），Arch/Manjaro 用户升级后有 `.pacnew` / `.pacsave`
  / `.pacorig` 残留配置文件时前检/后检从不提示；现用 `QDirIterator` 限定扫描 `/etc` 递归检测
  （避全盘 `find` 遇无权限目录返回非 0 被误判失败、且防主线程卡死），并以回归用例锁定
  （CI 无 pacman 则 SKIP，不伪通过）。
- **dde-tray 改为发行版感知构建**（`ci/package-deb.sh`）：deepin/UOS 环境下
  `dde-tray-loader-dev` 必须装得上且校验 `pluginsiteminterface.h` 真实存在，否则 CI 红
  （避免"deepin 环境以为编了 dde-tray 实际没编"的静默漏编）；非 deepin 环境该包不存在，
  仅 warning 跳过、其余 target 照常产出。配合 `src/tray/CMakeLists.txt` 的
  `BUILD_DDE_TRAY` + SDK 探测，达成"deepin 开启 dde-dock 支持、非 deepin 不开启"。

### 文档与开发规范

- 更新 `AGENTS.md` 可用 Skills 清单为新会话实际技能（新增 `find-skills` / `skill-creator` /
  `pdf`，并标注技能回流机制）；新增「代码质量要求（提交前强制门禁）」章节，量化防低质量与
  严重漏洞代码的 10 条红线（提权/异步写/功能真实实现/零回归验证/回归锁定/内存并发/解析稳健/
  提交卫生/分组提交/多 agent 交叉验证），与常态化审查工作流互补。

---

## 0.0.1 (2026-08-17)

首个稳定发布版本。

### 核心能力

- **跨发行版更新管理**：通过可插拔后端抽象接管系统包管理器，不再绑定单一发行版或包管理器。
  已支持 apt/dpkg（Debian/Ubuntu/UOS/Deepin）、dnf/rpm（Fedora/openEuler）、
  pacman（Arch/Manjaro）、zypper（openSUSE/SLES），以及沙箱式应用生态
  linyaps/玲珑、snap、flatpak（跨发行系、按需独立探测，0/1/N 个均合法）。
- **双托盘前端**：原生 dde-dock 插件（deepin/UOS，遵循 `PluginsItemInterfaceV2`）与
  独立 freedesktop 通用托盘（`QSystemTrayIcon`，无 dde-dock 私有依赖，可在任意 DTK6+Qt6
  发行系运行）；二者共用同一份 `UpdateIndicator` 核心。另提供独立 GUI
  （`dtk-update-gui`）与可选 D-Bus/systemd 用户守护进程（`dtk-update-daemon`）。
- **更新 / 安装 / 卸载 / 清除 / 自动移除 / 清理**：所有写操作经 `pkexec`/polkit 提权，
  绝不在进程内 `sudo`；进程级并发锁防止 GUI 与托盘同时触发系统写入。
- **预检 / 后检健康检查**：内核待重启、服务待重启、配置待审阅、失败的 systemd 单元——
  均为只读探测，容器感知（容器内跳过宿主状态检查），绝不自动重启或合并配置。
- **上游安全公告**：按发行版自动选择官方源（Debian DSA / Ubuntu USN / openSUSE /
  Arch），异步预取并缓存，更新流程内同步合并、绝不阻塞；超时优雅降级。
- **发行版最近新闻 / 通知**：独立于包名，按发行版从官网/公告服务拉取，在托盘/GUI
  弹出信息性通知。
- **依赖解析**：基于后端 dry-run 输出解析（APT/DNF 格式分流），无结构化事务输出的后端
  降级为仅目标包而非失败。
- **残留与缓存报告**：更新后报告残留包与可清理下载缓存，交由用户显式清理（绝不自动删除）。
- **透明配置**：DConfig + 用户可编辑 `backend.conf`（INI 风格），优先级为用户段 > 全局段
  > DConfig > 发行版预设。
- **本地化**：简体中文、英语、西班牙语、法语、德语五种语言。

### 架构与约束

- `src/core` 保持与 UI 无关、可独立单元测试；包管理命令/解析/探测全部下沉到具体后端。
- 更新确认框默认聚焦「取消」；安全公告与预检结果展示后由用户显式确认才继续（绝不替用户决定）。
- 后端接线集中化：`BackendFactory::attachSandboxBackends` 统一接入沙箱后端，前端只需一行调用。

### 打包与发布

- 提供 amd64 / arm64 / loong64 三架构 `.deb`，由 GitHub Actions 经 debootstrap deepin
  beige rootfs + chroot（loong64 经 QEMU 用户态模拟）自动构建。
- 推送 `v0.0.1` 标签即触发多架构构建并自动发布 GitHub Release，合并三架构安装包。
- 本版本为首批标记版本（`CMakeLists.txt` 与 `debian/changelog` 版本号统一为 `0.0.1`），
  此前 0.1.x 内部开发快照段已重置，不计入正式发布线。

### 已知限制

- dde-dock 插件仅在提供 `dde-tray-loader-dev`（Provides `dde-dock-dev`）的 deepin 系
  打包环境中编入 `.deb`；其他发行系自动跳过该插件，仅发布通用托盘 + GUI + 守护进程。
- 沙箱后端（snap/flatpak/linyaps）依赖各自运行环境健康，未安装或运行时损坏时静默不接入，
  不影响系统后端聚合。
- 上游公告/通知为可选能力，默认关闭（`AppConfig::fetchUpstreamAdvisories`），需用户显式开启。

### 安装

```bash
# 从 Release 下载对应架构 deb 后
sudo apt install ./dtk-update_0.0.1_<架构>.deb
```

或从源码构建（见 `README.md` 构建章节）。
