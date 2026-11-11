#!/usr/bin/env bash
# 一键打包：同时生成 .deb 与 AppImage（无需安装、双击即跑、自带 Qt6 运行时）。
#
# 用法：
#   ./build_package.sh                # 构建 deb + appimage
#   ./build_package.sh --no-deb      # 只打 appimage
#   ./build_package.sh --no-appimage # 只打 deb
#   ./build_package.sh --checksum    # 额外把两份包的 SHA256 写进 artifacts/SHA256SUMS.txt
#   ./build_package.sh -h            # 查看帮助
#
# 产物：
#   build-deb/noda_*.deb
#   noda-<arch>.AppImage
#   artifacts/SHA256SUMS.txt         （仅 --checksum 时）
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "${SCRIPT_DIR}"

DO_DEB=1
DO_APPIMAGE=1
DO_CHECKSUM=0
for arg in "$@"; do
  case "$arg" in
    --no-deb) DO_DEB=0 ;;
    --no-appimage) DO_APPIMAGE=0 ;;
    --checksum) DO_CHECKSUM=1 ;;
    -h|--help) sed -n '2,15p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "未知参数: $arg" >&2; exit 1 ;;
  esac
done

ARTIFACTS="${SCRIPT_DIR}/artifacts"
if [[ $DO_CHECKSUM -eq 1 ]]; then
  mkdir -p "${ARTIFACTS}"
  : > "${ARTIFACTS}/SHA256SUMS.txt"
fi

DEB="" APPIMAGE=""
if [[ $DO_DEB -eq 1 ]]; then
  echo "=== 构建 DEB ==="
  ./build_deb.sh
  DEB="$(ls -1t build-deb/*.deb 2>/dev/null | head -1)"
  echo ">>> DEB: ${DEB}"
  [[ $DO_CHECKSUM -eq 1 && -n "$DEB" ]] && sha256sum "$DEB" >> "${ARTIFACTS}/SHA256SUMS.txt"
fi

if [[ $DO_APPIMAGE -eq 1 ]]; then
  echo "=== 构建 AppImage ==="
  ./build_appimage.sh
  APPIMAGE="$(ls -1t noda-*.AppImage 2>/dev/null | head -1)"
  echo ">>> AppImage: ${APPIMAGE}"
  [[ $DO_CHECKSUM -eq 1 && -n "$APPIMAGE" ]] && sha256sum "$APPIMAGE" >> "${ARTIFACTS}/SHA256SUMS.txt"
fi

if [[ $DO_CHECKSUM -eq 1 ]]; then
  echo ">>> 校验和: ${ARTIFACTS}/SHA256SUMS.txt"
  cat "${ARTIFACTS}/SHA256SUMS.txt"
fi

echo "=== 打包完成 ==="
[[ $DO_DEB -eq 1 ]]      && echo "    DEB      : ${DEB:-<未生成>}"
[[ $DO_APPIMAGE -eq 1 ]] && echo "    AppImage : ${APPIMAGE:-<未生成>}"
