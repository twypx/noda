#!/usr/bin/env bash
# 把 Noda 打包成单文件 AppImage（无需安装、双击即跑、自带 Qt6 运行时）。
#
# 用法：
#   ./build_appimage.sh              # 构建（若未构建则先 Release 编译）
#   ./build_appimage.sh --no-build   # 假定 ./build/noda 已存在，只打包
#
# 首次运行会按需下载 linuxdeploy + linuxdeploy-plugin-qt + appimagetool 到 ./tools/。
# 需要联网；若机器离线，请提前把这些工具放到 ./tools/ 下对应文件名。
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
TOOLS_DIR="${SCRIPT_DIR}/tools"
APPDIR="${BUILD_DIR}/AppDir"
ARCH="$(uname -m)"
case "$ARCH" in
    x86_64) APPIMAGE_ARCH="x86_64" ;;
    aarch64) APPIMAGE_ARCH="aarch64" ;;
    *) APPIMAGE_ARCH="$ARCH" ;;
esac

NO_BUILD=0
[[ "${1:-}" == "--no-build" ]] && NO_BUILD=1

mkdir -p "${TOOLS_DIR}"

# ---- 1. 构建（除非跳过）----------------------------------------------------
if [[ $NO_BUILD -eq 0 ]]; then
    echo ">>> 构建 Release 版本 ..."
    mkdir -p "${BUILD_DIR}"
    cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release
    cmake --build "${BUILD_DIR}" --target noda -- -j"$(nproc)"
fi

if [[ ! -x "${BUILD_DIR}/noda" ]]; then
    echo "错误: 找不到可执行文件 ${BUILD_DIR}/noda，请先构建。" >&2
    exit 1
fi

# ---- 2. 下载打包工具（按需）------------------------------------------------
download() {  # url -> 目标文件
    local url="$1" out="$2"
    if [[ -f "$out" ]]; then return; fi
    echo ">>> 下载 $(basename "$out") ..."
    if command -v curl >/dev/null 2>&1; then
        curl -fSL "$url" -o "$out"
    else
        wget -O "$out" "$url"
    fi
    chmod +x "$out"
}

LINUXDEPLOY="${TOOLS_DIR}/linuxdeploy-${APPIMAGE_ARCH}.AppImage"
QT_PLUGIN="${TOOLS_DIR}/linuxdeploy-plugin-qt-${APPIMAGE_ARCH}.AppImage"

if [[ "$APPIMAGE_ARCH" == "x86_64" ]]; then
    download "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage" "$LINUXDEPLOY"
    download "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage" "$QT_PLUGIN"
else
    # aarch64 等架构走持续构建通道
    download "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${APPIMAGE_ARCH}.AppImage" "$LINUXDEPLOY"
    download "https://github.com/linuxdeploy/plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${APPIMAGE_ARCH}.AppImage" "$QT_PLUGIN"
fi

# ---- 3. 组装 AppDir --------------------------------------------------------
echo ">>> 组装 AppDir ..."
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin" "${APPDIR}/usr/share/applications" "${APPDIR}/usr/share/icons/hicolor/256x256/apps"

cp "${BUILD_DIR}/noda" "${APPDIR}/usr/bin/noda"

# .desktop 与图标：优先用仓库内置资源，否则现场生成最小可用版本。
if [[ -f "${BUILD_DIR}/noda.desktop" ]]; then
    cp "${BUILD_DIR}/noda.desktop" "${APPDIR}/usr/share/applications/noda.desktop"
else
    cat > "${APPDIR}/usr/share/applications/noda.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Noda
GenericName=Text Editor
Comment=Lightweight source/text editor with syntax highlighting
Exec=noda %F
Icon=noda
Terminal=false
Categories=Utility;TextEditor;
MimeType=text/plain;application/json;application/xml;
EOF
fi

ICON_SRC=""
for p in "${SCRIPT_DIR}/resources/icons/app_256.png" "${REPO_ROOT}/npp-linux/resources/icons/app_256.png"; do
    [[ -f "$p" ]] && ICON_SRC="$p" && break
done
if [[ -n "$ICON_SRC" ]]; then
    cp "$ICON_SRC" "${APPDIR}/usr/share/icons/hicolor/256x256/apps/noda.png"
fi

# AppImage 规范：AppDir 根需有 .desktop / 图标 / AppRun。
cp "${APPDIR}/usr/share/applications/noda.desktop" "${APPDIR}/noda.desktop"
[[ -n "$ICON_SRC" ]] && cp "$ICON_SRC" "${APPDIR}/noda.png"
cat > "${APPDIR}/AppRun" <<'EOF'
#!/bin/sh
HERE="$(dirname "$(readlink -f "${0}")")"
export PATH="${HERE}/usr/bin:${PATH}"
export LD_LIBRARY_PATH="${HERE}/usr/lib:${LD_LIBRARY_PATH:-}"
exec "${HERE}/usr/bin/noda" "$@"
EOF
chmod +x "${APPDIR}/AppRun"

# ---- 4. 打包 ---------------------------------------------------------------
echo ">>> 生成 AppImage ..."
VERSION="$(grep -oE 'VERSION +[0-9]+\.[0-9]+\.[0-9]+' "${SCRIPT_DIR}/CMakeLists.txt" \
    | head -1 | awk '{print $2}')"
export VERSION
"$LINUXDEPLOY" --appdir "${APPDIR}" --plugin qt --output appimage \
    --custom-appdir "${APPDIR}"

OUT="$(ls -1t "${SCRIPT_DIR}/noda-"*"${APPIMAGE_ARCH}.AppImage" 2>/dev/null | head -1)"
echo "=== 完成 ==="
echo "    AppImage: ${OUT:-${SCRIPT_DIR}/noda-*.AppImage}"
echo "    安装即用: chmod +x <AppImage> && ./<AppImage>"
