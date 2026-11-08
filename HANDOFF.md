# HANDOFF — Notepad++ Linux 移植开发交接

> 交接于提交 `481409a`（master）。若当前 HEAD 已远超此提交，请以代码为准，本文可能已过期。

## 0. 先确认你在哪

- **唯一开发目录**：`/media/wtian/GithubProjects/notepad-plus-plus-linux`
- 这是 Gitee 仓库 `twypx/notepad-plus-plus-linux` 的 clone，**所有开发、提交、发版都在这里**。
- `/media/wtian/GithubProjects/notepad-plus-plus` 是上游 Notepad++ 的原版 clone，**只读参考**（查 Windows 实现、取 Scintilla/Lexilla 上游版本）。它的 `npp-linux` 副本已删除，不要在那里写代码。
- 两个目录**不再需要 rsync 同步**，旧流程已废弃。

若你发现自己在 `/media/wtian/GithubProjects/notepad-plus-plus` 里改文件，停下来——那是错的目录。

## 1. 项目是什么

Notepad++ 的 Ubuntu / Linux 原生移植：纯 **C++20 + Qt6 Widgets**，编辑核心直接用 Scintilla 的 Qt 接口（`ScintillaEditBase` + Lexilla）。不是 QML、不是 Webview、不是 Wine。

上游 `AGENTS.md` 写着「仅支持 Windows，不要添加其他平台支持」——那条与本项目冲突，本仓库**刻意不含**该文件。

## 2. 环境事实

| 项 | 值 |
|---|---|
| 系统 | Ubuntu 24.04，Wayland + GNOME |
| Qt | 6.4.2（`/usr/include/x86_64-linux-gnu/qt6`） |
| 构建 | CMake ≥ 3.20 + AUTOMOC/AUTORCC + LTO（Release 全量约 5–8 分钟，LTO 串行 58 个 LTRANS） |
| 依赖包 | `build-essential cmake qt6-base-dev qt6-base-dev-tools`；打包另需 `dpkg-dev` |

注意：Qt6 包名在本机带 **t64** 后缀（`libqt6widgets6t64` 等），这是 Ubuntu 24.04 的时间戳 ABI 转换。手写 deb 依赖会踩坑，所以本项目用 `dpkg-shlibdeps` 自动生成。

## 3. 目录结构

```
notepad-plus-plus-linux/
├── npp-linux/                 ← 移植本体（唯一需要改的地方）
│   ├── CMakeLists.txt         ← 版本号、安装规则、CPack 配置全在这里
│   ├── src/
│   │   ├── main.cpp           启动、资源搜索、图标、desktopFile 名
│   │   ├── MainWindow.cpp/h   主窗口、菜单、工具栏、视图开关、偏好应用
│   │   ├── EditorView.cpp     对 Scintilla 的封装（列选区、换行、显示符号）
│   │   ├── TabManager.cpp     多标签页
│   │   ├── DocumentBuffer.cpp 文档缓冲与磁盘状态
│   │   ├── StyleManager.cpp   解析 langs/stylers XML
│   │   ├── dialogs/           查找替换、列编辑器、首选项、快捷键、样式配置、跳转、文档摘要
│   │   ├── utils/             ThemeManager(两套 QSS)、PrintHelper、ToolbarIcon、EncodingConverter
│   │   └── widgets/TitleBar   无边框自绘标题栏（setMenuWidget 承载菜单栏）
│   ├── resources/             app.qrc、toolbar.qrc、icons/、deb/{postinst,postrm}、*.desktop.in
│   ├── tools/                 gen_icons.py、gen_toolbar_icons.py、release.sh
│   ├── build.sh               日常构建 → build/
│   ├── build_deb.sh           打 deb   → build-deb/
│   └── README.md              ★ 功能与设计的权威文档，改动后请同步更新
├── scintilla/ lexilla/        vendored 上游引擎（不要改）
└── PowerEditor/src/           vendored：uchardet、pugixml、langs/stylers.model.xml
```

`CMakeLists.txt` 用 `NPP_ROOT = ..` 定位依赖，**这个层级不能动**，否则构建直接失败。

## 4. 常用命令

```bash
cd /media/wtian/GithubProjects/notepad-plus-plus-linux/npp-linux

./build.sh                      # 日常构建 → build/notepad-plus-plus-linux
./build_deb.sh                  # 打 deb → build-deb/*.deb

# 发版（需要 GITEE_TOKEN，projects 权限）
cd .. && ./npp-linux/tools/release.sh v1.0.0
./npp-linux/tools/release.sh v1.0.0 --dry-run    # 只校验+构建，不推送不上传

# 免 root 把图标/desktop 注册到当前用户（开发时临时用）
./install_desktop.sh
```

发版硬规则：**deb 版本号来自 `CMakeLists.txt` 的 `project(VERSION)`，不是 tag**。发版前先改版本号并提交，`release.sh` 会拦截不一致。

## 5. 已实现功能（别重复造）

多标签页（新建/打开/保存/另存为/未保存提示）、Scintilla 原生编辑与撤销、行号与折叠、当前行高亮、编码自动识别（uchardet）、换行符识别与显示、查找替换（正则/全字/大小写/全部替换）、外部文件修改监控（inotify）、**列选模式与列编辑器**、打印、工具栏（可持续选中态）、深浅两套主题、无边框自绘标题栏、透明抠图应用图标、deb 打包、Gitee Release 发版。

细节与设计意图见 `npp-linux/README.md`，那里每节都写了「为什么这么做」。

## 6. 源码里值得先读的几处

- `main.cpp` 的资源搜索列表：按 `applicationDirPath()` 依次探测 `./`、`../share/notepad-plus-plus-linux/`、`../PowerEditor/src/`，**不依赖编译期绝对路径**，所以 `/usr`、`/usr/local`、解包到临时目录都能跑。
- `MainWindow::addToggleTool()`：QAction 可勾选 + `toggled`↔`setChecked` 双向同步 + `triggered` 绑定。
- `MainWindow::forEachEditor()`：视图开关必须作用于**每个**标签页，不能只改当前那个。
- `EditorView::restoreColumnSelections()`：列选区改写后按角点重建。
- `ThemeManager`：两套完整 QSS（dark/light），改样式必须两套都改。

## 7. 已知陷阱（改代码前必读，全是踩过的）

**Qt / QSS**

1. QSS 一旦接管某个部件，**不再回退原生外观**。
   - `QTabBar::close-button` 只要有样式规则，就必须显式给 `image: url(...)`，否则 X 整块空白。
   - `QToolButton` 只要有样式规则，就必须写 `:checked`，否则开关按钮开着关着看起来一样。
2. `QAction::setChecked()` **只发 `toggled`，不发 `triggered`**。渲染若挂在 `triggered` 上，代码里的 setChecked 不会触发它——「显示所有字符」这个合体开关就是这么断链的，必须自己把渲染做一遍。
3. `QMainWindow::setMenuWidget()` 之后，`menuBar()->actions()` 返回空，找菜单要用 `findChildren<QMenu*>()`。
4. `widget->grab()` 的渲染可能比逻辑状态**滞后一整批**，两张截图会内容互换。判断视觉差异要加 `QEventLoop + QTimer::singleShot(200, ...)` 等渲染稳定，并用像素统计而不是肉眼看缩略图。

**Scintilla**

5. 列选区内部表示是**每行一个 selection**，且**索引 0 是主选区，不是最上面那一行**。改写后重建角点必须按 `selLpos`/`selRpos` 取极值，不能用 `front()/back()`。
6. `SCI_GETCOLUMN`/`FINDCOLUMN` 的 column 按**字符**计，position 按**字节**计。混合 UTF-8/CJK 时全程在字节层拼接才不错位。

**探针与验证**

7. offscreen 探针（`QT_QPA_PLATFORM=offscreen`）直接实例化真实窗口类来验证 UI 逻辑，非常有效。但要注意：
   - 复用 CMake 目标文件链接探针：`find . -name "*.o" | grep notepad-plus-plus-linux.dir`；
   - 管道下必须 `stdbuf -o0`，否则看不到输出；
   - **调用真实槽函数会触发 `savePreferences()`，写坏 `~/.config/Notepad++/Notepad++.conf`**，跑完要检查；
   - 用 `QLatin1String(中文)` 与 QString 比较**恒为 false**，会让 helper 返回 nullptr 再段错误。
8. `DeleteFile` 工具对 ELF 可执行文件无效（报成功但没删），删二进制用精确路径的 `rm -f`。

**打包 / CPack**

9. `CPACK_RESOURCE_FILE_LICENSE` 在 DEB 生成器下**不会**落盘 `/usr/share/doc/<pkg>/copyright`，必须显式 `install(FILES ... RENAME copyright)`。
10. CPack **不生成** debhelper 那样的 dpkg 触发器，装完图标缓存不刷新——靠 `postinst`/`postrm` 手动跑 `gtk-update-icon-cache` 与 `update-desktop-database`。
11. `.desktop` 的 `Exec` 在 configure 阶段就被固化成绝对路径，CPack 又复用 CMakeCache。**必须** `-DCMAKE_INSTALL_PREFIX=/usr`，否则打出 `Exec=/usr/local/bin/...` 而文件在 `/usr/bin`，装完点图标没反应。`build_deb.sh` 每次清空构建目录来规避。

## 8. 未决事项

| 事项 | 说明 |
|---|---|
| **HiDPI 图标** | QSS 的 `image:` 走固定 16px，用不了 `QIcon` 的 DPR 机制，200% 缩放下关闭按钮会偏小。方案是在 `gen_toolbar_icons.py` 补 `x_16@2x.png` 别名（指向 32px 资源），Qt 会按 DPR 自动命中。**未实施，等用户确认其屏幕是否真受影响。** |
| **仓库可见性** | Gitee 仓库当前是**私有**，Release 附件对外不可见。若要公开下载需改仓库可见性。 |
| **首次 build/** | 新目录只有 `build-deb/`。要日常迭代先在 `npp-linux/` 跑一次 `./build.sh`（全量 5–8 分钟）。 |
| **无 AGENTS.md** | 本仓库刻意没放。若想约束提交信息格式等，需要单独加一份。 |

## 9. 与上游的关系

`scintilla/`、`lexilla/`、`PowerEditor/src/{uchardet,pugixml}` 都是上游 vendored 副本，**不要就地修改**（上游 `AGENTS.md` 也要求不改这两个引擎）。要升级引擎版本，从只读参考目录或上游仓库取新版本覆盖。

许可：`npp-linux/` 沿用 Notepad++ 的 GPL-3.0；Scintilla/Lexilla 为 Neil Hodgson 宽松许可；uchardet 为 MPL 1.1/GPL 2.0/LGPL 2.1；pugixml 为 MIT。详见根目录 `LICENSE` 与各子目录的许可文件。
