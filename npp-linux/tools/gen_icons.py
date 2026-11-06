#!/usr/bin/env python3
"""从 notepad++.png 生成 iOS 风格的应用图标。

iOS 应用图标的要素在这里逐条落实：
  1. 超椭圆（squircle）外形 —— 用 Lamé 曲线 |x/a|^n + |y/b|^n = 1 (n=5) 采样生成遮罩，
     比普通的圆角矩形更接近 Apple 的连续曲率切角。
  2. 满幅品牌色背景 —— 原图的近黑底色被软色键替换成上亮下暗的绿色渐变，
     只影响“接近背景色”的像素，纸面与变色龙的固有颜色不受干扰。
  3. 悬浮卡片投影 —— 用内容 alpha 模糊下移后只在内容之外着色，让纸面浮起来。
  4. 顶部高光 + 底部压暗 + 内侧亮边 —— screen 混合的径向高光营造玻璃感，
     沿遮罩内描一条低透明度亮边，使深色标题栏上仍能看清图标轮廓。

产出三类文件（默认都落在 resources/icons/ 下）：
  1. <qrc_dir>/app_master_1024.png         —— iOS 处理后的母图，所有小尺寸由它缩放而来
  2. <qrc_dir>/app_<N>.png                 —— 供 resources/app.qrc 内嵌，运行时作为窗口图标（不依赖 Qt6Svg 开发包）
  3. <theme_dir>/hicolor/<S>x<S>/apps/<icon_name>.png —— 供 GNOME/KDE 任务栏、应用列表使用

用法：
    python3 tools/gen_icons.py [--src PATH] [--qrc-dir DIR] [--theme-dir DIR] [--icon-name NAME]

依赖：python3-pil（Pillow）。
"""

import argparse
import math
import os

from PIL import Image, ImageChops, ImageDraw, ImageFilter

MASTER_SIZE = 1024

# ---- iOS 外形 ----
SQUIRCLE_N = 5.0          # Lamé 指数，5 最接近 Apple 的连续曲率切角
SQUIRCLE_SAMPLES = 2400   # 多边形采样点数，配合 4x 超采样后边缘看不到折线

# ---- 背景软色键 ----
SRC_BG = (22, 26, 31)     # notepad++.png 的原始底色
KEY_THRESHOLD = 46.0      # 与原底色的通道色差小于此值才被视为背景（单位：L 模式色差）

# 背景取 Notepad++ 变色龙的绿色：比原图的近黑底在 16px 下辨识度高得多，
# 且在深色标题栏（#1e1e1e）和浅色标题栏（#ececec）上都能保住图标轮廓。
BG_TOP = (96, 164, 62)
BG_BOTTOM = (34, 98, 32)

# ---- 悬浮投影 ----
SHADOW_GAIN = 0.45        # 投影不透明度
SHADOW_BLUR = 22          # 高斯模糊半径
SHADOW_DY = 18            # 向下偏移量
SHADOW_TINT = (12, 32, 10)  # 投影颜色（深绿黑，避免灰色脏底）

# ---- 光照 ----
GLOSS_ALPHA = 0.075       # 顶部径向高光强度（screen 混合）
VIGNETTE_BOTTOM = 236     # 底部压暗到的灰度（multiply 混合）
RIM_ALPHA = 0.12          # 内侧亮边不透明度
RIM_WIDTH_RATIO = 0.0045  # 亮边线宽（相对母图边长）

QRC_SIZES = [16, 24, 32, 48, 64, 128, 256]
THEME_SIZES = [16, 22, 24, 32, 48, 64, 96, 128, 192, 256, 512]


def squircle_points(side: int, inset: float = 0.0, n: float = SQUIRCLE_N):
    """按 1x 尺寸给出超椭圆顶点序列，inset 用于描出内缩的一圈。"""
    r = side / 2.0 - inset
    c = side / 2.0
    pts = []
    for i in range(SQUIRCLE_SAMPLES):
        t = 2 * math.pi * i / SQUIRCLE_SAMPLES
        ct, st = math.cos(t), math.sin(t)
        pts.append((c + r * math.copysign(abs(ct) ** (2.0 / n), ct),
                    c + r * math.copysign(abs(st) ** (2.0 / n), st)))
    return pts


def squircle_mask(side: int, n: float = SQUIRCLE_N, ss: int = 4) -> Image.Image:
    """生成超椭圆 alpha 遮罩。先按 ss 倍尺寸绘制再缩回，得到平滑的抗锯齿边缘。"""
    big = side * ss
    m = Image.new("L", (big, big), 0)
    ImageDraw.Draw(m).polygon(squircle_points(big, 0.0, n), fill=255)
    return m.resize((side, side), Image.LANCZOS)


def vertical_gradient(side: int, top, bottom) -> Image.Image:
    g = Image.new("RGB", (1, side))
    for y in range(side):
        k = y / (side - 1)
        g.putpixel((0, y), tuple(round(a + (b - a) * k) for a, b in zip(top, bottom)))
    return g.resize((side, side))


def background_key(src: Image.Image, bg, threshold: float) -> Image.Image:
    """返回"背景程度"灰度图：越接近底色越白，用于把底色替换成渐变。

    用逐通道色差折算成 L 作为距离，取值天然落在 0..255，不会溢出截断。
    """
    diff = ImageChops.difference(src, Image.new("RGB", src.size, bg)).convert("L")
    return diff.point(lambda v: max(0, 255 - int(255 * v / threshold)))


def add_drop_shadow(img: Image.Image, content_alpha: Image.Image,
                    bg_key: Image.Image) -> Image.Image:
    """把内容轮廓模糊下移后当作投影，只在内容之外（bg_key 高的地方）着色。"""
    sh = ImageChops.offset(content_alpha.filter(ImageFilter.GaussianBlur(SHADOW_BLUR)),
                           0, SHADOW_DY)
    sh = ImageChops.multiply(sh, bg_key).point(lambda v: int(v * SHADOW_GAIN))
    return Image.composite(Image.new("RGB", img.size, SHADOW_TINT), img, sh)


def add_gloss(img: Image.Image, side: int, alpha: float) -> Image.Image:
    """顶部偏左的径向白色高光，screen 混合，营造 iOS 的玻璃质感。"""
    gloss = Image.new("L", (side, side), 0)
    gd = ImageDraw.Draw(gloss)
    cx, cy, rad, steps = side * 0.34, -side * 0.28, side * 1.15, 96
    for i in range(steps, 0, -1):  # 由外向内逐圈绘制，最内圈（最亮）最后覆盖
        rr = rad * i / steps
        v = int(255 * alpha * (1.0 - (i - 1) / steps) ** 2)
        gd.ellipse([cx - rr, cy - rr, cx + rr, cy + rr], fill=v)
    gloss = gloss.filter(ImageFilter.GaussianBlur(side * 0.03))
    gray = Image.merge("RGB", (gloss, gloss, gloss))
    return ImageChops.screen(img, gray)


def build_master(src_path: str, side: int = MASTER_SIZE) -> Image.Image:
    src = Image.open(src_path).convert("RGB")
    if src.size != (side, side):
        src = src.resize((side, side), Image.LANCZOS)

    # 1) 底色软色键 -> 绿色渐变；同时留一份内容 alpha 给投影用
    key = background_key(src, SRC_BG, KEY_THRESHOLD)
    img = Image.composite(vertical_gradient(side, BG_TOP, BG_BOTTOM), src, key)
    img = add_drop_shadow(img, key.point(lambda v: 255 - v), key)

    # 2) 顶部高光 + 底部压暗
    img = add_gloss(img, side, GLOSS_ALPHA)
    img = ImageChops.multiply(img, vertical_gradient(side, (255, 255, 255),
                                                     (VIGNETTE_BOTTOM,) * 3))

    # 3) 内侧亮边：先沿内缩一圈描边，再与遮罩相与，只保留边界内侧的一半
    mask = squircle_mask(side)
    w = max(2, int(side * RIM_WIDTH_RATIO))
    rim = Image.new("L", (side, side), 0)
    ImageDraw.Draw(rim).polygon(squircle_points(side, w / 2.0), outline=255, width=w)
    rim = ImageChops.multiply(rim, mask).point(lambda v: int(v * RIM_ALPHA))
    img = Image.composite(Image.new("RGB", (side, side), (255, 255, 255)), img, rim)

    img.putalpha(mask)
    return img


def main() -> int:
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.dirname(here)
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=os.path.join(root, "notepad++.png"))
    ap.add_argument("--qrc-dir", default=os.path.join(root, "resources", "icons"))
    ap.add_argument("--theme-dir", default=os.path.join(root, "resources", "icons"))
    ap.add_argument("--icon-name", default="notepad-plus-plus-linux")
    args = ap.parse_args()

    if not os.path.exists(args.src):
        print(f"找不到源图: {args.src}")
        return 1

    img = build_master(args.src)
    os.makedirs(args.qrc_dir, exist_ok=True)
    img.save(os.path.join(args.qrc_dir, "app_master_1024.png"))
    print("  母图 1024x1024 OK")

    for size in sorted(set(QRC_SIZES) | set(THEME_SIZES)):
        data = img.resize((size, size), Image.LANCZOS)
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
    img.save(os.path.join(scalable, f"{args.icon_name}.png"))
    print("  scalable 母图 OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
