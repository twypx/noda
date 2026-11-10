#!/usr/bin/env bash
# 把应用图标与 .desktop 注册到当前用户（无需 root），让 GNOME/KDE 的任务栏、
# 活动总览、应用列表显示 notepad++_cutout.png 生成的透明背景图标。
#
# 用法：
#   ./install_desktop.sh [可执行文件绝对路径]
# 不传参数时，默认使用 /usr/local/bin/noda，
# 若该路径不存在则回退到本仓库的 build/noda。
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
icon_name="noda"
exe="${1:-}"

if [[ -z "$exe" ]]; then
    if [[ -x "/usr/local/bin/$icon_name" ]]; then
        exe="/usr/local/bin/$icon_name"
    elif [[ -x "$here/build/$icon_name" ]]; then
        exe="$here/build/$icon_name"
    else
        echo "找不到可执行文件，请把路径作为第一个参数传入。" >&2
        exit 1
    fi
fi
exe="$(readlink -f "$exe")"

# 1. 图标：优先用已生成好的 hicolor 目录；缺失时现场生成（需要 python3-pil）。
if [[ ! -d "$here/resources/icons/hicolor" ]]; then
    echo "==> 生成 PNG 图标"
    python3 "$here/tools/gen_icons.py"
fi

dest_icons="$HOME/.local/share/icons/hicolor"
mkdir -p "$dest_icons"
cp -r "$here/resources/icons/hicolor/." "$dest_icons/"
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    # 目录里没有 index.theme 时它返回 2（"No theme index file"）。图标本身不依赖缓存，
    # 不能因为这个可选的优化失败就让 set -e 中断，否则 .desktop 根本写不出去。
    gtk-update-icon-cache -q "$dest_icons" >/dev/null 2>&1 || true
fi

# 2. .desktop：用真实可执行路径替换占位符。
dest_apps="$HOME/.local/share/applications"
mkdir -p "$dest_apps"
desktop="$dest_apps/$icon_name.desktop"
sed -e "s|@NPP_APP_EXE@|$exe|g" "$here/resources/$icon_name.desktop.in" > "$desktop"
chmod 644 "$desktop"

if command -v desktop-file-validate >/dev/null 2>&1; then
    desktop-file-validate "$desktop" && echo "==> .desktop 校验通过"
fi
if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database -q "$dest_apps" >/dev/null 2>&1 || true
fi

echo "==> 已注册："
echo "   Exec  = $exe"
echo "   Icon  = $icon_name (hicolor, 16~512px + scalable 1024px)"
echo "   桌面文件 = $desktop"
echo "如任务栏图标未立即刷新，注销重登或执行： gtk-update-icon-cache ~/.local/share/icons/hicolor"
