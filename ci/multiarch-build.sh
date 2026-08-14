#!/usr/bin/env bash
#
# 在 deepin beige (Debian 12 基础) rootfs 中构建 dtk-update 的 .deb 包，
# 支持 amd64 / arm64 / loong64 三种架构。
#
# 用法:  ci/multiarch-build.sh <arch> <source-dir>
#   arch       : amd64 | arm64 | loong64
#   source-dir : 已 checkout 的源码绝对路径
#
# 设计说明:
# - deepin beige 官方源提供 DTK6 dev 包 (libdtk6core/gui/widget-dev) 的
#   amd64/arm64/loong64 三架构构建，故统一使用 beige rootfs + chroot，
#   保证三架构产物环境一致。
# - loong64 无 GitHub 原生 runner、Ubuntu 也无 loong64 port，故在 x86_64
#   runner 上用 qemu-user-static 模拟 loongarch64 执行 chroot。
# - amd64/arm64 在同架构 runner 上直接用 debootstrap 构造 rootfs (无需 QEMU)。
#
set -euo pipefail

MODE="${1:-build}"          # build | test
ARCH="${2:-amd64}"
SRC_DIR="${3:-$PWD}"
MIRROR="https://community-packages.deepin.com/beige"
ROOTFS="/tmp/dtk-update-rootfs"

if [[ ! -d "$SRC_DIR/debian" ]]; then
  echo "ERROR: source dir '$SRC_DIR' does not contain a debian/ directory" >&2
  exit 1
fi

echo "==> Building dtk-update for arch=$ARCH from $SRC_DIR"

# --- 1. 安装引导工具 -------------------------------------------------------
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  debootstrap qemu-user-static git ca-certificates

# deepin beige 没有独立 debootstrap 脚本，用 sid 脚本代替
if [[ ! -f /usr/share/debootstrap/scripts/beige ]]; then
  sudo cp /usr/share/debootstrap/scripts/sid /usr/share/debootstrap/scripts/beige
fi

# --- 2. 引导第一阶段 rootfs ------------------------------------------------
sudo rm -rf "$ROOTFS"
sudo debootstrap --arch="$ARCH" --foreign --no-check-gpg --variant=minbase \
  beige "$ROOTFS" "$MIRROR"

# loong64 需要静态模拟器才能执行 chroot 内的第二阶段
if [[ "$ARCH" == "loong64" ]]; then
  sudo cp /usr/bin/qemu-loongarch64-static "$ROOTFS/usr/bin/"
fi

# --- 3. 引导第二阶段 ------------------------------------------------------
sudo chroot "$ROOTFS" /debootstrap/debootstrap --second-stage

# --- 4. 配置 apt 源并安装构建依赖 -----------------------------------------
echo "deb $MIRROR beige main community" \
  | sudo tee "$ROOTFS/etc/apt/sources.list" >/dev/null
sudo chroot "$ROOTFS" apt-get update
sudo chroot "$ROOTFS" apt-get install -y --no-install-recommends \
  build-essential dpkg-dev fakeroot cmake pkg-config \
  qt6-base-dev qt6-tools-dev qt6-tools-dev-tools \
  libdtk6core-dev libdtk6gui-dev libdtk6widget-dev \
  libpolkit-qt6-1-dev libgtest-dev debhelper git

# --- 5. 拷贝源码进 rootfs -------------------------------------------------
sudo rm -rf "$ROOTFS/src"
sudo mkdir -p "$ROOTFS/src"
# 用 tar 避免 sudo cp 的权限/符号链接问题
sudo tar -C "$SRC_DIR" -cf - --exclude='.git' --exclude='build' --exclude='artifacts' . \
  | sudo tar -C "$ROOTFS/src" -xf -

# --- 6. 构建 / 测试 -------------------------------------------------------
if [[ "$MODE" == "test" ]]; then
  # 仅编译并运行单元测试，不产出 deb
  sudo chroot "$ROOTFS" bash -c \
    "cd /src && cmake -B build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release \
     && cmake --build build -j\$(nproc) \
     && ctest --test-dir build --output-on-failure"
else
  # 在 chroot 内打包 .deb
  sudo chroot "$ROOTFS" bash -c "cd /src && dpkg-buildpackage -us -uc -b"

  # --- 7. 取出产物 --------------------------------------------------------
  mkdir -p artifacts
  sudo cp "$ROOTFS"/dtk-update*.deb artifacts/ 2>/dev/null || true
  sudo chmod -R a+rw artifacts || true

  echo "==> Built artifacts:"
  ls -1 artifacts/ 2>/dev/null || echo "(none)"
fi
