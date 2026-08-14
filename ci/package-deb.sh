#!/usr/bin/env bash
#
# 在 deepin beige chroot 内执行 dpkg-buildpackage 产出 .deb
#
# 用法（在 chroot 内调用）：bash ci/package-deb.sh <arch> <version>
#   <arch>    目标架构：amd64 / arm64 / loong64
#   <version> deb 版本号（tag 触发用 tag 名去 v 前缀，分支触发用默认版本）
#
set -euo pipefail

ARCH="${1:?用法: package-deb.sh <arch> <version>}"
VER="${2:?用法: package-deb.sh <arch> <version>}"

export DEBIAN_FRONTEND=noninteractive
export LC_ALL=C.UTF-8

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# dpkg-buildpackage 要求 debian/rules 可执行
chmod +x debian/rules 2>/dev/null || true

echo "==> 更新 changelog 版本为 $VER ($ARCH)"
# dpkg-buildpackage 需要有效的 changelog；用 --newversion 注入版本与架构无关
if [ -x "$(command -v dch)" ]; then
  dch --newversion "$VER" --distribution beige --force-distribution \
    "Automated build for $ARCH" || true
else
  # 无 dch 时直接改写首行
  sed -i "1s/.*/dtk-update ($VER) beige; urgency=medium/" debian/changelog
fi

echo "==> dpkg-buildpackage (arch=$ARCH, ver=$VER)"
# -us -uc 跳过签名；-b 仅二进制；-a 指定目标架构
dpkg-buildpackage -us -uc -b -a"$ARCH"

echo "==> 产物（仓库上级目录）:"
ls -l ../dtk-update_*_"$ARCH".deb ../dtk-update_*.changes 2>/dev/null || true
