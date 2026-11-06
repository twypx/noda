#!/usr/bin/env python3
"""生成工具栏动作图标 PNG（深浅两套主题）。

图标形态来自 Lucide（ISC 许可），源 SVG 已固化在
resources/icons/toolbar/src/*.svg，因此本脚本不依赖任何外部仓库路径，
只依赖 python3-gi + librsvg + pycairo。

产出：
    resources/icons/toolbar/<theme>/<name>_<size>.png
    theme = dark | light，size = 16（常规 DPI）| 32（HiDPI 2x）
    resources/toolbar.qrc —— 与上面 PNG 一一对应的 qrc 清单（由脚本生成，随仓库提交）

用法：
    python3 tools/gen_toolbar_icons.py [--out-dir DIR] [--sizes 16,32] [--qrc PATH]

改图标形态时：直接编辑 src/ 下对应 SVG，或从新版 Lucide 重新导出该 SVG，
再运行本脚本并重跑 cmake（qrc 内容变化需要重新生成 rcc）。
"""

import argparse
import cairo
import gi
import io
import os
import re
import warnings

gi.require_version("Rsvg", "2.0")
from gi.repository import Rsvg  # noqa: E402

# 与 ThemeManager 的工具栏底色配套：深色底 #282828 上用最亮文字色，
# 浅色底 #f6f6f6 上用次深文字色，保证 hover 前后都有足够对比度。
THEME_COLORS = {
    "dark": "#cfcfcf",
    "light": "#4d4d4d",
}

DEFAULT_SIZES = [16, 32]


def render(svg_text: str, size: int, color: str) -> bytes:
    """把 SVG 文本中的 currentColor 替换成指定前景色后渲染为 PNG。"""
    data = re.sub(r'currentColor', color, svg_text, flags=re.I).encode("utf-8")
    with warnings.catch_warnings():
        # librsvg 2.5x 把 get_dimensions/render_cairo 标为 deprecated，
        # 但仍是能跨版本稳定取尺寸 + 绘制的组合。
        warnings.simplefilter("ignore", DeprecationWarning)
        handle = Rsvg.Handle.new_from_data(data)
        dim = handle.get_dimensions()
        scale = size / max(dim.width, dim.height)
        surf = cairo.ImageSurface(cairo.FORMAT_ARGB32, size, size)
        cr = cairo.Context(surf)
        cr.translate((size - dim.width * scale) / 2.0, (size - dim.height * scale) / 2.0)
        cr.scale(scale, scale)
        handle.render_cairo(cr)
        surf.flush()
    buf = io.BytesIO()
    surf.write_to_png(buf)
    return buf.getvalue()


def emit_qrc(qrc_path: str, out_dir: str, names, themes, sizes) -> None:
    """生成 qrc 清单。

    prefix 用 /toolbar/<theme>，alias 用 <name>_<size>.png，
    运行时路径即为 :/toolbar/dark/save_16.png，与 ThemeManager 的主题命名一致。
    """
    lines = ['<!DOCTYPE RCC><RCC version="1.0">']
    for theme in themes:
        lines.append(f'<qresource prefix="/toolbar/{theme}">')
        for name in names:
            for size in sizes:
                rel = os.path.relpath(os.path.join(out_dir, theme, f"{name}_{size}.png"),
                                      os.path.dirname(qrc_path))
                lines.append(f'    <file alias="{name}_{size}.png">{rel}</file>')
        lines.append('</qresource>')
    lines.append('</RCC>')
    with open(qrc_path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--src-dir", default=os.path.join(root, "resources", "icons", "toolbar", "src"))
    ap.add_argument("--out-dir", default=os.path.join(root, "resources", "icons", "toolbar"))
    ap.add_argument("--sizes", default=",".join(str(s) for s in DEFAULT_SIZES))
    ap.add_argument("--qrc", default=os.path.join(root, "resources", "toolbar.qrc"))
    args = ap.parse_args()

    sizes = [int(s) for s in args.sizes.split(",") if s.strip()]
    if not os.path.isdir(args.src_dir):
        print(f"找不到图标源目录: {args.src_dir}")
        return 1

    names = sorted(f[:-4] for f in os.listdir(args.src_dir) if f.endswith(".svg"))
    if not names:
        print(f"{args.src_dir} 下没有 SVG")
        return 1

    for name in names:
        with open(os.path.join(args.src_dir, name + ".svg"), encoding="utf-8") as f:
            svg = f.read()
        for theme, color in THEME_COLORS.items():
            out_dir = os.path.join(args.out_dir, theme)
            os.makedirs(out_dir, exist_ok=True)
            for size in sizes:
                path = os.path.join(out_dir, f"{name}_{size}.png")
                with open(path, "wb") as g:
                    g.write(render(svg, size, color))
    emit_qrc(args.qrc, args.out_dir, names, list(THEME_COLORS), sizes)
    print(f"生成 {len(names)} 个动作 x {len(THEME_COLORS)} 主题 x {len(sizes)} 尺寸 = "
          f"{len(names) * len(THEME_COLORS) * len(sizes)} 个 PNG -> {args.out_dir}")
    print(f"qrc 清单 -> {args.qrc}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
