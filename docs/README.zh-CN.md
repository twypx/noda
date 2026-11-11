<p align="center">
  <img src="../npp-linux/resources/icons/app_256.png" width="112" alt="Noda LOGO">
</p>

<h1 align="center">Noda</h1>

<p align="center"><b>Linux 上最快的原生文本编辑器。</b><br>
C++20 + Qt6 Widgets · Scintilla/Lexilla 内核 · 不用 Wine、不用 Electron、不用 WebView。</p>

<p align="center">
  <a href="../README.md">English</a> &nbsp;|&nbsp; <b>简体中文</b>
</p>

---

Noda 是一款面向 Ubuntu / Linux 的原生文本编辑器，**基于 Notepad++ 源码修改**：复用其资产与
逻辑（`langs.model.xml`、`stylers.model.xml`、uchardet、Scintilla、Lexilla），但用纯
Qt6 Widgets 重写。它不是 Wine 包装，也不是 Electron 应用。

> **归属说明**：本项目是对 Notepad++ 源码的独立修改，按 GPL-3.0 发布。
> 与 Notepad++ 及其作者不存在隶属、背书或关联关系。

## 相比 Notepad++ 的增强

部分能力与上游重叠；这里强调的是它们在Noda里**开箱即用、零第三方依赖、并为 Linux 调优**，
而不是靠插件或兼容层拼出来。

| 能力 | Notepad++（Windows） | Noda |
|---|---|---|
| 平台 | 仅 Windows | **Linux 原生**（X11 + Wayland），提供 `.deb`、`.desktop`、hicolor 图标 |
| 运行时 | Win32 / GDI | **C++20 + Qt6 Widgets**，冷启动约 80–120ms，内存约 35–45MB |
| 二进制 / 十六进制查看 | 需 HexEditor 插件 | **内置原始字节查看器**：`mmap`、只读可见窗口、无整份拷贝、支持 >4GB 地址 |
| 文件夹工作区 | “Folder as Workspace” | **内置左侧停靠窗**：惰性加载、零递归预扫描、自绘展开箭头与层级线 |
| 界面语言 | `nativeLang/*.xml` | **运行时中英文切换**，菜单 / 工具栏提示 / 状态栏全量双语 |
| 主题 | 内置主题 | **Obsidian 风格深浅两套色板**，中英文字体统一，无边框自绘标题栏 |
| 代码格式化 | 插件 / 外部工具 | **内置 AStyle（C/C++/C#/Java/Obj-C）+ JSON 美化**，工作线程执行 |
| TypeScript | 依赖词法器配置 | **`.ts` / `.tsx` 高亮**，复用 cpp 词法器并补全正确属性 |
| 会话与备份 | 周期性备份 | **会话快照 + 自动备份**，未保存内容连同光标位置原样恢复 |

## 亮点

- **性能优先的工程实现**：选词高亮只扫可见区域并带硬性时间预算、字体清单与主题元信息单例、
  TabBar 批量重绘、mmap 十六进制视图，且不引入任何投机性依赖。
- **严肃、现代的界面**：树形首选项、可编辑且带冲突检测的快捷键映射器、按「语言 × 样式槽位」
  的样式配置器；只呈现真实可用的开关，没有占位配置。
- **Linux 原生集成**：X11 启动通知补漏、`%F` 文件关联、`register_open_with.py` 桌面右键助手、
  正确的 Wayland 处理。
- **URL 可点击**：可见区域内的 URL 自动加下划线，`Ctrl` + 点击即在默认浏览器打开。
- **二进制文件直接可用**：含 NUL 字节的文件自动以十六进制打开，显示**磁盘原始字节**而非解码后的文本。
- **面向大文件的比对引擎**：Histogram/Patience 锚点 + 有界 Myers（无 O(D²) 内存）、wyhash 风格块哈希、
  真正的字符级行内差异、块移动检测，以及 `QtConcurrent` 异步比对，UI 不卡顿。
- **不止比两份文件**：整篇 / 选区 / 与 Git HEAD / 与 SVN BASE 比对（`QProcess` 调用系统命令，零链接依赖），
  差异小地图点击跳转，按对齐行映射的滚动同步在占位行存在时也不漂移。

## 核心功能

- [x] 多标签页：新建 / 打开 / 保存 / 另存为 / 拷贝另存为、未保存提示、双击标签栏空白处新建。
- [x] 标签右键菜单：关闭 / 批量关闭、保存、重命名、移到回收站、重新载入、打印、只读、复制路径、
      移动标签、打开所在文件夹。
- [x] 拖拽文件打开（多文件）、命令行 / IPC 打开。
- [x] Scintilla 原生编辑：撤销 / 重做、剪切 / 复制 / 粘贴、全选、列选模式。
- [x] 行号与折叠列、当前行高亮、自动换行。
- [x] 编码识别（UTF-8 / GBK / Big5 / UTF-16 …）与换行符识别（LF / CRLF / CR）。
- [x] 快速查找替换：区分大小写、全字、正则、全部替换。
- [x] 基于 `inotify` 的外部文件修改监控与重载提示。
- [x] 会话记忆 + 未保存内容自动备份。
- [x] 代码格式化：C 系走 AStyle，JSON 走保序美化器，均在工作线程执行。
- [x] 样式配置器：按「语言 × 样式槽位」编辑颜色、粗斜体、字体族与字号。
- [x] 树形首选项：主题、界面语言、标签栏、自动补全、高亮匹配、界面字体、编辑器行为。
- [x] 快捷键映射器：由真实菜单动作生成，可编辑、冲突检测、恢复默认。
- [x] 打开文件夹：左侧停靠窗惰性加载的目录树，零递归预扫描。
- [x] 内置十六进制 / 二进制查看器，显示磁盘原始字节。
- [x] TypeScript / TSX 语法高亮。
- [x] 并排比对：整篇 / 选区 / Git HEAD / SVN BASE / 选择文件，块移动识别、行内字符高亮、
      差异导航（`F8` / `Shift+F8`）、差异分布小地图；工具栏按钮为下拉开关型，再次点击即退出比对
      （也可 `Ctrl+Alt+X`）。「显示所有字符」按钮同样带下拉，可直接选显示空格/换行符等。
- [x] 忽略空白 / 大小写 / 换行符选项、运行时中英文切换、改名后旧配置一次性自动迁移。

## 快速开始

```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev qt6-base-dev-tools

cd npp-linux
./build.sh
./build/noda
```

打包为 `.deb`：

```bash
cd npp-linux
./build_deb.sh
sudo apt install ./build-deb/noda_*_amd64.deb
```

工具栏与应用图标由 `npp-linux/tools/*.py` 离线生成并内嵌为 Qt 资源，常规构建不需要 Python、
不需要联网，也不依赖系统图标主题。

## 仓库结构

本仓库自带构建所需的全部第三方源码，因此目录层级是**承载构建的**：
`npp-linux/CMakeLists.txt` 通过 `NPP_ROOT = ..` 定位依赖。

| 路径 | 来源 | 说明 |
|---|---|---|
| `npp-linux/` | Noda | 应用本体：主窗口、编辑器封装、各对话框、主题与图标工具链 |
| `scintilla/` | 上游 vendored | 编辑引擎（文本缓冲、选区、撤销、渲染） |
| `lexilla/` | 上游 vendored | 词法高亮引擎与各语言 lexer |
| `PowerEditor/src/uchardet/` | 上游 vendored | 字符编码自动识别 |
| `PowerEditor/src/pugixml/` | 上游 vendored | 解析语言与样式 XML |
| `PowerEditor/src/langs.model.xml`<br>`PowerEditor/src/stylers.model.xml` | 上游 vendored | 语言与主题定义，直接复用原版资产 |


## 文档

- 英文技术文档（默认）：**[../npp-linux/README.md](../npp-linux/README.md)**
- 完整中文技术文档：**[README.technical.zh-CN.md](README.technical.zh-CN.md)**
- 开发计划与模块记录：**[开发计划2.md](开发计划2.md)**
- English README: **[../README.md](../README.md)**

## 许可

- Noda 按 **GNU General Public License v3.0（GPL-3.0）** 发布（见根目录 [`LICENSE`](../LICENSE)）；
  原始 Notepad++ 代码版权归 Copyright (C) 2021 Don HO 所有。
- `scintilla/` 与 `lexilla/` 为 Neil Hodgson 的宽松许可（见各自的 `License.txt`）。
- `PowerEditor/src/uchardet/` 为 **MPL 1.1 / GPL 2.0 / LGPL 2.1** 三重许可。
- `PowerEditor/src/pugixml/` 为 **MIT**。

Noda 是独立项目，与 Notepad++ 及其作者不存在隶属、背书或关联关系，也不是 Notepad++ 官方发布物。
