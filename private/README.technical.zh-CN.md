<p align="center">
  <img src="../npp-linux/resources/icons/app_256.png" width="88" alt="Noda LOGO">
</p>

> **Noda 技术文档 · 中文完整版** · English (technical): [../npp-linux/README.md](../npp-linux/README.md) · 产品首页：[../README.md](../README.md) / [README.zh-CN.md](README.zh-CN.md)
>
> Noda 是基于 Notepad++ 源码的独立修改（GPL-3.0），与 Notepad++ 及其作者无隶属或背书关系。

# Noda 技术文档（Qt6 / Widgets 原生）

基于 Notepad++ 核心资产与逻辑改造的 Linux 原生高性能实现。

## 架构特点与性能保证

1. **零性能损失**：
   - 彻底剔除 Windows Win32 API 与 GDI 历史依赖。
   - 采用纯 C++20 + **Qt Widgets** 原生架构（非 QML/Webview），启动耗时约 80-120ms，内存占用约 35-45MB。
   - 编辑核心采用 Scintilla 原生 Qt 接口（`ScintillaEditBase`），保持超大文件极端流畅的文本吞吐能力。
2. **核心资产完全复用**：
   - 语法高亮与主题：通过 `pugixml` 原生解析原版的 `langs.model.xml` 与 `stylers.model.xml`。
   - 智能字符集编码识别：直接复用 `PowerEditor/src/uchardet/`（Mozilla 算法），自动识别 UTF-8 / GBK / Big5 等。
   - 全链路统一采用 UTF-8 内存流，彻底消除了 Windows 版在 UTF-16 (`wchar_t`) 与 UTF-8 之间的频繁拷贝开销。
3. **已实现基础功能**：
   - [x] 多标签页管理（新建、打开、保存、另存为、未保存修改提示；双击标签栏空白处也可新建文档）
  - [x] 标签页右键菜单（关闭/批量关闭、保存、另存为、打开所在文件夹、重命名、移到回收站、重新载入、打印、只读、复制路径、移动标签；右键先选中被点的标签）
   - [x] 鼠标拖入本地文件打开（支持多文件；编辑区走 Scintilla `uriDropped`，其余窗口区域由主窗口接收）
   - [x] Scintilla 纯原生文本编辑、撤销/重做、剪切/复制/粘贴、全选
   - [x] 行号与折叠列、当前行高亮
   - [x] 自动换行切换
   - [x] 智能文件编码检测与显示（UTF-8, GBK 等）
   - [x] 跨平台换行符自动识别与状态栏显示（Unix LF / Windows CRLF / Mac CR）
   - [x] 毫秒级查找与替换（区分大小写、全字匹配、正则支持、全部替换）
   - [x] 磁盘文件外部修改实时监控与重载提醒（Linux `inotify` 驱动）
   - [x] 会话记忆与未保存内容自动备份（关闭后重开可原样恢复，见下节）
   - [x] 代码格式化：C/C++/C#/Java/Objective-C 走 AStyle，JSON 走保序流式美化器，耗时计算放工作线程
   - [x] 语言样式配置器：按「语言 × 样式槽位」编辑颜色、粗斜体、字体族与字号并持久化
  - [x] 首选项（树形分栏）：主题切换、中文/英文界面、标签栏行为与外观、文档词/关键字自动补全、选中词与括号高亮、主界面中英文字体、编辑器行为配置（只列真实可用的功能，无占位开关）
   - [x] 快捷键映射器：表格由真实菜单动作生成，可编辑、有冲突检测、可恢复默认
   - [x] 保存/拷贝另存为的事务化写入与失败显式提示（不会静默写错编码）
   - [x] 语法配置加载诊断：缺文件或解析失败时明确报告原因、搜索路径与后果
   - [x] 打开文件夹：左侧停靠窗展示目录树，自绘展开箭头与层级连接线，单击文件夹即展开/收起、单击文件在主区域打开（惰性加载、零全量预扫描）
   - [x] TypeScript / TSX 语法高亮：按扩展名自动识别，复用 cpp 词法器与主题样式槽位，支持模板字符串

## 编译环境依赖

在 Ubuntu / Debian 系统上安装必要依赖：

```bash
sudo apt update
sudo apt install -y build-essential cmake qt6-base-dev qt6-base-dev-tools
```

可选：`libxcb1-dev`。装了它会启用 X11 启动通知补漏（把 `_NET_STARTUP_ID` 补写到主窗口上、
并向启动器广播启动序列结束，见下文“从任务栏点开时一直转圈”）；没装也只是这一项不生效，
构建本身不会失败。

*(如果使用 Qt5 环境，可安装：`sudo apt install -y build-essential cmake qtbase5-dev qtbase5-dev-tools`)*

## 一键编译与运行

```bash
cd npp-linux
./build.sh

# 运行
./build/noda
```

## 打包成 deb

```bash
./build_deb.sh

# 安装（用 apt 而不是 dpkg -i，缺依赖时会自动补齐）
sudo apt install ./build-deb/noda_1.0.8-1_amd64.deb
```

产物为 `build-deb/noda_<版本>-1_<架构>.deb`（约 2.7 MB），按 Debian 政策装到标准位置：

| 路径 | 内容 |
|---|---|
| `/usr/bin/noda` | 可执行文件 |
| `/usr/share/noda/` | `langs.model.xml`、`stylers.model.xml` |
| `/usr/share/applications/*.desktop` | 桌面入口，应用列表可直接搜到 |
| `/usr/share/icons/hicolor/*/apps/` | 13 种尺寸图标 |
| `/usr/share/doc/noda/copyright` | 许可声明 |

卸载：`sudo apt remove noda`。

几个打包细节（改 `CMakeLists.txt` 的 CPack 段前值得先看）：

- **Depends 不手写**：由 `dpkg-shlibdeps` 按实际链接关系生成，所以在 Ubuntu 24.04 上拿到的是
  `libqt6core6t64` 这类带 `t64` 后缀的包名，换发行版也不会写错。
- **`Recommends: qt6-wayland`**：X11 的 `libqxcb.so` 就在 `libqt6gui6t64` 里，Wayland 会话用的
  `libqwayland-*.so` 则单独放在 `qt6-wayland`，缺了它 Qt 只能退回 XWayland。
- **必须用 `-DCMAKE_INSTALL_PREFIX=/usr`**：`.desktop` 里的 `Exec` 在 configure 阶段就被固化成
  `${CMAKE_INSTALL_FULL_BINDIR}` 的绝对路径，沿用 CMake 默认的 `/usr/local` 会打出
  `Exec=/usr/local/bin/...` 而文件实际装在 `/usr/bin`，装完点图标没反应。`build_deb.sh` 已处理。
- **XML 装在 `share` 而不是 `bin`**：`main.cpp` 按 `applicationDirPath()/../share/noda`
  相对推导，所以 `/usr`、`/usr/local`、甚至把 deb 解包到临时目录后直接运行都能命中，不依赖编译期绝对路径。

### 「打开方式」里看不到本应用？

装完 deb 却在右键菜单里找不到，通常不是包坏了。GNOME 的候选分两层，职责完全不同：

- **「用其他应用打开…」完整对话框**读 `mimeinfo.cache` 的*已注册*列表，由 `postinst` 里的
  `update-desktop-database` 生成，内容取决于 `.desktop` 的 `MimeType`。
- **右键「打开方式」子菜单**只列*推荐*列表，来源是 `mimeapps.list` 的 `[Added Associations]`。
  `MimeType` 写得再多也进不了这一层——gedit、Obsidian 等已经声明过该类型时，本应用就被挤出去，
  表现成「装是装了，但菜单里没有」。

子菜单这一层只能改用户配置，包无法代劳：`mimeapps.list` 属于用户/管理员策略，Debian 也不允许
包去覆盖 `/usr/share/applications/mimeapps.list`（那是 `mime-support` 的公共文件）。用附带脚本补上：

```bash
cd npp-linux
tools/register_open_with.py --dry-run   # 先看会动哪些类型，不落盘
tools/register_open_with.py             # 给所有继承自 text/plain 的类型加关联，自动备份
tools/register_open_with.py --remove    # 撤销
```

脚本扫 `/usr/share/mime/types` 后按 `Gio.content_type_is_a(t, "text/plain")` 筛选，所以
`application/json`、`application/x-shellscript`、`application/toml` 这类非 `text/` 前缀的文本格式
也会被覆盖；`application/octet-stream` 之类的二进制则不会被误加。

`.desktop` 的 `Exec` 用 `%F`（本地文件列表）而不是 `%U`：`main.cpp` 只认本地路径，`%U` 传进来的是
`file:///...`，`QFileInfo` 判定不存在，文件会被静默丢弃。缺少字段码时 GIO 报告
`supports_files=False`，部分应用选择器会据此隐藏该应用。

### 从任务栏点开时一直转圈？

只在 **X11** 会话下出现：窗口 300ms 就出来了，但指针移到任务栏上就变成转圈的忙光标，
要等 **15.6 秒**才停；这期间关掉应用再点图标也开不起来，得等转圈结束。这跟启动速度无关，
是**启动序列（startup notification）没有被结束**：15.6 秒正是序列的超时值。

启动应用时桌面环境会在子进程环境里放 `DESKTOP_STARTUP_ID`，并建立一条序列。转圈的光标
和“正在启动”状态属于**启动器**（这里是 gnome-shell，它链了 libstartup-notification），
不属于窗口管理器；只有应用侧把“这次启动好了”通知回去，序列才会结束。

应用侧有两个可写的地方，`src/utils/StartupNotify.*` 两个都补了（先后顺序都要对）：

1. `main()` 在构造 QApplication **之前**把 `DESKTOP_STARTUP_ID` 读走（Qt 会在构造时把它
   消费掉，事后 `qgetenv` 已是空串），再在 `show()` **之前**把它写进主窗口的
   `_NET_STARTUP_ID` 属性（`winId()` 只建窗口不映射，正好满足“属性先于映射就位”）。Qt6
   只把这个属性写在自己那个 SM client leader 隐藏窗口上（20x20、未映射），主窗口上一个字
   都没有。**但本机实测光把属性写对并不结束序列**，转圈照样走满 15.6 秒 —— 这一条补的是
   规范要求的状态，不是转圈的开关。
2. `show()` 之后向 root 广播 `_NET_STARTUP_INFO_BEGIN`/`_NET_STARTUP_INFO` 的 format 8
   ClientMessage，载荷 `remove: ID="<序列id>"`（每 20 字节拆一条）—— 这才是结束序列的那一条。

第 2 条里有两个坑，都是拿 gedit（同机实测 207ms 就不转圈）当对照、逐字节抓线上行为抓出来的
（取证工具按项目约定放在不被 git 跟踪的 `npp-linux/build/` 下，不入库：`snwatch.c` 在 root 上
选 `PropertyChangeMask|SubstructureNotifyMask` 收启动通知消息，`xtrace.c`/`xcbtrace.c` 用
`LD_PRELOAD` 分别拦 Xlib 与 libxcb 的发送）：

* **id 必须带引号。** Qt 其实自己发过一条 `remove`，但写成不带引号的 `ID=<id>`；id 形如
  `gnome-shell/<Name>/...`（中间那段取自 .desktop 的 `Name`）。历史上 `Name` 含空格时解析器
  读到空格就截断，这条通知于是指向一个不存在的序列，等于没发。当前 `Name=Noda` 无空格，
  但仍保留引号写法以兼容用户自定义名称。GTK 是带引号发的，所以没事。
* **发完必须强制一次往返再断开连接。** 只 `xcb_flush()` 然后 `xcb_disconnect()` 的话，这几笔
  `send_event` 实测根本没到达服务器（同一个二进制，加上一次往返立刻就能在 root 上收到）。

生效的判据 —— 点图标时 `snwatch` 在 root 上录到的四步：

```
    1 ... src_win=0x6013e5  "new: ID=gnome-shell/..."        <- shell 建序列
  359 ... src_win=0x20b     "remove: ID=gnome-she..."        <- Qt 那条，没引号，没人认
  359 ... src_win=0x7400006 "remove: ID=\"gnome-sh..."       <- 我们这条（带引号）
  391 ... src_win=0x6013f1  "remove: ID=gnome-she..."        <- shell 自己广播 = 序列已结束
```

改完实测忙光标只持续 350ms 左右（改前 15600ms），转圈期间关应用再点图标也能立刻开起来。

另一个必要的配套是桌面文件里的 `StartupWMClass=Noda`：窗口的
`WM_CLASS` 是 `"noda", "Noda"`，第二段来自
`main.cpp` 的 `setApplicationName("Noda")`，与桌面文件名一致；GNOME 用
`StartupWMClass` 来决定窗口归到哪个应用，这关系到图标合并与分组。改完必须重新安装
桌面项才生效（用户级 `./install_desktop.sh` 优先级高于包里的 `/usr/share`）。

Wayland 会话下既没有 `_NET_STARTUP_ID` 也不看 `WM_CLASS`，靠 `app_id`
（由 `setDesktopFileName()` 设定）识别，所以不受影响。

### 弹出菜单的圆角外面又套了一层直角框？

样式表里 `QMenu` 写了 `border-radius: 6px`，但菜单四个角看上去仍被直角包着。

原因：`QMenu` 弹出时是一个**独立的矩形原生窗口**，`border-radius` 只能决定 Qt 在这个
窗口内怎么画，改变不了窗口自身的形状。圆角之外、矩形角落之内的那几像素会被窗口底色
填实，于是成了「圆角套在直角里」。不是又多包了一层控件。

修法：`MainWindow::setupMenus()` 末尾给所有 `QMenu`（实测 53 个，含各级子菜单）打开
`Qt::WA_TranslucentBackground`，让弹出窗口拿到 32 位 ARGB 视觉，没画到的角落就是真透明。

验证：弹出窗口的 Depth 应为 32；且角落像素应该等于它背后的内容而不是菜单自己的底色：

```bash
xwininfo -id <弹出窗口 id> -stats | grep Depth        # 期望 32
```

注意这依赖合成器（GNOME/mutter 常驻合成）。在没有合成器的窗口管理器下，透明角落会
透出脏像素；那种环境下去掉 `QMenu` 的 `border-radius` 比保留它更稳。

## 会话记忆与自动备份

与 Notepad++ 一致：关掉程序再打开，上次打开的标签、光标/选区位置、语言、编码都在；
**没保存的修改也照样回来**，所以退出时不会逐个弹「是否保存」。

机制是两层，缺一不可（对应上游 `PowerEditor/src/ScintillaComponent/Buffer.cpp` 中
`backupCurrentBuffer()` 上方那段 *Specs and Algorithm of session snapshot & periodic backup system*）：

| 位置 | 作用 |
| --- | --- |
| `~/.config/Noda/session.xml` | 记录打开了哪些文件、每个标签的 `startPos/endPos/firstVisibleLine/lang/encoding/eolMode/userReadOnly/backupFilePath`；结构与上游 session.xml 同构 |
| `~/.config/Noda/backup/<标签名>@<YYYY-MM-DD_HHMMSS>` | 未保存内容的快照，统一以 UTF-8 存放 |

工作流程：

1. 定时器每 N 秒（默认 7s）扫一遍脏缓冲区，`<标签名>@<时间戳>` 只在**第一次变脏**时生成，
   之后路径固定、反复覆盖；新建备份后立即写一次 `session.xml`，这样即使进程被 `kill -9`，
   下次启动也能在 `session.xml` 里找到这条备份记录。
2. 文档回到已保存状态（保存、另存、重新载入、外部修改重载）立刻删掉它的备份文件，
   避免用旧内容把已保存的文件盖回去；标签关闭时同样清理。
3. 退出时先把脏内容刷进 `backup/`、再写 `session.xml`，然后直接退——不弹保存框。
4. 启动时读 `session.xml`，有 `backupFilePath` 且文件还在，就用备份内容重建标签并把
   修改标记置为「已修改」；没有备份的按普通文件打开。

几个细节：

- 空白的未命名标签不写入会话（同上游 `isUntitled() && docLength()==0` 跳过），
  否则每次启动都会多出一个空标签。
- 备份内容统一存 UTF-8，恢复时按 `session.xml` 里的 `encoding` 回填缓冲区，
  下次保存再转码写回原文件，不会把 GBK 文件改成 UTF-8。
- 覆盖 `session.xml` 前会先复制一份 `session.xml.inCaseOfCorruption.bak`；解析失败时
  自动回退读这份备份，一次坏写不会让会话永久丢失。
- 恢复出来的内容不进撤销栈（否则一次 Ctrl+Z 就回到空文档），但仍带「已修改」标记，
  保存一次即可清掉。
- 光标、选区、首个可见行和**横向滚动偏移**都会一起还原。还原时不能调 `SCI_SCROLLCARET`：
  它会把光标强行滚进视野，重算横向偏移，长行一重开就只剩后半截（上游同样避开它，
  改用 `SETANCHOR`/`SETCURRENTPOS` + `SETXOFFSET`）。

开关在 **设置 → 首选项 → 会话与备份**：「记住上次打开的会话」控制 `session.xml`，
「定时备份未保存内容」控制 `backup/` 与退出时不弹保存框，两者都可单独关闭。

## 发版（打 tag 并发布到 Gitee Release）

```bash
export GITEE_TOKEN=xxxxxxxx   # Gitee 私人令牌，勾选 projects 权限
tools/release.sh v1.0.0
```

脚本依次完成：对齐版本号 → 打 annotated tag → 全量构建 deb → 推送 tag →
调 Gitee API 建 Release → 上传 deb 附件。Release 说明文字自动取上一个 tag 以来的提交标题。

**版本号不用手动改**：`CMakeLists.txt` 里 `project(... VERSION x.y.z)` 与 tag 不一致时，
脚本会自动改写成 tag 的版本号，并把这个改动单独提交、推送 —— 因为 **deb 的版本号来自这里而不是 tag**，
同步之后才不会再出现「tag 叫 v1.2.0、包却是 1.0.0-1」的事故。打包完成后还会拿
`dpkg-deb -f` 核对一次实际产物版本。

它还会拦住两种容易出错的情况：

- 有未推送的提交 —— 否则 tag 指向的 commit 在远端不存在，Release 挂上去打不开。
- `origin` 不是 Gitee 仓库（在带 GitHub 上游 remote 的源码树里跑会直接拒绝）。

如果工作区还有**其他**未提交改动，脚本只警告不阻断：deb 从工作区构建、会包含它们，
而 tag 指向的源码不含，两者会有差异。

只想检查不发布：`tools/release.sh v1.0.1 --dry-run`（不建 tag、不推送、不上传，仍会构建出 deb）。
dry-run 下自动改写的版本号会留在工作区但不提交，正式发版时再一并提交、推送。

令牌在 <https://gitee.com/profile/personal_access_tokens> 生成，只需 `projects` 权限。

## 应用图标

图标源文件为 `notepad++_cutout.png`（透明抠图），由 `tools/gen_icons.py`（Pillow）
离线生成透明背景图标：保持宽高比居中缩放，不添加绿色背景、超椭圆切角或高光，
图标主体就是纸面与变色龙本身。

- `resources/icons/app_master_1024.png` —— 加工后的母图，所有小尺寸由它缩放而来。
- `resources/icons/app_{16,24,32,48,64,128,256}.png` —— 经 `resources/app.qrc` 内嵌进可执行文件，
  运行时由 `main.cpp` 设为窗口图标，并由 `TitleBar` 以 20px 显示在标题栏最左侧（菜单栏“文件”的左边），
  不依赖系统 QtSvg 开发包。
- `resources/icons/hicolor/{16x16 … 512x512,scalable}/apps/noda.png` —— 系统图标主题用。

重新生成（修改了源图或想调风格时）：

```bash
sudo apt install -y python3-pil
python3 tools/gen_icons.py
```

内容占比等参数集中在 `tools/gen_icons.py` 顶部的常量区。

让 GNOME/KDE 任务栏与应用列表识别图标（写入 `~/.local`，无需 root）：

```bash
./install_desktop.sh                       # 默认使用 build/ 或 /usr/local/bin 下的可执行文件
./install_desktop.sh /opt/npp/noda   # 也可显式指定路径
```

`sudo make install` 时会一并安装 hicolor 图标与 `noda.desktop`。

> 标题栏中间不再绘制文件名。当前打开的文件依靠 `MainWindow::syncWindowTitle()` 写入窗口标题，
> GNOME/KDE 任务栏悬停时读的就是这个标题，因此鼠标移到任务栏上仍能看到完整文件名。

## 工具栏

工具栏为纯图标模式，20 个动作分 6 组（组间用分隔符）：

| 分组 | 动作 |
|---|---|
| 文件 | 新建 · 打开 · 打开文件夹 · 保存 · 全部保存 · 关闭 · 全部关闭 · 打印 |
| 剪贴板 | 剪切 · 复制 · 粘贴 |
| 撤销 | 撤销 · 恢复 |
| 搜索 | 查找 · 替换 |
| 缩放 | 放大 · 缩小 |
| 显示开关 | 自动换行 · 显示所有字符 · 显示缩进参考线 |

最后三个是开关型按钮：它们与“视图”菜单里同名的 checkable 项各自持有 QAction（共用一个
action 会把图标一并带进菜单），靠一对 `toggled` 连接双向同步勾选状态。“显示所有字符”与
Notepad++ 一致，是“空格与制表符”+“换行符”两个开关的合体，后两者任一关掉时它会自动取消勾选。

「文件对比」与「显示所有字符」是**下拉开关按钮**，复刻 Notepad++ 工具栏的
`BTNS_DROPDOWN` + `TBN_DROPDOWN` → `notifyTBShowMenu()` → `createMenuFromMenu()` 机制：
Qt 侧用自管 `QToolButton` + `QToolButton::MenuButtonPopup`，主体点击执行默认动作，右侧箭头弹子菜单。
有个坑：`QToolBar` 在 `addAction()` 时就创建了 `QToolButton`，之后再 `QAction::setMenu()` 不会回传，
因此必须自建按钮并显式 `setMenu()`。下拉项直接复用主菜单的 `QAction`，勾选/启用状态天然同步，
比上游手动复制菜单项状态更可靠。

- 「显示所有字符」下拉：显示空格与制表符 · 显示换行符 · 显示缩进参考线 | 分隔符 | 显示所有字符。
- 「文件对比」下拉：与 Git HEAD 比对 · 与 SVN BASE 比对 · 选择文件比对；主体点击仍是进入/退出比对的开关。

这三个开关是**全局持续状态**，不是“点一下就结束”的一次性动作：切换时写回 `_prefs`、
当场作用到**每一个**已打开的标签页（而不只是当前那个）、并落盘，因此新开标签页会从偏好继承，
重启后保持不变。两个容易踩的点：

- 选中态必须靠 QSS 里的 `QToolButton:checked` 规则显式画出来。样式表一旦接管了 QToolButton
  就不再回退原生外观，缺了 `:checked` 时按钮开着和关着看起来一模一样。
- “显示所有字符”除了同步两个分量开关的勾选，还必须自己把渲染做一遍：`setChecked()` 只发射
  `toggled`、不发射 `triggered`，而渲染逻辑挂在 `triggered` 上，指望分量开关替自己发命令会一个字节都不变。

## 列选模式与列编辑器

对应 Notepad++ “编辑”菜单里的四个条目，本端全部对齐（菜单文案与官方简体中文一致）：

| 菜单项 | 快捷键 | 作用 |
| --- | --- | --- |
| 开始/结束 选取 | Ctrl+Shift+B | 第一次按下记住起点，第二次选中两点之间的普通选区 |
| 开始/结束 按列选取 | Alt+Shift+B | 同上，但第二次按下得到矩形列块（键盘两点定块） |
| 列选模式... | （无） | 弹出说明，列出进入列选的三种方式 |
| 列编辑器... | Alt+C | 在列块每一行插入相同文本，或插入按增量递增的数字 |

进入列选有三条路：按住 Alt 拖动鼠标、Alt+Shift+方向键扩展、以及上面的两点定块。
前两条是 Scintilla 的原生能力（`rectangularSelectionModifier` 默认就是 Alt，
`SCI_*RECTEXTEND` 也在默认键表里），端口只开了 `SCVS_RECTANGULARSELECTION` 让选区可以越过行尾，
没有重复实现；只有第三条需要自己写（`EditorView::beginOrEndSelect`）。

列选相关代码集中在 `EditorView.cpp` 的“列选模式”一节（以及 `EditorView.h` 里同名的公共接口），
算法照搬 Notepad++ `columnReplace`：
一个矩形列块在 Scintilla 内部是“每行一个 selection”，所以先 `collectColumnLines()` 采集每行的
左右字节位置与虚拟空格列数，再按从左到右的顺序逐行 `SETTARGETRANGE`+`REPLACETARGET`，
用 `totalDiff` 累计前面各行造成的位移；越过行尾的部分先把虚拟空格补成真实空格再替换。
改写完成后按原 order 回填选区，若虚拟空格行被“实现”导致选区被收回，则用首尾两行的角点重建矩形。
数字模式支持十/八/十六/二进制、`none`（尾部补空格）/`zeros`/`spaces` 三种开头补齐、
十六进制大小写，以及“同一个数字连续占 N 行”的重复次数；对齐宽度取首末两个值的最大位数。

**没有列选区时**，列编辑器沿用 Notepad++ 的退化行为：从光标所在行一直写到文档结尾，
统一插到光标所在的那一列（短行先补空格对齐）。这个行为会改动大量文本，对话框在这种情况下
会显式提示；整个过程包在一个 undo action 里，Ctrl+Z 一次即可整体撤销。

列与位置的单位不同：`SCI_GETCOLUMN`/`FINDCOLUMN` 里的列按**字符**计（一个汉字算 1 列），
而 position 按**字节**。端口全程在字节层拼接文本，只在需要定位列时做一次转换，
因此 UTF-8/CJK 混排不会错位。

与 Notepad++ 有两处有意偏差：“重复次数”固定按十进制解析（不随所选进制变化，避免“10 进制下填 10
表示重复 2 行”这类反直觉行为），以及数字字段给出 1/1/1 的默认值而不是留空。

## 打印

“文件 > 打印”（Ctrl+P）走系统打印对话框（CUPS），输出为等宽字体的纯文本：左侧行号栏、
制表符按 `tabWidth` 展开到列边界、超出页宽的行直接截断（与 Notepad++ 默认“打印不折行”一致）。
分页与列展开的排版逻辑在 `src/utils/PrintHelper.cpp`，与 `QPrinter` / 对话框解耦，
因此可以用 `QPdfWriter` 离线验证而不产生真实的打印任务。当前不打印语法高亮颜色，也不支持“仅打印选区”。

## 工具栏图标

形态取自 [Lucide](https://lucide.dev)（ISC 许可），
源 SVG 已固化在 `resources/icons/toolbar/src/`，因此生成与构建都不依赖外部仓库或系统图标主题。
例外是 `indent-guide.svg`：Lucide 里没有能准确表达“缩进参考线”的图形（`indent` 是“增加缩进”动作，
`columns-*` 是分栏），它按 Lucide 的笔触规范（stroke-width=2、圆头端点、24x24 网格）自制。

`tools/gen_toolbar_icons.py`（librsvg + cairo）离线生成两套前景色的 PNG：

- `resources/icons/toolbar/{dark,light}/<name>_{16,32}.png`
  —— 16px 用于常规 DPI，32px 以 DPR=2 注册用于 HiDPI；
  `dark`/`light` 分别对应深色（#cfcfcf）与浅色（#4d4d4d）主题。
- `resources/toolbar.qrc` —— 由同一脚本生成的内嵌清单，运行时路径为 `:/toolbar/<theme>/<name>_<size>.png`。

图标由 `src/utils/ToolbarIcon.cpp` 组装成 `QIcon`，`MainWindow::refreshToolBarIcons()`
在主题切换时重建（QSS 改不了 `QIcon` 的颜色，必须换资源）。若资源缺失，
工具栏会自动退回纯文字模式而不是显示空按钮。

另有两处不走 `QIcon`，得自己按屏幕 DPR 取图，否则 HiDPI 屏上会发糊
（`QIcon` 会随窗口 DPR 自动在 16/32 之间择一，QSS 的 `image:` 和裸 `QPixmap` 都不会）：

- 标签右侧的关闭按钮：样式表里写 `image: url(...x_16.png)` 的话，2x 屏上等于把 16px 原图
  拉成 32 物理像素。`ThemeManager::closeButtonImage()` 按 DPR 在两张图里选一张并锁定 16px
  逻辑盒，样式串里以 `@CLOSE_BUTTON_IMAGE@` 占位，`getStyleSheet()` 返回前替换。
- 自绘标题栏最左侧的应用图标：`QLabel::setScaledContents(true)` 会把 pixmap 铺满 20px 逻辑盒，
  所以 `TitleBar` 用 `icon.pixmap(20 * dpr)` 再 `setDevicePixelRatio(dpr)`，而不是 `icon.pixmap(20, 20)`。

改图标形态或新增动作：编辑/添加 `src/` 下的 SVG，然后

```bash
sudo apt install -y python3-gi python3-gi-cairo gir1.2-rsvg-2
python3 tools/gen_toolbar_icons.py    # 同时刷新 PNG 和 resources/toolbar.qrc
cmake -S . -B build && cmake --build build
```

许可：`resources/icons/toolbar/LICENSE-lucide.txt`（ISC，含 Feather 派生声明），
随仓库一并保留。

## 回归测试（TDD）

```bash
cd npp-linux
./tests/run_tests.sh
```

`CMakeLists.txt` 里 `NPP_BUILD_TESTS` 默认 OFF，不影响 `build.sh` / `build_deb.sh` / CPack。
`run_tests.sh` 显式打开它，用 `OBJECT` 库 `npp_core` 复用应用源码的对象文件（不重复编译 22 个 `.cpp`），
只构建 `npp_tests` 目标，避免每次迭代都触发主程序的 LTO 链接。由于 `npp_core` 含
`CodeFormatter.cpp` / `JsonFormatter.cpp`，测试目标也显式链接了 `astyle`。

- 框架：Qt6 Test + CTest，单个可执行文件 `npp_tests`，`tests/test_main.cpp` 依次运行各测试类。
- 隔离：`QT_QPA_PLATFORM=offscreen`；`XDG_CONFIG_HOME` 指向 `build/test-config` 并在每次运行前清空，
  所以 `QSettings` 不会碰到 `~/.config/Noda/Noda.conf`。
- `TestV108Baseline` 专门锁住 v1.0.7 / v1.0.8 的 AStyle / JSON 格式化与拖放能力，任何改造都不能让它变红。
- 覆盖：编码转换与失败语义、保存/拷贝另存为事务性、保存失败提示、配置定位与载入报告、
  样式覆盖序列化与旧格式兼容、样式配置器 UI、快捷键收集/冲突/持久化、折叠幂等与菜单接线、
  目录树惰性加载契约（1500 文件深层目录零预扫描）、文件激活、主窗口集成、主题与工具栏图标资源。

开发顺序是先写测试跑出红灯，再写实现跑到绿灯；各模块记录见 `docs/开发计划2.md`。

## 保存失败与编码一致性

`EditorView::writeEncodedFile()` 是所有写盘路径的唯一入口：

- `QSaveFile` 先写同目录临时文件，`commit()` 时原子替换；转码失败或写盘失败都 `cancelWriting()`，
  原文件不会被截断，也不会留下半个文件。
- `Encoding::*Chunks()` 在 `iconv_open` 失败时**返回 false 且不产出任何字节**。旧实现会把内存 UTF-8
  原样写出去并照样返回「保存成功」，结果是编码错误的文件却被标成已保存。
- `saveToFile()` 严格传播失败：不 `SETSAVEPOINT`、不清修改标记、不改文件路径；
  `lastSaveError()` 给出具体原因（不支持的编码 / 打不开 / 写盘失败 / 提交失败）。
- `saveCopyToFile()` 与保存共用同一条转码链路，但不动文件路径、savepoint、修改标记与文件监控 ——
  「拷贝另存为」的 GBK 文档现在得到的是 GBK 副本，而不是 UTF-8 冒充。
- `MainWindow` 的四个保存入口统一处理失败：发 `saveFailed` 信号 + 带原因的提示框；
  「全部保存」收集所有失败项，循环结束后汇总一次，避免连续弹框打断批量操作。

## 语法配置加载诊断

`StyleManager::locateConfigFiles()` 统一做 6 级路径探测并做词法规范化（诊断文本里不再出现
`bin/../share/...`），`loadConfigs()` 返回 `ConfigLoadReport`：两个 XML 都必须解析成功且结构非空，
`errors` 是致命问题、`warnings` 是可降级的单属性问题（例如 `fgColor` 写了非法十六进制，此时保留
内置默认值而不是把 `-1` 写进 `GlobalStyles`）。

启动失败时 `buildDiagnostic()` 生成完整文本：缺哪个文件、搜过哪些路径、具体解析错误，
以及后果「编辑/查找/保存仍可用，语言菜单与语法高亮不可用」，同时写 stderr 并弹提示框。
状态也可用 `StyleManager::lastReport()` 查询。重载配置会先清空旧状态，不会残留上一次的语言/样式。

## 样式配置器

「设置 > 语言样式配置器」按「语言 × 样式槽位」两维编辑：

- 槽位来自 `stylers.model.xml` 里真实解析出的 `WordsStyle name` / `styleID`，不再只改槽位 0。
- 字体系列、字体大小、粗体、斜体、前景色、背景色全部参与保存，并立即作用到所有打开的文档。
- 覆盖值用 JSON 存在 `styleOverride/<lang>/<styleId>`；旧版 4 字段字符串键 `styleOverride/<lang>`
  仍能被 `StyleOverride::deserialize()` 识别（等价 styleId 0），升级不会丢用户设置。
- `applyPreferencesToEditor()` 里的 `setEditorFont()` 会重置全部 128 个样式槽位的字体，
  所以 `applyPreferences()` 末尾必须重新 `reloadStyleOverrides()`，否则改一次首选项就把槽位字体冲掉了。
- 全局项作用于编辑器整体字体与 `STYLE_DEFAULT`；语言级覆盖在解析顺序的后面叠加，优先级更高。

## 快捷键映射器

「设置 > 快捷键映射器」不再维护第二份硬编码清单：

- `ShortcutManager::collectActions()` 遍历真实的 `QMenu` / `QAction` 树，分类由「哪个菜单的
  `actions()` 包含它」反查（`QMenu::addAction()` 不改变 QAction 的 parent），并过滤工具栏里的镜像动作。
  因此 v1.0.8 新增的「代码格式化」（Ctrl+Shift+F）会自动出现在表里。
- 快捷键列用 `QKeySequenceEdit` 直接编辑；「应用」先做全量冲突检测，有冲突则不落盘并指出冲突的两个命令。
- 覆盖存在 `shortcuts` 组，与默认键一致时不写；`applyTo()` 在 `MainWindow` 构造末尾执行，
  首次调用时（尚未套用任何覆盖）记录默认键，所以「恢复默认」始终能回到代码里注册的键。
- Scintilla 原生鼠标手势（Alt+拖动、Ctrl+点击等）单独列在「操作」组，只读并注明原因。

## 折叠层次菜单

「视图 > 折叠层次」现在同时提供「折叠层次 1~8」与「展开层次 1~8」子菜单，后者接到早已有实现
却一直没有入口的 `EditorView::unfoldLevel()`。「折叠当前层次」/「展开当前层次」分别调用
`foldCurrent()` / `expandCurrent()`，两者都先定位所属折叠头（`SCI_GETFOLDPARENT`）再按目标状态决定是否
`SCI_TOGGLEFOLD`，因此重复点击不会来回反转；旧实现两个菜单项共用 `toggleCurrentFold()`。

## 打开文件夹（左侧目录树）

「文件 > 打开文件夹...」或工具栏的文件夹树按钮选择本地目录后，左侧出现可停靠的目录树；
单击/双击树中的文件即在主区域标签页打开，目录节点只负责展开。
「视图 > 文件夹树」可随时显示 / 隐藏，点停靠窗自己的关闭按钮也会同步菜单勾选状态。

性能设计（这是该功能成立的前提）：

- 数据完全交给 `QFileSystemModel`：`setRootPath()` 只建立当前根目录的监视，子目录内容在用户
  展开时才 `fetchMore`。**没有任何递归遍历、全量索引或 O(N) 常驻内存**；打开一个含十万文件的
  目录与打开空目录的成本相同 —— 都只读根一层。
- 不自建线程、不自建 watcher：复用 `QFileSystemModel` 内建的异步填充与 inotify 监视。
- `FolderTreeIconProvider` 用 `QStyle` 标准图标 + 按后缀的 `QHash` 缓存，避免默认
  `QFileIconProvider` 对每个条目做 MIME / 主题图标查询。
- `QTreeView` 只保留 Name 列（隐藏 Size/Type/Date Modified）、`setUniformRowHeights(true)`，
  大目录滚动时跳过逐行高度计算。
- 回归测试 `doesNotRecursivelyPrefetchDeepDirectories` 在深层目录放 1500 个文件，断言设根后
  该目录 `rowCount() == 0` 且 `canFetchMore() == true`，把「零预扫描」变成可执行的契约。

> 选型依据：评估过 `npp-explorer-plugin`，它是 Win32 专用，且附带全量递归 Quick Open 索引、
> 单工作线程串行与非原子停止标志，不满足本项目的极致高性能目标，因此**未引入其任何代码**。
> 逐条证据见 `docs/文件夹树技术评估.md`。

## 十六进制查看（二进制数据）

「视图 > 以十六进制查看」或 `Ctrl+Alt+H` 在当前标签把文本视图切换为十六进制 / ASCII 并排视图，
再次触发切回文本视图。

行为与性能：

- **原始字节保真**：已保存文件直接读取**磁盘原始字节**（`setFile` + `mmap`），不是 Scintilla
  解码后的 UTF-8 缓冲，因此 UTF-16 / GBK / 含 NUL 的二进制都能如实显示；未保存的新文档回退
  为内存缓冲。
- **零整份拷贝**：文件走 `mmap` 映射，`paintEvent` 只对**可见窗口**做一次区间读取，内存占用与
  文件大小无关，O(可见字节)。
- **大文件**：地址宽度按文件大小自动在 8 位 / 16 位间切换；行数与偏移用 `qint64` 计算并夹取到
  `QScrollBar` 的 `int` 上限。
- **绘制降本**：整行拼接后一次 `drawText`，避免逐字节 `QString` 分配与调用。
- **二进制自动进入**：含 NUL 字节的非 UTF-16 文件打开后自动切到十六进制视图，不必手动按快捷键。
- **主题一致**：配色由 `ThemeManager::hexViewColors()` 统一提供，深/浅主题切换时同步换色。
- **等宽栅格**：绘制与列宽测量共用同一个等宽字体（`painter.setFont(_monoFont)`），并用控件级样式表挡住全局 QSS 的界面字体覆盖，保证「点击/高亮所在字节」与实际显示严格对齐。
- 只读查看：不修改文件；对文件的未保存改动需先保存后才会体现在十六进制视图（该视图反映磁盘内容）。

---

## 主题色（Accent Color）

「首选项 > 外观 > 主题色」提供 7 个常见预设：`默认紫`（保持改造前的 Obsidian 紫）、`macOS 蓝`、
`Ubuntu 橙`、`GNOME 蓝`、`翠绿`、`玫瑰红`、`琥珀`，以及「自定义…」按钮 —— 用 Qt 内建
`QColorDialog`（带 `DontUseNativeDialog`，避免部分 Linux 桌面原生对话框弹不出）任意取色。
色板是互斥的圆形色块，右侧预览块显示当前十六进制值；自定义色不属于任何预设，不会被误勾选。

主题色影响 QSS 里的选中底色、链接色、`QToolButton:checked`、输入控件焦点边框、标签色条、
分组标题等，以及 `QPalette::Link` / `QPalette::Highlight`。实现上把所有硬编码紫色换成
`@ACCENT@` / `@ACCENT_RGB@` / `@ACCENT_DEEP@` / `@ACCENT_TINT@` 四个占位符，由
`finishStyleSheet()` 在生成样式表时按深浅主题落地 —— 深/浅各取同一基色的不同明度。
默认基色对三档派生色做精确还原，保证不选新颜色时视觉零回归（已有测试逐字节锁住）。

配置存于 `prefs/accentColor`，启动时载入并校验（非法值回退默认紫）；运行期修改会重建全局
样式表，界面字体等既有设置由 `ThemeManager` 的字体缓存补回，不受影响。

---

## 文件比对（Comparison / Diff）

「视图 > 文件比对」是收敛后的**子菜单**，含 6 项：`文件对比`（`Ctrl+Alt+C`）、`比对选区`、
`选择文件比对`、`与 Git HEAD 比对`、`与 SVN BASE 比对`、`关闭对比`（`Ctrl+Alt+X`），
不再平铺在「视图」里。

**工具栏的「文件对比」按钮是下拉开关型按钮**：主体点击进入并呈选中态（深浅主题下均为紫色高亮），
**再次点击即退出比对模式**；`Ctrl+Alt+C` 同样是切换语义。右侧箭头下拉提供 `与 Git HEAD 比对`、
`与 SVN BASE 比对`、`选择文件比对` 三个入口。

`选择文件比对` 打开 **Qt 内建**文件选择器（与「打开文件」一致，避免部分 Linux 桌面原生对话框弹不出），
用独立 `EditorView` 走同一套编码探测 / 语言检测 / 行尾识别，把所选文件与当前文档比对：
**当前文档在左、所选文件在右**，复用独立只读副本链路，不改动任何标签页；读取前有 16MB 体积护栏，
另有 `createCompareView()` 的单侧 16MB 兜底。

**只在比对模式下**，选项栏（「忽略空白」「忽略大小写」所在行）右侧会出现「下一个差异」
「上一个差异」「关闭对比」三个按钮——它们由 `CompareWidget` 持有，随比对视图一起创建/销毁。
`F8` / `Shift+F8` 保留为窗口级快捷键，不再占菜单位置。

选区比对、与 Git HEAD / SVN BASE 比对等入口进入时，按钮会自动同步为选中态；因标签页不足、
文件过大等被护栏拒绝时，按钮回滚为未选中，不会出现「假选中」。菜单、`Ctrl+Alt+X`、工具条
「关闭对比」按钮退出时同样会取消选中。

右侧差异分布小地图可点击跳转。退出比对后原标签页与文档保持完好（比对使用独立只读副本，
不抢占 TabManager 的页面）。
