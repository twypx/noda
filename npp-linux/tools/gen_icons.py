#!/usr/bin/env python3
"""从 notepad++_cutout.png 生成透明背景的应用图标。

源图是 RGBA 透明抠图，图标主体就是纸面与变色龙本身。为保证任务栏/启动器
小尺寸清晰：先裁掉透明边距，再让每个目标尺寸直接从源图 LANCZOS 缩放
（不经过 1024 母图二次重采样），最后做轻度 UnsharpMask 锐化。

产出三类文件（默认都落在 resources/icons/ 下）：
  1. <qrc_dir>/app_master_1024.png         —— 透明母图，供 scalable 条目使用
  2. <qrc_dir>/app_<N>.png                 —— 供 resources/app.qrc 内嵌，运行时作为窗口图标
  3. <theme_dir>/hicolor/<S>x<S>/apps/<icon_name>.png —— 供 GNOME/KDE 任务栏、应用列表使用

用法：
    python3 tools/gen_icons.py [--src PATH] [--qrc-dir DIR] [--theme-dir DIR] [--icon-name NAME]

依赖：python3-pil（Pillow）。
"""

import argparse
import os

from PIL import Image, ImageFilter

MASTER_SIZE = 1024
CONTENT_MAX_RATIO = 0.98   # 裁边后内容最长边占目标尺寸比例，尽量占满有效像素
ALPHA_TRIM_THRESHOLD = 16  # 低于此 alpha 的边距/软阴影不计入内容边界
UNSHARP_PERCENT = 120      # 轻度锐化强度，避免小尺寸出现明显白边
UNSHARP_THRESHOLD = 2

QRC_SIZES = [16, 24, 32, 48, 64, 128, 256]
THEME_SIZES = [16, 22, 24, 32, 48, 64, 96, 128, 192, 256, 512]


def trimmed_source(src: Image.Image) -> Image.Image:
    alpha = src.getchannel("A").point(lambda v: 255 if v > ALPHA_TRIM_THRESHOLD else 0)
    bbox = alpha.getbbox()
    return src.crop(bbox) if bbox else src


def render_size(src: Image.Image, side: int) -> Image.Image:
    scale = min(side / src.width, side / src.height) * CONTENT_MAX_RATIO
    size = (max(1, round(src.width * scale)), max(1, round(src.height * scale)))
    content = src.resize(size, Image.LANCZOS)
    canvas = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    canvas.paste(content, ((side - size[0]) // 2, (side - size[1]) // 2))
    radius = max(0.6, side * 0.004)
    return canvas.filter(ImageFilter.UnsharpMask(radius=radius,
                                                 percent=UNSHARP_PERCENT,
                                                 threshold=UNSHARP_THRESHOLD))


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.join(root, "notepad++_cutout.png"))
    ap.add_argument("--qrc-dir", default=os.path.join(root, "resources", "icons"))
    ap.add_argument("--theme-dir", default=os.path.join(root, "resources", "icons"))
    ap.add_argument("--icon-name", default="notepad-plus-plus-linux")
    args = ap.parse_args()

    if not os.path.exists(args.src):
        print(f"找不到源图: {args.src}")
        return 1

    src = trimmed_source(Image.open(args.src).convert("RGBA"))
    os.makedirs(args.qrc_dir, exist_ok=True)
    render_size(src, MASTER_SIZE).save(os.path.join(args.qrc_dir, "app_master_1024.png"))
    print("  母图 1024x1024 OK")

    for size in sorted(set(QRC_SIZES) | set(THEME_SIZES)):
        data = render_size(src, size)
        if size in QRC_SIZES:
            data.save(os.path.join(args.qrc_dir, f"app_{size}.png"))
        if size in THEME_SIZES:
            d = os.path.join(args.theme_dir, "hicolor", f"{size}x{size}", "apps")
            os.makedirs(d, exist_ok=True)
            data.save(os.path.join(d, f"{args.icon_name}.png"))
    print(f"  输出 {len(set(QRC_SIZES) | set(THEME_SIZES))} 个尺寸")

    # 母图同时作为 scalable 条目，供支持任意缩放的桌面环境取用。
    scalable = os.path.join(args.theme_dir, "hicolor", "scalable", "apps")
    os.makedirs(scalable, exist_ok=True)
    render_size(src, MASTER_SIZE).save(os.path.join(scalable, f"{args.icon_name}.png"))
    print("  scalable 母图 OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())