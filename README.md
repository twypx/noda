# Notepad++ Linux Port

Notepad++ 的 Ubuntu / Linux 原生移植：**纯 C++20 + Qt6 Widgets**，编辑核心直接用 Scintilla 的
Qt 接口（`ScintillaEditBase`），不是 QML、不是 Webview，也不是 Wine 包装。

主要代码在 [`npp-linux/`](npp-linux/)，其中文文档、架构说明与各功能细节以
**[npp-linux/README.md](npp-linux/README.md)** 为准。

## 仓库结构

本仓库自带构建所需的全部第三方源码，因此保持了这个目录层级 —— `npp-linux/CMakeLists.txt`
用 `NPP_ROOT = ..` 定位依赖，改动结构会直接导致构建失败。

| 路径 | 来源 | 说明 |
|---|---|---|
| `npp-linux/` | 本移植项目 | 移植本体：主窗口、编辑器封装、各对话框、主题与图标工具链 |
| `scintilla/` | 上游 vendored | 编辑引擎（文本缓冲、选区、撤销、渲染） |
| `lexilla/` | 上游 vendored | 词法高亮引擎与各语言 lexer |
| `PowerEditor/src/uchardet/` | 上游 vendored | 字符编码自动识别（UTF-8 / GBK / Big5 等） |
| `PowerEditor/src/pugixml/` | 上游 vendored | 解析语言与样式 XML |
| `PowerEditor/src/langs.model.xml`<br>`PowerEditor/src/stylers.model.xml` | 上游 vendored | 语言定义与主题样式定义，直接复用 Notepad++ 原版资产 |

## 构建与运行

```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev qt6-base-dev-tools

cd npp-linux
./build.sh
./build/notepad-plus-plus-linux
```

工具栏与应用图标的 PNG 由 `npp-linux/tools/*.py` 离线生成并内嵌进 Qt 资源，
常规构建不需要 Python 依赖，也不需要联网或系统图标主题。若要修改图标形态，
见 `npp-linux/README.md` 的"工具栏图标"一节。

## 许可

- `npp-linux/` 沿用 Notepad++ 上游的 **GPL-3.0**（见根目录 [`LICENSE`](LICENSE)）。
- `scintilla/` 与 `lexilla/` 为 Neil Hodgson 的宽松许可，见各自的 `License.txt`。
- `PowerEditor/src/uchardet/` 为 **MPL 1.1 / GPL 2.0 / LGPL 2.1** 三重许可（各文件头部声明）。
- `PowerEditor/src/pugixml/` 为 **MIT**。

本移植项目与 Notepad++ 官方项目相互独立，不是官方发布物。
