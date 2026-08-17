# dtk-update 发行日志

本文件记录 dtk-update 各发布版本的面向用户变更。包级变更细节同时维护在
`debian/changelog`（Debian 打包规范格式，二者顶部版本号必须保持一致）。

版本号遵循语义化版本（SemVer）：`主版本.次版本.修订`。`0.0.1` 为首个正式发布标记。

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
