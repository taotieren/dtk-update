#!/usr/bin/env bash
#
# dtk-update 本地 / CI 多架构构建辅助脚本
#
# 背景：dtk-update 基于 DTK6，而 libdtk6gui-dev / libdtk6widget-dev / libdtk6log-dev
# 在 Ubuntu 官方源中仅在开发版（stonking / devel）提供，稳定版（noble/questing 等）
# 均不提供。因此 CI 与本地构建统一使用 `ubuntu:devel` 容器。
#
# 多架构支持说明：
#   - amd64 / arm64：Ubuntu 官方提供对应的 ubuntu:devel 多架构镜像与 DTK6 开发栈，
#     可在对应原生 runner（ubuntu-24.04 / ubuntu-24.04-arm64）上的同名容器内直接构建。
#   - loong64：Ubuntu 官方暂未提供 loong64 的 DTK6 开发包，且缺少原生 runner，
#     纯 Ubuntu 环境下无法真正构建。脚本保留 loong64 入口参数以便将来在 deepin/CLFS
#     系根文件系统中复用，CI 中该架构标记为 best-effort（失败不阻塞其余架构）。
#
# 子命令：
#   ./ci/multiarch-build.sh test [arch]   # 在 arch 架构下 配置 + 编译 + ctest
#   ./ci/multiarch-build.sh deb  [arch]   # 在 test 基础上额外产出 .deb 包
#
# arch 取值：amd64（默认）| arm64 | loong64
#
set -euo pipefail

ARCH="${2:-amd64}"

# 将 Debian 架构名映射为 GNU 三元组前缀（用于交叉工具链）
case "$ARCH" in
  amd64) TRIPLE="" ;;
  arm64) TRIPLE="aarch64-linux-gnu" ;;
  loong64) TRIPLE="loongarch64-linux-gnu" ;;
  *) echo "不支持的架构: $ARCH" >&2; exit 2 ;;
esac

BUILD_DEB=0
case "${1:-test}" in
  test) BUILD_DEB=0 ;;
  deb) BUILD_DEB=1 ;;
  *) echo "未知子命令: ${1:-}（应为 test 或 deb）" >&2; exit 2 ;;
esac

export DEBIAN_FRONTEND=noninteractive

echo "==> 架构: $ARCH (triplet=${TRIPLE:-native})"

# 非本机架构时启用多架构与交叉工具链（loong64 在 Ubuntu 上缺少 DTK6 包，此处尽力配置）
if [ -n "$TRIPLE" ]; then
  echo "==> 启用多架构 $ARCH"
  dpkg --add-architecture "$ARCH" || true
  apt-get update
  apt-get install -y --no-install-recommends \
    "gcc-$TRIPLE" "g++-$TRIPLE" "pkg-config-$TRIPLE" binutils-$TRIPLE qemu-user-static || true
else
  apt-get update
fi

echo "==> 安装构建依赖 (ubuntu:devel + DTK6)"
apt-get install -y --no-install-recommends \
  cmake ninja-build pkg-config g++ extra-cmake-modules \
  qt6-base-dev qt6-tools-dev \
  libdtk6core-dev libdtk6gui-dev libdtk6widget-dev libdtk6log-dev \
  libpolkit-qt6-1-dev libgtest-dev

if [ "$BUILD_DEB" -eq 1 ]; then
  apt-get install -y --no-install-recommends debhelper-compat dpkg-dev devscripts
fi

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

echo "==> 配置 (CMake + Ninja, arch=$ARCH)"
CMAKE_EXTRA=()
if [ -n "$TRIPLE" ]; then
  CMAKE_EXTRA+=(
    "-DCMAKE_C_COMPILER=$TRIPLE-gcc"
    "-DCMAKE_CXX_COMPILER=$TRIPLE-g++"
    "-DCMAKE_SYSTEM_NAME=Linux"
    "-DCMAKE_SYSTEM_PROCESSOR=$ARCH"
  )
fi

cmake -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  "${CMAKE_EXTRA[@]}"

echo "==> 编译"
cmake --build build -j"$(nproc)"

echo "==> 单元测试"
cd build
ctest --output-on-failure
cd "$ROOT"

if [ "$BUILD_DEB" -eq 1 ]; then
  echo "==> 产出 .deb (arch=$ARCH)"
  # dpkg-buildpackage 会按当前架构产出对应的 .deb，位于仓库上级目录
  dpkg-buildpackage -us -uc -b
  echo "==> 产物:"
  ls -l ../dtk-update_*.deb ../dtk-update_*.changes ../dtk-update_*.buildinfo 2>/dev/null || true
fi

echo "==> 完成 (arch=$ARCH)"
if [ -f build/plugins/libdtk-update-tray.so ]; then
  echo "托盘插件: 已构建"
else
  echo "托盘插件: 跳过 (ubuntu:devel 无 dde-dock SDK；在 deepin 系环境可构建)"
fi
