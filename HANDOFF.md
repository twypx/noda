# HANDOFF — Noda 开发交接

> 原交接基于 `481409a`；当前基线为远程 `origin/master` = `dec7c62`（v1.0.8）。
> 本轮「半成品与静默失败治理」的改动**尚未提交**；若 HEAD 已变动，请以代码与英文技术文档 `npp-linux/README.md` 为准，完整中文版见 `docs/README.technical.zh-CN.md`。

## 0. 先确认你在哪

- **唯一开发目录**：`/media/wtian/GithubProjects/notepad-plus-plus-linux`
- 这是 Gitee 仓库 `twypx/notepad-plus-plus-linux` 的 clone，**所有开发、提交、发版都在这里**。
- `/media/wtian/GithubProjects/notepad-plus-plus` 是上游 Notepad++ 的原版 clone，**只读参考**（查 Windows 实现、取 Scintilla/Lexilla 上游版本）。它的 `npp-linux` 副本已删除，不要在那里写代码。
- 两个目录**不再需要 rsync 同步**，旧流程已废弃。

若你发现自己在 `/media/wtian/GithubProjects/notepad-plus-plus` 里改文件，停下来——那是错的目录。

## 1. 项目是什么

Noda 是面向 Ubuntu / Linux 的原生文本编辑器，基于 Notepad++ 源码独立修改（GPL-3.0）：纯 **C++20 + Qt6 Widgets**，编辑核心直接用 Scintilla 的 Qt 接口（`ScintillaEditBase` + Lexilla）。不是 QML、不是 Webview、不是 Wine。与 Notepad++ 及其作者无隶属、背书或关联关系。

上游 `AGENTS.md` 写着「仅支持 Windows，不要添加其他平台支持」——那条与本项目冲突，本仓库**刻意不含**该文件。

## 2. 环境事实

| 项 | 值 |
|---|---|
| 系统 | Ubuntu 24.04，Wayland + GNOME |
| Qt | 6.4.2（`/usr/include/x86_64-linux-gnu/qt6`），需要 `Qt6::Concurrent` |
| 构建 | CMake ≥ 3.20 + AUTOMOC/AUTORCC + LTO（-flto=auto 多核并行链接） |
| 测试 | Qt6 Test + CTest，offscreen 平台插件，见 §10 |
| 依赖包 | `build-essential cmake qt6-base-dev qt6-base-dev-tools`；打包另需 `dpkg-dev` |

注意：Qt6 包名在本机带 **t64** 后缀（`libqt6widgets6t64` 等），这是 Ubuntu 24.04 的时间戳 ABI 转换。手写 deb 依赖会踩坑，所以本项目用 `dpkg-shlibdeps` 自动生成。

## 3. 目录结构

```
notepad-plus-plus-linux/
├── npp-linux/                 ← 移植本体（唯一需要改的地方）
│   ├── CMakeLists.txt         ← 版本 1.0.8、astyle、CPack、可选测试目标
│   ├── src/
│   │   ├── main.cpp           启动、配置定位与失败诊断、图标、desktopFile 名
│   │   ├── MainWindow.cpp/h   主窗口、菜单/工具栏、拖放、格式化、保存失败提示
│   │   ├── EditorView.cpp     对 Scintilla 的封装（列选区、编码 IO、折叠、格式化写回）
│   │   ├── TabManager.cpp     多标签页
│   │   ├── DocumentBuffer.cpp 文档缓冲与磁盘状态
│   │   ├── StyleManager.cpp   解析 XML、路径探测、载入报告与诊断文本
│   │   ├── SessionManager.cpp session.xml 与 backup/
│   │   ├── dialogs/           查找替换、列编辑器、首选项、快捷键映射器、样式配置器等
│   │   │   ├── StyleOverride.cpp/h     样式覆盖序列化与旧格式兼容（纯逻辑）
│   │   │   └── ShortcutManager.cpp/h   QAction 收集、冲突检测、持久化（纯逻辑）
│   │   ├── utils/             CodeFormatter/JsonFormatter、ThemeManager、EncodingConverter 等
│   │   ├── widgets/FolderTreePane  左侧目录树停靠窗（QFileSystemModel + QTreeView，惰性）
│   │   └── widgets/TitleBar   无边框自绘标题栏
│   ├── third_party/astyle/    AStyle 格式化引擎（vendored，MIT，不要改）
│   ├── tests/                 Qt Test 回归套件、test_main.cpp、run_tests.sh
│   ├── resources/             app.qrc、toolbar.qrc、icons/、deb/{postinst,postrm}、*.desktop.in
│   ├── tools/                 gen_icons.py、gen_toolbar_icons.py、release.sh
│   ├── build.sh               日常构建 → build/
│   ├── build_deb.sh           打 deb   → build-deb/
│   └── README.md              ★ 功能与设计的权威文档，改动后请同步更新
├── docs/                      开发计划2.md（TDD 执行记录）、文件夹树技术评估.md（选型证据）
├── scintilla/ lexilla/        vendored 上游引擎（不要改）
└── PowerEditor/src/           vendored：uchardet、pugixml、langs/stylers.model.xml
```

`CMakeLists.txt` 用 `NPP_ROOT = ..` 定位依赖，**这个层级不能动**，否则构建直接失败。

`APP_SOURCES` 去掉 `main.cpp` 后编成 `OBJECT` 库 `npp_core`，主程序与 `npp_tests` 复用同一批对象文件。
`npp_core` 含 `CodeFormatter.cpp` / `JsonFormatter.cpp`，二者引用 AStyle，所以两个可执行目标都要显式链接 `astyle`。
`NPP_BUILD_TESTS` 默认 OFF；跑过 `tests/run_tests.sh` 后 CMakeCache 会保留 ON，此时 `cmake --build .` 也会构建测试目标，无害。

## 4. 常用命令

```bash
cd /media/wtian/GithubProjects/notepad-plus-plus-linux/npp-linux

./build.sh                      # 日常构建 → build/notepad-plus-plus-linux
./tests/run_tests.sh            # TDD：构建 npp_tests 并跑 ctest（见 §10）
./build_deb.sh                  # 打 deb → build-deb/*.deb

# 发版（需要 GITEE_TOKEN，projects 权限）
cd .. && ./npp-linux/tools/release.sh v1.0.9
./npp-linux/tools/release.sh v1.0.9 --dry-run    # 只校验+构建，不推送不上传

# 免 root 把图标/desktop 注册到当前用户（开发时临时用）
./install_desktop.sh
```

发版硬规则：**deb 版本号来自 `CMakeLists.txt` 的 `project(VERSION)`，不是 tag**。发版前先改版本号并提交，`release.sh` 会拦截不一致。

## 5. 已实现功能（别重复造）

多标签页（新建/打开/保存/另存为/未保存提示）、Scintilla 原生编辑与撤销、行号与折叠、当前行高亮、编码自动识别（uchardet）、换行符识别与显示、查找替换（正则/全字/大小写/全部替换）、外部文件修改监控（inotify）、**列选模式与列编辑器**、打印、工具栏（可持续选中态）、深浅两套主题、无边框自绘标题栏、透明抠图应用图标、会话记忆与未保存内容自动备份、deb 打包、Gitee Release 发版。

v1.0.7 / v1.0.8 已提交：拖拽文件到窗口打开、透明抠图图标重做、**代码格式化**（AStyle + JSON，工作线程执行，含 vendored `third_party/astyle`）。

本轮本地补齐（尚未提交）：**拷贝另存为按真实编码转码**；**保存失败事务化（`QSaveFile`）并显式提示**；**配置载入失败显式诊断**；**样式配置器支持多槽位与字体族/字号**；**快捷键映射器可编辑、自动与真实 QAction 一致、含冲突检测**；**视图菜单补齐「展开层次 1~8」与幂等的折叠/展开当前块**；**标签页右键菜单（复刻上游 TabContextMenu：批量关闭、打开所在文件夹、移到回收站、只读、复制路径、移动标签等）**；**目录树自绘展开箭头/层级连接线、整行悬停底色、单击文件夹展开、Dock 关闭按钮 lucide 化**；**首选项树形重构（官方主题、中文/英文界面语言、复刻上游 TabBar 全部配置、文档词补全、选中词/括号高亮、UI 字体）+ 按差异应用/不重复写盘**；**首选项严肃化（清除界面里的上游说明/实现说明/英文括注、删除无后端的占位配置项、修复主界面中英文字体生效、括号高亮真正可见）**；**TypeScript / TSX 语法高亮修复（补 cpp 词法器映射与模板字符串属性、CreateLexer 失败显式诊断）**；**五处性能优化（选词高亮视口优先 + 5ms 熔断、字体清单单例、主题元信息缓存、中文本地化旁路、TabBar 批量 polish）**；Qt Test 回归套件。

模块 F（打开文件夹，同样尚未提交）：文件菜单 / 工具栏「打开文件夹」→ 左侧 `QDockWidget` 目录树 → 点击文件复用 `openFile()` 在主区域打开；`QFileSystemModel` 惰性加载、零递归预扫描、图标按后缀缓存、深浅两套 QSS。**未引入 `npp-explorer-plugin` 任何代码**（评估见 `docs/文件夹树技术评估.md`）。

模块 L（URL 可点击链接，尚未提交）：移植上游 `isUrl` 系列（去掉 Win32 `InternetCrackUrl`），可见区域扫描 + `kUrlIndicator`（`INDIC_PLAIN` 下划线 + `SC_INDICFLAG_VALUEFORE`），Ctrl+左键命中即 `QDesktopServices::openUrl`；首选项「高亮匹配」页新增开关，默认开启。

模块 N（HexView 复检与性能改造，尚未提交）：修掉模块 M 的两处硬伤——改为读取**磁盘原始字节**（`setFile` + `mmap`，未保存文档回退内存），`paintEvent` 只对可见窗口单次区间读取（零整份拷贝），绘制改整行一次 `drawText`，地址宽度按文件大小 8/16 位自适应。另：二进制文件（含 NUL）打开后**自动进入**十六进制视图（`isBinary` 由死代码变为有效信号）；配色改由 `ThemeManager::hexViewColors()` 统一提供、跟随深/浅主题；光标只高亮当前焦点所在的 HEX/ASCII 一侧；修复「比例界面字体导致列栅格漂移、点击/高亮不准」——HexView 用 `_monoFont` 统一测量与绘制，并用控件级样式表挡住全局 QSS 的字体覆盖。只读查看；文件未保存改动需先保存才反映到十六进制视图。

模块 P（文件比对，尚未提交）：原实现存在 O(D²) 内存、抢占 TabManager 页面、行内差异只算前后缀等问题；已整改为 Histogram 锚点 + 有界 Myers、字符级行内 diff、`currentTextView()` 零拷贝、独立只读副本比对、16MB 护栏，并新增忽略空白/大小写选项。详见 `docs/开发计划2.md` 模块 P.6。

模块 P.7-DROP（工具栏下拉 + 选择文件比对，尚未提交）：先调研上游 `BTNS_DROPDOWN` / `TBN_DROPDOWN` → `notifyTBShowMenu()` / `createMenuFromMenu()`（证据见 `docs/开发计划2.md` P.7.9）。Qt 侧用自管 `QToolButton` + `MenuButtonPopup`（`QAction::setMenu()` 不会回传给 QToolBar 已创建的按钮，已用红灯证实），下拉项复用主菜单 `QAction` 让勾选态天然同步。「文件对比」下拉含与 Git HEAD 比对 / 与 SVN BASE 比对 / **选择文件比对**（新增：Qt 内建文件选择器 + 独立 `EditorView` 编码探测，当前文档在左、所选在右，16MB 护栏），视图子菜单同步新增该项；「显示所有字符」下拉复刻上游，含空白/换行/缩进参考线 + 分隔符 + 总开关。新增 2 个用例并补主体点击回归；双语文档同步。详见 `docs/开发计划2.md` P.7.9。

模块 P.7-MENU（比对菜单收敛与工具条按钮，尚未提交）：「视图」下新增子菜单「文件比对」，收纳 5 个入口（文件对比 / 比对选区 / Git HEAD / SVN BASE / 关闭对比）；`F8`、`Shift+F8` 改为窗口级快捷动作，不再占菜单。`CompareWidget` 的 optionsBar 右侧加入「下一个差异」「上一个差异」「关闭对比」三个按钮，随比对视图创建/销毁，故只在比对模式出现；通过新信号 `closeCompareRequested()` 通知 MainWindow 退出，无反向依赖。新增 `compareMenuIsGroupedAndToolbarHasNavigationButtons` 用例；双语文档同步。详见 `docs/开发计划2.md` P.7.8。

模块 P.7-UI（工具栏比对按钮开关化，尚未提交）：「文件对比」工具栏按钮改为 checkable（深浅主题已有 `:checked` 紫色高亮），点击进入呈选中态、再次点击退出；`Ctrl+Alt+C` 同步为切换语义，`Ctrl+Alt+X` 仍可退出。状态同步集中在 `createCompareView()` / `onCloseCompare()`，菜单、选区、Git/SVN 等所有入口一致，被护栏拒绝时回滚未选中。tooltip 补齐英文词典。新增 `toolbarCompareButtonTogglesCompareMode` 用例；双语文档同步。详见 `docs/开发计划2.md` P.7.7。

模块 P.7-CRASH（比对模式约 10 秒崩溃修复，尚未提交）：内核日志定位到 `libQt6Widgets +0x210bf4`（`QTabBar::count()`），gdb 复现栈为 `syncBackups()` ← `QTimer::timeout`（备份周期 7 秒）。根因是进入比对时 `setCentralWidget` 把旧的 `_tabManager` 排队删除，定时器随后访问悬空指针。已新增 `switchCentralWidget()` 用 `takeCentralWidget()` 取回所有权，并加 `QPointer` 哨兵保护定时器；新增 4 个生命周期回归用例（修复前红灯、修复后绿灯）。真实 APP + gdb 进入比对存活 26 秒无 SIGSEGV。详见 `docs/开发计划2.md` P.7.6。

模块 P.7-CI（GitHub Actions 工作流失败修复，尚未提交）：`Build & Release` 报 `npp_tests` Failed / CTest exit 8，根因是 `langs.model.xml` / `stylers.model.xml` 原先只挂在 `noda` 的 `POST_BUILD` 复制，而 CI 的 `run_tests.sh` 只构建 `npp_tests`，干净构建目录缺这两个 XML，导致 8 个依赖 `loadRealConfigs()` 的用例失败（本地因曾构建过 `noda` 被掩盖）。已改为 `configure_file(... COPYONLY)` 在 configure 阶段落地，并在 `run_tests.sh` 增加 4 类资源前置校验；顺带把 `project(NotepadPlusPlus_Linux)` 改名为 `project(Noda)`。验证：干净构建目录 + CI 环境变量 + 仅构建 `npp_tests` → CTest 100%；`./tests/run_tests.sh` exit 0；`./build_deb.sh` 正常产出 deb。详见 `docs/开发计划2.md` P.7.5。

模块 P.7（所有未落地项收口，尚未提交）：wyhash 风格块哈希、Moved 块检测（含开关）、选区比对、`QProcess` 版 Git HEAD / SVN BASE 比对（零链接依赖）、`QtConcurrent` 异步比对 + 「正在比对…」状态、按 alignment 文档行映射的垂直滚动同步（消除 annotation 占位漂移）、`DiffMiniMap` 差异分布小地图；改名收尾：启动时一次性迁移 `~/.config/Notepad++` → `~/.config/Noda`（幂等、不覆盖）、hicolor 启动器图标同步到新手绘图案；文档双语收口：`npp-linux/README.md` 改为英文默认技术文档，完整中文版迁至 `docs/README.technical.zh-CN.md`，两版落地页修正旧二进制名并补文档导航。详见 `docs/开发计划2.md` 模块 P.7。

模块 O（英文界面本地化补全，尚未提交）：修复英文模式下菜单/工具栏 tooltip/设置项/状态栏残留中文。做法：语言下拉改用原生名称（简体中文 / English）并标记 `nppNoL10n` 不翻译；`extraDictionary` 增补约 100 条（工具栏/菜单动作、折叠与格式层次、编码子菜单、查找替换、状态栏模板、URL 链接）；新增 `translateTooltip()` 处理「名称 (Ctrl+X)」形式；状态栏改为运行期按语言拼接并加 `languageChanged` 信号刷新。验收测试 `tests/test_localization.cpp` 在主窗口与设置对话框上断言 0 处中文。

细节与设计意图见完整中文技术文档 `docs/README.technical.zh-CN.md`（每节都写了「为什么这么做」），英文技术文档为 `npp-linux/README.md`。

## 6. 源码里值得先读的几处

- `StyleManager::locateConfigFiles()`：按 `applicationDirPath()` 相对探测两个 XML 并做词法规范化，失败时 `buildDiagnostic()` 给出原因、路径清单与后果。
- `EditorView::writeEncodedFile()`：`QSaveFile` + BOM + 分块转码 + `commit()`；任何失败 `cancelWriting()` 并把原因写进 `lastSaveError()`，`saveToFile()` 绝不 `SETSAVEPOINT`。
- `MainWindow::addToggleTool()`：QAction 可勾选 + `toggled`↔`setChecked` 双向同步 + `triggered` 绑定。
- `MainWindow::forEachEditor()`：视图开关必须作用于**每个**标签页，不能只改当前那个。
- `MainWindow::reloadStyleOverrides()`：全局字体先于语言/槽位字体叠加，顺序反了会被 `setEditorFont()` 冲掉。
- `StyleOverride::deserialize()` / `ShortcutManager::collectActions()`：把易错逻辑从 QWidget 抽出来，测试直接打纯逻辑。
- `FolderTreePane::setRootPath()`：目录树惰性契约的唯一入口；不要在它周围加递归扫描或全量索引，
  `TestFolderTree::doesNotRecursivelyPrefetchDeepDirectories` 会把任何预扫描变成红灯。
- `EditorView::restoreColumnSelections()`：列选区改写后按角点重建。
- `ThemeManager`：两套完整 QSS（dark/light），改样式必须两套都改。

## 7. 已知陷阱（改代码前必读，全是踩过的）

**Qt / QSS**

1. QSS 一旦接管某个部件，**不再回退原生外观**。
   - `QTabBar::close-button` 只要有样式规则，就必须显式给 `image: url(...)`，否则 X 整块空白。
   - `QToolButton` 只要有样式规则，就必须写 `:checked`，否则开关按钮开着关着看起来一样。
2. `QAction::setChecked()` **只发 `toggled`，不发 `triggered`**。渲染若挂在 `triggered` 上，代码里的 setChecked 不会触发它——「显示所有字符」这个合体开关就是这么断链的，必须自己把渲染做一遍。
3. `QMainWindow::setMenuWidget()` 之后，`menuBar()->actions()` 返回空，找菜单要用 `findChildren<QMenu*>()`。
4. `widget->grab()` 的渲染可能比逻辑状态**滞后一整批**。判断视觉差异要加 `QEventLoop + QTimer::singleShot(200, ...)` 等渲染稳定，并用像素统计而不是肉眼看缩略图。
5. `QMenu::addAction()` 不改变 QAction 的 parent；分类与反查要靠 `menu->actions()` 建映射。工具栏里还有一份同名镜像 QAction，`ShortcutManager` 必须过滤，否则同一快捷键被两个 QAction 抢注。

**Scintilla**

6. 列选区内部表示是**每行一个 selection**，且**索引 0 是主选区，不是最上面那一行**。改写后重建角点必须按 `selLpos`/`selRpos` 取极值，不能用 `front()/back()`。
7. `SCI_GETCOLUMN`/`FINDCOLUMN` 的 column 按**字符**计，position 按**字节**计。混合 UTF-8/CJK 时全程在字节层拼接才不错位。
8. `setEditorFont()` 会重置全部 128 个样式槽位的字体；样式配置器的槽位字体必须在它**之后**重新叠加。

**编码 / 保存**

9. `iconv_open` 失败时绝不能把内部 UTF-8 原始字节当目标编码写出去。`EncodingConverter` 现在硬失败且不产出字节；上层用 `QSaveFile` 保证原文件不被截断。

**探针与验证**

10. offscreen 探针（`QT_QPA_PLATFORM=offscreen`）直接实例化真实窗口类来验证 UI 逻辑，非常有效。但要注意：
    - 复用 CMake 目标文件链接探针：`find . -name "*.o" | grep notepad-plus-plus-linux.dir`；
    - 管道下必须 `stdbuf -o0`，否则看不到输出；
    - **调用真实槽函数会触发 `savePreferences()`，写坏 `~/.config/Notepad++/Notepad++.conf`**；测试进程必须隔离 `XDG_CONFIG_HOME`；
    - 用 `QLatin1String(中文)` 与 QString 比较**恒为 false**，会让 helper 返回 nullptr 再段错误。
11. `DeleteFile` 工具对 ELF 可执行文件无效（报成功但没删），删二进制用精确路径的 `rm -f`。

**打包 / CPack**

12. `CPACK_RESOURCE_FILE_LICENSE` 在 DEB 生成器下**不会**落盘 `/usr/share/doc/<pkg>/copyright`，必须显式 `install(FILES ... RENAME copyright)`。
13. CPack **不生成** debhelper 那样的 dpkg 触发器，装完图标缓存不刷新——靠 `postinst`/`postrm` 手动跑 `gtk-update-icon-cache` 与 `update-desktop-database`。
14. `.desktop` 的 `Exec` 在 configure 阶段就被固化成绝对路径，CPack 又复用 CMakeCache。**必须** `-DCMAKE_INSTALL_PREFIX=/usr`，否则打出 `Exec=/usr/local/bin/...` 而文件在 `/usr/bin`，装完点图标没反应。`build_deb.sh` 每次清空构建目录来规避。

**目录树 / 停靠窗**

- `QFileSystemModel::setRootPath()` 只填充根一层，靠 `canFetchMore/fetchMore` 惰性展开；任何
  `QDirIterator` 递归都会让 `TestFolderTree` 的惰性契约变红。
- `QDockWidget::visibilityChanged` 与视图菜单 `QAction::toggled` 双向同步必须用 `QSignalBlocker`
  阻断回环；`QAction::setChecked()` 只发 `toggled`，不发 `triggered`（见第 2 条）。
- `QFileSystemModel` 不接管 `iconProvider` 的所有权：`FolderTreePane` 析构顺序必须是
  `view->setModel(nullptr)` → 删 model → 删 provider，否则留下悬空指针。

## 8. 未决事项

| 事项 | 说明 |
|---|---|
| **仓库可见性** | Gitee 仓库当前是**私有**，Release 附件对外不可见。若要公开下载需改仓库可见性。 |
| **能力缺口** | 插件系统、宏、函数列表、分栏/双视图、UDL、远程文件、「仅打印选区」、打印语法高亮颜色仍未实现（设计上明确排除，见 README）。 |
| **无 AGENTS.md** | 本仓库刻意没放。若想约束提交信息格式等，需要单独加一份。 |

HiDPI 标签关闭按钮已由 `ThemeManager::closeButtonImage()` 按 DPR 选 `x_16/x_32` 实现，旧版交接文档把它列为「未实施」已失效，此处移除。

## 9. 与上游的关系

`scintilla/`、`lexilla/`、`PowerEditor/src/{uchardet,pugixml}`、`npp-linux/third_party/astyle/` 都是上游 vendored 副本，**不要就地修改**。要升级引擎版本，从只读参考目录或上游仓库取新版本覆盖。

许可：`npp-linux/` 沿用 Notepad++ 的 GPL-3.0；Scintilla/Lexilla 为 Neil Hodgson 宽松许可；uchardet 为 MPL 1.1/GPL 2.0/LGPL 2.1；pugixml 为 MIT；AStyle 为 MIT。详见根目录 `LICENSE` 与各子目录的许可文件。

## 10. 回归测试（TDD）

```bash
cd npp-linux
./tests/run_tests.sh
```

- 框架：Qt6 Test，单可执行文件 `npp_tests`，`tests/test_main.cpp` 用 `QTest::qExec` 依次跑各测试类；`enable_testing()` + CTest 注册。
- 隔离：`QT_QPA_PLATFORM=offscreen`；`XDG_CONFIG_HOME` 指向 `build/test-config` 并在每次运行前清空，**测试不会碰真实用户配置**。
- 只构建 `npp_tests` 目标，日常迭代不触发主程序 LTO 链接；模块收尾时再 `cmake --build build --target notepad-plus-plus-linux` 全量验证。
- `TestV108Baseline` 专门锁住 v1.0.7 / v1.0.8 的格式化与拖放能力，任何改造都不能让它变红。
- 覆盖：编码转换与失败语义、保存/拷贝另存为事务性、保存失败提示、配置定位与载入报告、样式覆盖序列化与旧格式兼容、样式配置器 UI、快捷键收集/冲突/持久化、折叠幂等与菜单接线、目录树惰性加载契约（1500 文件深层零预扫描）、文件激活与主窗口集成、主题与工具栏图标资源、首选项左右分栏与扩展配置项、URL超链接探测与点击、十六进制视图自绘与二进制自动识别、桌面关联MIME、零拷贝极致高性能Diff比对引擎与双栏Annotation占位对齐联动。
- 当前 18 个测试类共 118 项；`TestV108Baseline` 始终锁住基线能力。
- 开发流程：先写测试跑出红灯，再写实现跑到绿灯；绿灯前不进入下一模块。执行记录见 `docs/开发计划2.md`。
