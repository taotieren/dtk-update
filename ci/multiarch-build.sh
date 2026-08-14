#!/usr/bin/env bash
#
# dtk-update 本地构建辅助脚本
#
# 背景：dtk-update 基于 DTK6，而 libdtk6gui-dev / libdtk6widget-dev / libdtk6log-dev
# 在 Ubuntu 官方源中仅在开发版（stonking / devel）提供，稳定版（noble/questing 等）
# 均不提供。因此 CI 与本地构建均统一使用 `ubuntu:devel` 容器。
#
# 该脚本在 ubuntu:devel 容器内可直接安装完整 DTK6 开发栈并通过 apt 构建，
# 不再依赖 deepin 私有源、debootstrap rootfs、chroot 或 qemu 模拟。
#
# 注意：dde-tray-loader 的 dde-dock SDK 是 deepin/UOS 组件，未进入 Ubuntu 源，
# 因此在 ubuntu:devel 上托盘插件会被 CMake 自动跳过（仅警告，不报错）。
# 需要包含托盘插件的完整 .deb 时，请在 deepin 系环境下构建。
#
# 用法：
#   ./ci/multiarch-build.sh            # 在当前 ubuntu:devel 容器内构建 + 测试
#   ./ci/multiarch-build.sh --deb      # 额外产出 .deb（需 dpkg-buildpackage / devscripts）
#
set -euo pipefail

BUILD_DEB=0
for arg in "$@"; do
  case "$arg" in
    --deb) BUILD_DEB=1 ;;
    *) echo "未知参数: $arg" >&2; exit 2 ;;
  esac
done

export DEBIAN_FRONTEND=noninteractive

echo "==> 安装构建依赖 (ubuntu:devel + DTK6)"
apt-get update
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

echo "==> 配置 (CMake + Ninja)"
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON

echo "==> 编译"
cmake --build build -j"$(nproc)"

echo "==> 单元测试"
cd build
ctest --output-on-failure
cd "$ROOT"

if [ "$BUILD_DEB" -eq 1 ]; then
  echo "==> 产出 .deb"
  dpkg-buildpackage -us -uc -b
fi

echo "==> 完成"
if [ -f build/plugins/libdtk-update-tray.so ]; then
  echo "托盘插件: 已构建"
else
  echo "托盘插件: 跳过 (ubuntu:devel 无 dde-dock SDK；在 deepin 系环境可构建)"
fi
