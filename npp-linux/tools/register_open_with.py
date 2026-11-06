#!/usr/bin/env python3
"""把本应用注册进 GNOME 右键「打开方式」的推荐列表。

用法：
    tools/register_open_with.py              # 给所有属于 text/plain 的文本类型加关联
    tools/register_open_with.py --dry-run    # 只打印将改动的类型，不落盘
    tools/register_open_with.py --remove     # 撤销本脚本加过的关联

为什么需要它——GNOME 的候选列表分两层，职责完全不同：

  * 「用其他应用打开…」完整对话框读 mimeinfo.cache 的**已注册**列表，
    由 postinst 里的 update-desktop-database 生成，内容取决于 .desktop 的 MimeType。
  * 右键「打开方式」**子菜单**只显示**推荐**列表，来源是 mimeapps.list 的
    [Added Associations]。MimeType 写得再多也不会自动进这一层：一旦 gedit、
    Obsidian 之类应用已经声明过该类型，本应用就被挤出子菜单、看起来像"没装上"。

所以 MimeType 负责「对话框里有没有」，本脚本负责「子菜单里有没有」。
另外 mimeapps.list 支持给 .desktop 未声明的类型建立关联，两个列表互不依赖。
"""

import os
import shutil
import sys
import time

DESKTOP_ID = "notepad-plus-plus-linux.desktop"
SECTION = "[Added Associations]"


def mimeapps_path():
    base = os.environ.get("XDG_CONFIG_HOME") or os.path.expanduser("~/.config")
    return os.path.join(base, "mimeapps.list")


def text_types():
    """返回所有继承自 text/plain 的类型。

    直接扫 /usr/share/mime/types 并逐个做继承判断，而不是只取 text/* 前缀：
    application/json、application/x-shellscript、application/toml 这些虽然不是
    text/ 开头，也都是地道的文本文件。
    """
    import gi

    gi.require_version("Gio", "2.0")
    from gi.repository import Gio

    types_file = "/usr/share/mime/types"
    with open(types_file, encoding="utf-8") as f:
        all_types = [line.strip() for line in f if line.strip()]
    return [t for t in all_types if Gio.content_type_is_a(t, "text/plain")]


def split_section(lines):
    """定位 [Added Associations] 段，返回 (段首行号, 段尾行号)。"""
    start = None
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped == SECTION:
            start = i
        elif start is not None and i > start and stripped.startswith("[") and stripped.endswith("]"):
            return start, i
    if start is None:
        return None, None
    return start, len(lines)


def merge(lines, types, add):
    """在段内增删关联。同一类型可能有多行，都处理，避免留下互相矛盾的条目。"""
    start, end = split_section(lines)
    if start is None:
        lines += ["", SECTION]
        start, end = len(lines) - 1, len(lines)

    touched = 0
    drop = []
    for i in range(start + 1, end):
        line = lines[i]
        if "=" not in line or line.lstrip().startswith("#"):
            continue
        key, value = line.split("=", 1)
        if key.strip() not in types:
            continue
        entries = [e for e in value.split(";") if e]
        if add and DESKTOP_ID not in entries:
            entries.append(DESKTOP_ID)
            lines[i] = "%s=%s;" % (key, ";".join(entries))
            touched += 1
        elif not add and DESKTOP_ID in entries:
            entries.remove(DESKTOP_ID)
            touched += 1
            if entries:
                lines[i] = "%s=%s;" % (key, ";".join(entries))
            else:
                drop.append(i)

    for i in reversed(drop):
        del lines[i]

    if add:
        # 文件里还没出现过的类型，按 Debian 惯例在段末补齐
        present = {line.split("=", 1)[0].strip() for line in lines[start + 1:end] if "=" in line}
        for t in types:
            if t not in present:
                lines.append("%s=%s;" % (t, DESKTOP_ID))
                touched += 1
    return touched


def main(argv):
    dry_run = "--dry-run" in argv
    remove = "--remove" in argv

    want = set(text_types())
    path = mimeapps_path()
    print("配置: %s" % path)
    print("属于 text/plain 的文本类型: %d 个" % len(want))

    lines = []
    if os.path.exists(path):
        with open(path, encoding="utf-8") as f:
            lines = f.read().splitlines()

    touched = merge(lines, want, not remove)
    action = "移除" if remove else "写入"
    if touched == 0:
        print("无需改动：%s 已经是最新状态。" % DESKTOP_ID)
        return 0
    if dry_run:
        print("[dry-run] 将%s %d 处关联，未落盘。" % (action, touched))
        return 0

    if os.path.exists(path):
        bak = "%s.bak-%s" % (path, time.strftime("%Y%m%d-%H%M%S"))
        shutil.copy2(path, bak)
        print("已备份: %s" % bak)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print("已%s %d 处关联。右键「打开方式」应立即生效，无需重启文件管理器。" % (action, touched))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
