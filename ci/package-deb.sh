#!/usr/bin/env bash
#
# 在 deepin beige chroot 内执行 dpkg-buildpackage 产出 .deb
#
# 用法（在 chroot 内调用）：bash ci/package-deb.sh <arch> <version>
#   <arch>    目标架构：amd64 / arm64 / loong64（仅用于校验与产物命名提示）
#   <version> deb 版本号（tag 触发用 tag 名去 v 前缀，分支触发用默认版本）
#
# 重要：chroot 本身已是目标架构（loong64 经 qemu 用户态模拟），
# 因此这里做的是 **本地构建** 而非交叉构建，绝不能传 `-a<arch>`，
# 否则 dpkg-buildpackage 进入 cross 模式并要求 crossbuild-essential-* 而失败。

set -euo pipefail

ARCH="${1:?用法: package-deb.sh <arch> <version>}"
VER="${2:?用法: package-deb.sh <arch> <version>}"

export DEBIAN_FRONTEND=noninteractive
export LC_ALL=C.UTF-8
# dch/dpkg-buildpackage 需要维护者身份，缺失会直接报错
export DEBEMAIL="${DEBEMAIL:-admin@taotieren.com}"
export DEBFULLNAME="${DEBFULLNAME:-taotieren}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# dpkg-buildpackage 要求 debian/rules 可执行
chmod +x debian/rules

# dde-dock SDK（dde-tray-loader-dev）已从 debian/control 的必需 Build-Depends 移除，
# 使其成为可选依赖，从而非 deepin 环境也能完整编译。deepin/UOS 环境下主动安装
# dde-tray-loader-dev：它 Provides dde-dock-dev（虚拟包），提供
# /usr/include/dde-dock/pluginsiteminterface.h，使 src/tray 插件照常编进 deb。
#
# 环境区分策略（避免"静默跳过"导致 deepin 环境下 dde-tray 实际没编进却以为编了）：
#   - 深 deepin/UOS 环境：dde-tray 是明确要交付的产物，SDK 装不上必须让 CI 红掉（硬失败），
#     并在安装后校验头文件真实存在，防止"装了包却未提供头"的源侧异常被掩盖。
#   - 非 deepin 环境（Debian/Ubuntu/Fedora/Arch 等）：该包不存在，优雅 warning 跳过，
#     其余 target（通用托盘、GUI、daemon、core + tests）照常产出。
is_deepin() {
  if [ -r /etc/os-release ] && grep -qi '^ID=deepin\|^ID=uos\|deepin' /etc/os-release; then
    return 0
  fi
  [ -d /usr/include/dde-dock ] && return 0
  return 1
}

if is_deepin; then
  echo "==> deepin/UOS 环境：安装 dde-dock SDK（dde-tray-loader-dev）为必须"
  apt-get install -y dde-tray-loader-dev
  if [ ! -f /usr/include/dde-dock/pluginsiteminterface.h ]; then
    echo "::error::deepin 环境下 dde-tray-loader-dev 未提供 pluginsiteminterface.h，dde-tray 插件无法构建"
    exit 1
  fi
  echo "==> dde-dock SDK 就绪，src/tray 插件将编入 deb"
else
  echo "==> 非 deepin 环境：跳过可选的 dde-dock SDK，src/tray 插件将不被构建"
  apt-get install -y dde-tray-loader-dev 2>&1 | tail -3 || \
    echo "::warning::dde-tray-loader-dev 不可用（非 deepin 源），跳过 dde-tray 插件构建"
fi

# 打包阶段不跑单元测试：测试由 .github/workflows/test.yml 在 ubuntu:devel 容器内
# 原生跑 ctest 负责（结果真实、可失败）。此处 nocheck 避免 chroot 内重复且慢的测试，
# 同时 dde-tray 等需 dde-dock-dev 的 target 仍会正常编译并进 deb。
export DEB_BUILD_OPTIONS="${DEB_BUILD_OPTIONS:-} nocheck"

# 校验 chroot 架构与期望目标一致，不一致立即失败（避免产出错架构包）
HOST_ARCH="$(dpkg-architecture -qDEB_HOST_ARCH)"
echo "==> chroot dpkg 架构: $HOST_ARCH (期望: $ARCH)"
if [ "$HOST_ARCH" != "$ARCH" ]; then
  echo "错误: chroot 架构 $HOST_ARCH 与目标 $ARCH 不一致" >&2
  exit 1
fi

echo "==> 重写 changelog 顶部版本为 $VER"
# 直接改写首行最可靠：不依赖 dch，且避免追加多余条目导致版本号漂移
# 注意：必须保持 changelog 版本号降序（首段最新），否则 dpkg-buildpackage
# 会因版本号倒挂告警甚至失败。分支触发（IS_TAG != true）时从 changelog 首行
# 读取原版本号，避免硬编码 0.1.0 导致与后续 0.1.1/0.1.2 段倒挂。
if [ "${IS_TAG:-}" != "true" ]; then
  VER="$(head -1 debian/changelog | sed -E 's/.*\(([^)]+)\).*/\1/')"
  echo "分支触发：沿用 changelog 顶部版本 $VER"
fi
sed -i "1s/.*/dtk-update ($VER) beige; urgency=medium/" debian/changelog
head -1 debian/changelog

# 清理历史构建残留，保证 dpkg-buildpackage 从零单次编译（避免复用陈旧 CMakeCache 导致
# dh_auto_configure 误判；CI 里编译只此一次，由 dpkg-buildpackage 完成，不再前置独立 cmake+make）
rm -rf build obj-* CMakeCache.txt

echo "==> dpkg-buildpackage (本地构建 $HOST_ARCH, ver=$VER)"
# -us -uc 跳过签名；-b 仅二进制；不加 -a（见文件头说明）
# dh 会自行 dh_auto_configure + dh_auto_build（含 BUILD_TESTING=ON）；
# 测试已由上面的 DEB_BUILD_OPTIONS=nocheck 关闭，统一交给 test.yml 跑 ctest。
dpkg-buildpackage -us -uc -b

echo "==> 产物（仓库上级目录）:"
ls -l ../dtk-update_*_"$ARCH".deb ../dtk-update_*.changes

# 明确确认 deb 存在，否则以非零退出，让 CI 立刻红掉
if ! ls ../dtk-update_*_"$ARCH".deb >/dev/null 2>&1; then
  echo "错误: 未产出 dtk-update_*_$ARCH.deb" >&2
  exit 1
fi
