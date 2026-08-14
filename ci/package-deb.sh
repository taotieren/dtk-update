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
