#include "MainWindow.h"
#include "utils/ThemeManager.h"
#include "utils/PrintHelper.h"
#include "utils/ToolbarIcon.h"
#include "widgets/TitleBar.h"
#include <QStyleOption>
#include <QPainter>
#include <QVBoxLayout>
#include <QSizeGrip>
#include "StyleManager.h"
#include <QMenuBar>
#include <QMenu>
#include <QToolBar>
#include <QStatusBar>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QApplication>
#include <QClipboard>
#include <QPalette>
#include <QStyle>
#include <QStyleFactory>
#include <QDesktopServices>
#include <QUrl>
#include <QProcess>
#include <QDateTime>
#include <QInputDialog>
#include <QPrinter>
#include <QPrintDialog>
#include <QTimer>
#include <QSignalBlocker>
#include <QtConcurrent/QtConcurrentRun>
#include <vector>
#include "utils/CodeFormatter.h"
#include "utils/LocalizationManager.h"
#include "dialogs/StyleOverride.h"
#include "dialogs/ShortcutManager.h"
#include "dialogs/AboutDlg.h"
#include "widgets/CompareWidget.h"
#include "utils/SingleInstance.h"

namespace {
// 写盘期间临时解除对该路径的 inotify 监视，析构后恢复，避免自身保存触发“外部修改”假警报。
class WatchGuard {
public:
    WatchGuard(QFileSystemWatcher* w, const QString& path)
        : _w(w), _path(path), _removed(w && !path.isEmpty() && w->files().contains(path)) {
        if (_removed) _w->removePath(_path);
    }
    ~WatchGuard() { if (_removed && _w) _w->addPath(_path); }
    WatchGuard(const WatchGuard&) = delete;
    WatchGuard& operator=(const WatchGuard&) = delete;
private:
    QFileSystemWatcher* _w;
    QString _path;
    bool _removed;
};

// 将首选项里的 defaultEOL 文本映射为枚举。
LineEnding eolFromPref(const QString& s) {
    if (s.contains("CRLF") || s.contains("Windows")) return LineEnding::CRLF;
    if (s.contains("Macintosh") || s.contains("Mac") || s.compare("CR", Qt::CaseInsensitive) == 0) return LineEnding::CR;
    return LineEnding::LF;
}

// 新建文档的默认语言：兼容旧配置里的显示名（C++ / Shell），统一折算成 langs.model.xml 的 id。
QString defaultLanguageId(const QString& stored) {
    return QString::fromStdString(
        StyleManager::instance().resolveLanguageId(stored.toStdString()));
}

bool hasLocalUrl(const QMimeData* mime) {
    const QList<QUrl> urls = mime->urls();
    for (const QUrl& url : urls) {
        if (url.isLocalFile()) return true;
    }
    return false;
}

// 工具栏一项的声明：图标基名 + 中文提示 + 对应槽 + 快捷键（仅用于 tooltip 展示，
// 真正的 shortcut 仍由菜单项持有，避免同一按键被两个 QAction 抢注产生歧义）。
struct ToolEntry {
    const char* icon;   // 空串表示一个分隔符
    const char* text;
    void (MainWindow::*slot)();
    QKeySequence seq;
};
} // namespace

struct LanguageItem {
    QString id;
    QString displayName;
};

static const std::map<char, std::vector<LanguageItem>> s_languageGroups = {
    {'A', {{"actionscript", "ActionScript"}, {"ada", "Ada"}, {"asm", "Assembly"}, {"asn1", "ASN.1"}, {"asp", "ASP"}, {"autoit", "AutoIt"}}},
    {'B', {{"bash", "Bash / Shell"}, {"batch", "Batch (批处理)"}, {"baanc", "BaanC"}, {"blitzbasic", "BlitzBasic"}}},
    {'C', {{"c", "C"}, {"cpp", "C++"}, {"cs", "C#"}, {"cmake", "CMake"}, {"cobol", "COBOL"}, {"coffeescript", "CoffeeScript"}, {"css", "CSS"}, {"csound", "Csound"}}},
    {'D', {{"d", "D"}, {"diff", "Diff"}}},
    {'E', {{"erlang", "Erlang"}, {"escript", "ESCRIPT"}}},
    {'F', {{"forth", "Forth"}, {"fortran", "Fortran (free form)"}, {"fortran77", "Fortran (fixed form)"}, {"freebasic", "FreeBasic"}}},
    {'G', {{"go", "Go"}, {"gui4cli", "Gui4Cli"}}},
    {'H', {{"haskell", "Haskell"}, {"html", "HTML"}}},
    {'I', {{"ini", "INI file"}, {"inno", "Inno Setup"}, {"ihex", "Intel HEX"}}},
    {'J', {{"java", "Java"}, {"javascript.js", "JavaScript"}, {"json", "JSON"}, {"jsp", "JSP"}}},
    {'K', {{"kix", "KiXtart"}}},
    {'L', {{"latex", "LaTeX"}, {"lisp", "Lisp"}, {"lua", "Lua"}}},
    {'M', {{"makefile", "Makefile"}, {"markdown", "Markdown"}, {"matlab", "MATLAB"}}},
    {'N', {{"nsis", "NSIS"}, {"nim", "Nim"}}},
    {'O', {{"objc", "Objective-C"}, {"oscript", "OScript"}}},
    {'P', {{"pascal", "Pascal"}, {"perl", "Perl"}, {"php", "PHP"}, {"postscript", "PostScript"}, {"powershell", "PowerShell"}, {"props", "Properties"}, {"purebasic", "PureBasic"}, {"python", "Python"}}},
    {'R', {{"r", "R"}, {"rc", "Resource file (RC)"}, {"ruby", "Ruby"}, {"rust", "Rust"}}},
    {'S', {{"sql", "SQL"}, {"swift", "Swift"}, {"smalltalk", "Smalltalk"}, {"scheme", "Scheme"}}},
    {'T', {{"tcl", "TCL"}, {"tex", "TeX"}, {"toml", "TOML"}, {"typescript", "TypeScript"}}},
    {'V', {{"vb", "Visual Basic"}, {"verilog", "Verilog"}, {"vhdl", "VHDL"}}},
    {'X', {{"xml", "XML"}}},
    {'Y', {{"yaml", "YAML"}}}
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , _tabManager(new TabManager(this))
    , _findDialog(new FindReplaceDialog(this))
    , _fileWatcher(new QFileSystemWatcher(this))
{
    setAttribute(Qt::WA_StyledBackground, true);
    // 无边框窗口：标题栏由自绘 TitleBar 接管，因此深色/浅色下顶部颜色完全可控（不再跟随系统主题）。
    setWindowFlag(Qt::FramelessWindowHint, true);
    // 将菜单栏/工具栏/编辑区/状态栏整体内缩 1px，让 QMainWindow 的 1px 样式边框显露出来，
    // 否则子控件会铺满并遮盖边框，叠在其他窗口上时看不到程序边界。
    setContentsMargins(1, 1, 1, 1);
    // 编辑区由 ScintillaEditBase 自己接收拖放；主窗口覆盖标题栏/标签栏/工具栏/状态栏等其余区域。
    setAcceptDrops(true);
    setCentralWidget(_tabManager);
    setWindowTitle(QStringLiteral("Noda"));
    resize(1080, 780);

    // 记住系统默认字体：首选项选“系统默认”时要能恢复，而不是把当前字体当成默认。
    _defaultUiFont = QApplication::font();
    loadPreferences();

    setupMenus();
    setupTitleBar();
    setupToolBar();
    setupStatusBar();
    connect(&LocalizationManager::instance(), &LocalizationManager::languageChanged,
            this, &MainWindow::refreshStatusBarText);
    setupFolderTree();
    loadRecentFiles();

    connect(_tabManager, &TabManager::activeEditorChanged, this, &MainWindow::onActiveEditorChanged);
    connect(_tabManager, &TabManager::hexViewToggled, this, [this](bool active) {
        if (_actViewHex) _actViewHex->setChecked(active);
    });
    // 上游 quitOnEmpty：延迟到事件循环再 close()，避免在 closeTab 的析构路径里销毁窗口。
    connect(_tabManager, &TabManager::quitOnEmptyRequested, this, [this]() {
        QTimer::singleShot(0, this, &QWidget::close);
    });
    // 标签文本变化（保存/另存/重命名/修改标记）时，同步窗口标题——任务栏悬停提示读的就是它。
    connect(_tabManager, &TabManager::tabTitleChanged, this, [this](int) {
        syncWindowTitle();
    });
    connect(_tabManager, &TabManager::editorCreated, this, [this](EditorView* ed) {
        applyPreferencesToEditor(ed);
        connect(ed, &ScintillaEditBase::uriDropped, this, [this](const QString& uri) {
            openDroppedUrl(QUrl(uri));
        });
        // 缓冲区一旦回到“已保存”状态，旧备份必须立刻作废：否则程序被强杀后，
        // session.xml 里记的 backupFilePath 会把旧内容盖回已经保存过的文件。
        // 保存、另存、重新载入、外部修改自动重载都会走 savePointChanged -> 这里。
        connect(ed, &EditorView::documentModified, this, [this, ed](bool modified) {
            if (!modified) dropBackup(ed);
        });
    });
    // 标签被销毁时删掉它的备份，不然 backup/ 下会留一堆没人引用的孤儿文件。
    connect(_tabManager, &TabManager::editorClosing, this, &MainWindow::dropBackup);
    // 双击标签栏空白处新建文档（上游 的同一手势）。走 onNewFile 而不是直接
    // createNewTab()，新建的文档才会带上首选项里的默认 EOL / 编码。
    connect(_tabManager, &TabManager::blankAreaActivated, this, &MainWindow::onNewFile);
    // 标签右键：TabManager 已经选中被点的标签，这里只负责弹出对应菜单。
    connect(_tabManager, &TabManager::tabContextMenuRequested,
            this, &MainWindow::showTabContextMenu);
    connect(_fileWatcher, &QFileSystemWatcher::fileChanged, this, &MainWindow::onFileChangedOnDisk);

    _backupTimer = new QTimer(this);
    connect(_backupTimer, &QTimer::timeout, this, &MainWindow::syncBackups);
    restartBackupTimer();

    // 上次会话有可恢复的内容就照原样恢复（含未保存文档），否则给一个空白标签。
    if (!restoreSession()) {
        auto* firstEd = _tabManager->createNewTab();
        if (firstEd) {
            firstEd->applyDefaultDocFormat(eolFromPref(_prefs.defaultEOL), _prefs.defaultEncoding.toStdString());
            firstEd->applyLanguage(defaultLanguageId(_prefs.defaultLanguage).toStdString());
        }
    }
    // 启动时 _prefs 已载入，旧值比较无法判断“尚未应用”，必须强制走一遍。
    applyPreferences(_prefs, true);
    reloadStyleOverrides();

    ThemeManager::applyTheme(_prefs.darkMode);

    if (_prefs.darkMode) {
        onToggleDarkMode(true);
    }

    // 所有菜单/工具栏 QAction 已建好，此时套用用户自定义快捷键。
    ShortcutManager::instance().applyTo(this);
}

void MainWindow::setupMenus() {
    menuBar()->setNativeMenuBar(false);
    setupFileMenu();
    setupEditMenu();
    setupSearchMenu();
    setupViewMenu();
    setupEncodingMenu();
    setupLanguageMenu();
    setupSettingsMenu();
    setupHelpMenu();

    // QMenu 的弹出窗口是一个矩形的原生窗口，而样式表里的 border-radius 只能决定 Qt 在窗口内
    // 怎么画：圆角之外、矩形角落之内的几像素仍会被窗口底色填实，看上去就像圆角菜单外面
    // 又套了一层直角框。打开逐像素 alpha，让没画到的角落真透明，圆角才立得住。
    // 菜单全部在上面那几个 setup*Menu() 里建完（没有运行期才 new 的，实测 53 个），
    // 所以这一趟递归能覆盖到所有层级子菜单。合成器缺失时角落会透出脏像素，但 GNOME/mutter 常驻合成。
    const auto menus = findChildren<QMenu*>();
    for (QMenu* m : menus)
        m->setAttribute(Qt::WA_TranslucentBackground);
}

void MainWindow::setupTitleBar() {
    // 无边框下用自绘标题栏替换系统标题栏，并把菜单栏嵌入标题栏同一行的左侧：
    // 标题栏 = 左：应用图标 → 菜单栏 → 可拖动空白 → 右：最小化/最大化/关闭。
    auto* header = new QWidget(this);
    auto* hl = new QVBoxLayout(header);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);

    auto* tb = new TitleBar(header);
    tb->insertMenuWidget(menuBar()); // 复用已构建的菜单栏，插到应用图标右侧
    hl->addWidget(tb);

    setMenuWidget(header);
}

void MainWindow::setupFileMenu() {
    auto* menu = menuBar()->addMenu("文件(&F)");

    menu->addAction("新建(&N)", QKeySequence::New, this, &MainWindow::onNewFile);
    menu->addAction("打开(&O)...", QKeySequence::Open, this, &MainWindow::onOpenFile);
    menu->addAction("打开文件夹(&D)...", this, &MainWindow::onOpenFolder);

    auto* openFolderMenu = menu->addMenu("打开所在文件夹(&F)");
    openFolderMenu->addAction("在文件管理器中打开", this, &MainWindow::onOpenFolderInFileManager);
    openFolderMenu->addAction("在终端中打开", this, &MainWindow::onOpenFolderInTerminal);

    menu->addAction("重新载入文件(&L)", this, &MainWindow::onReloadFile);

    menu->addSeparator();
    menu->addAction("保存(&S)", QKeySequence::Save, this, &MainWindow::onSaveFile);
    menu->addAction("另存为(&A)...", QKeySequence::SaveAs, this, &MainWindow::onSaveAsFile);
    menu->addAction("拷贝另存为(&Y)...", this, &MainWindow::onSaveCopyAs);
    auto* saveAllAct = menu->addAction("全部保存(&E)", this, &MainWindow::onSaveAllFiles);
    saveAllAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));

    menu->addAction("重命名(&R)...", this, &MainWindow::onRenameFile);
    menu->addAction("从磁盘删除(&B)", this, &MainWindow::onDeleteFileFromDisk);

    menu->addSeparator();
    menu->addAction("关闭(&C)", QKeySequence::Close, this, &MainWindow::onCloseFile);

    auto* closeMoreMenu = menu->addMenu("关闭多个文档(&M)");
    auto* closeAllAct = closeMoreMenu->addAction("全部关闭(&E)", this, &MainWindow::onCloseAllFiles);
    closeAllAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W));
    closeMoreMenu->addAction("关闭当前以外所有文件", this, &MainWindow::onCloseAllExceptCurrent);
    closeMoreMenu->addAction("关闭左侧所有文件", this, &MainWindow::onCloseAllToLeft);
    closeMoreMenu->addAction("关闭右侧所有文件", this, &MainWindow::onCloseAllToRight);
    closeMoreMenu->addAction("关闭所有未修改的文件", this, &MainWindow::onCloseAllUnchanged);

    auto* restoreAct = menu->addAction("恢复最近一个关闭的文件", this, &MainWindow::onRestoreClosedFile);
    restoreAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));

    menu->addAction("打印(&P)...", QKeySequence::Print, this, &MainWindow::onPrint);

    menu->addSeparator();
    _recentFilesMenu = menu->addMenu("近期文件(&R)");
    updateRecentFilesMenu();

    menu->addSeparator();
    menu->addAction("退出(&X)", QKeySequence::Quit, this, &QWidget::close);
}

void MainWindow::setupEditMenu() {
    auto* menu = menuBar()->addMenu("编辑(&E)");

    menu->addAction("撤销(&U)", QKeySequence::Undo, this, &MainWindow::onUndo);
    menu->addAction("恢复(&R)", QKeySequence::Redo, this, &MainWindow::onRedo);

    menu->addSeparator();
    menu->addAction("剪切(&T)", QKeySequence::Cut, this, &MainWindow::onCut);
    menu->addAction("复制(&C)", QKeySequence::Copy, this, &MainWindow::onCopy);
    menu->addAction("粘贴(&P)", QKeySequence::Paste, this, &MainWindow::onPaste);
    menu->addAction("删除(&D)", QKeySequence::Delete, this, &MainWindow::onDelete);
    menu->addAction("全选(&A)", QKeySequence::SelectAll, this, &MainWindow::onSelectAll);

    // “两点定块”：先把光标放在起点执行一次，再移到终点执行一次，中间用普通移动光标即可。
    // 两条命令只差选取模式，菜单里成对出现（同 上游）。
    menu->addAction("开始/结束 选取(&S)", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B), this,
                    [this]() { onBeginEndSelect(false); });
    menu->addAction("开始/结束 按列选取", QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_B), this,
                    [this]() { onBeginEndSelect(true); });

    menu->addSeparator();
    auto* insertMenu = menu->addMenu("插入");
    insertMenu->addAction("日期时间 (短)", [this]() { onInsertDateTime(false); });
    insertMenu->addAction("日期时间 (长)", [this]() { onInsertDateTime(true); });

    auto* copyMenu = menu->addMenu("复制到剪贴板(&Y)");
    copyMenu->addAction("复制当前文件路径", this, &MainWindow::onCopyFilePath);
    copyMenu->addAction("复制当前文件名", this, &MainWindow::onCopyFileName);
    copyMenu->addAction("复制当前目录路径", this, &MainWindow::onCopyDir);
    copyMenu->addAction("复制所有文件名", this, &MainWindow::onCopyAllFileNames);
    copyMenu->addAction("复制所有文件路径", this, &MainWindow::onCopyAllFilePaths);

    auto* indentMenu = menu->addMenu("缩进(&I)");
    indentMenu->addAction("增加缩进", this, &MainWindow::onIndent);
    indentMenu->addAction("减少缩进", this, &MainWindow::onUnindent);

    auto* caseMenu = menu->addMenu("转换大小写(&V)");
    auto* upperAct = caseMenu->addAction("转成大写(&U)", this, &MainWindow::onToUpperCase);
    upperAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U));
    auto* lowerAct = caseMenu->addAction("转成小写(&L)", this, &MainWindow::onToLowerCase);
    lowerAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_U));
    caseMenu->addAction("每词首字母大写(&P)", this, &MainWindow::onToTitleCase);
    caseMenu->addAction("大小写互换(&I)", this, &MainWindow::onInvertCase);

    auto* lineMenu = menu->addMenu("行操作(&L)");
    auto* dupLineAct = lineMenu->addAction("重复当前行", this, &MainWindow::onDuplicateCurrentLine);
    dupLineAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    auto* delLineAct = lineMenu->addAction("删除当前行", this, &MainWindow::onDeleteCurrentLine);
    delLineAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_L));
    auto* moveUpAct = lineMenu->addAction("向上移动当前行", this, &MainWindow::onMoveLineUp);
    moveUpAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Up));
    auto* moveDownAct = lineMenu->addAction("向下移动当前行", this, &MainWindow::onMoveLineDown);
    moveDownAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Down));
    auto* joinAct = lineMenu->addAction("合并行", this, &MainWindow::onJoinLines);
    joinAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_J));
    lineMenu->addAction("拆分行", this, &MainWindow::onSplitLines);
    lineMenu->addSeparator();
    lineMenu->addAction("升序排列文本行", [this]() { onSortLines(true, false); });
    lineMenu->addAction("降序排列文本行", [this]() { onSortLines(false, false); });
    lineMenu->addAction("升序排列文本行(忽略大小写)", [this]() { onSortLines(true, true); });
    lineMenu->addAction("降序排列文本行(忽略大小写)", [this]() { onSortLines(false, true); });
    lineMenu->addAction("删除重复行", this, &MainWindow::onRemoveDuplicateLines);
    lineMenu->addAction("逆序排列行", this, &MainWindow::onReverseLines);
    lineMenu->addSeparator();
    auto* insAboveAct = lineMenu->addAction("在当前行上方插入空行", [this]() { onInsertEmptyLine(true); });
    insAboveAct->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_Return));
    auto* insBelowAct = lineMenu->addAction("在当前行下方插入空行", [this]() { onInsertEmptyLine(false); });
    insBelowAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Return));

    auto* commentMenu = menu->addMenu("注释/取消注释(&M)");
    auto* lineCommentAct = commentMenu->addAction("单行注释/取消注释", this, &MainWindow::onToggleLineComment);
    lineCommentAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Slash));
    auto* blockCommentAct = commentMenu->addAction("区块注释", this, &MainWindow::onBlockComment);
    blockCommentAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Q));
    commentMenu->addAction("清除区块注释", this, &MainWindow::onUnblockComment);

    auto* blankMenu = menu->addMenu("空白字符操作(&B)");
    blankMenu->addAction("移除行尾空格", this, &MainWindow::onTrimTrailingSpaces);
    blankMenu->addAction("移除行首空格", this, &MainWindow::onTrimLeadingSpaces);
    blankMenu->addAction("移除行首和行尾空格", this, &MainWindow::onTrimBoth);
    blankMenu->addAction("TAB 转空格", this, &MainWindow::onTabsToSpaces);
    blankMenu->addAction("空格转 TAB", this, &MainWindow::onSpacesToTabs);
    blankMenu->addSeparator();
    blankMenu->addAction("移除空行", [this]() { onRemoveEmptyLines(false); });
    blankMenu->addAction("移除空行(包括空白字符)", [this]() { onRemoveEmptyLines(true); });

    menu->addSeparator();
    auto* formatAct = menu->addAction("代码格式化(&F)", this, &MainWindow::onFormatCode);
    formatAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));

    menu->addSeparator();
    // 列选模式入口：前者只讲怎么进入列选，后者真正改写列内容。
    menu->addAction("列选模式...", this, &MainWindow::onColumnModeTip);
    menu->addAction("列编辑器(&N)...", QKeySequence(Qt::ALT | Qt::Key_C), this, &MainWindow::onColumnEditor);

    menu->addSeparator();
    _actReadOnly = menu->addAction("当前文件设为只读", this, &MainWindow::onToggleReadOnly);
    _actReadOnly->setCheckable(true);
}

void MainWindow::setupSearchMenu() {
    auto* menu = menuBar()->addMenu("搜索(&S)");

    menu->addAction("查找(&F)...", QKeySequence::Find, this, &MainWindow::onFind);
    auto* repAct = menu->addAction("替换(&R)...", this, &MainWindow::onReplace);
    repAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_H));

    auto* findNextAct = menu->addAction("查找下一个(&N)", this, &MainWindow::onFindNext);
    findNextAct->setShortcut(QKeySequence(Qt::Key_F3));
    auto* findPrevAct = menu->addAction("查找上一个(&P)", this, &MainWindow::onFindPrev);
    findPrevAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3));

    auto* qFindNextAct = menu->addAction("快速查找下一个(&V)", this, &MainWindow::onQuickFindNext);
    qFindNextAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F3));
    auto* qFindPrevAct = menu->addAction("快速查找上一个(&V)", this, &MainWindow::onQuickFindPrev);
    qFindPrevAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F3));

    auto* gotoAct = menu->addAction("定位/跳转行(&G)...", this, &MainWindow::onGoToLine);
    gotoAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_G));

    menu->addSeparator();
    auto* braceAct = menu->addAction("转到匹配的括号(&M)", this, &MainWindow::onGoToMatchingBrace);
    braceAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    auto* selBraceAct = menu->addAction("选中匹配括号之间的所有字符(&E)", this, &MainWindow::onSelectToMatchingBrace);
    selBraceAct->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_B));

    menu->addSeparator();
    auto* bookmarkMenu = menu->addMenu("书签(&B)");
    auto* toggleBmAct = bookmarkMenu->addAction("切换书签", this, &MainWindow::onToggleBookmark);
    toggleBmAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_F2));
    auto* nextBmAct = bookmarkMenu->addAction("下一个书签", this, &MainWindow::onNextBookmark);
    nextBmAct->setShortcut(QKeySequence(Qt::Key_F2));
    auto* prevBmAct = bookmarkMenu->addAction("上一个书签", this, &MainWindow::onPrevBookmark);
    prevBmAct->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F2));
    bookmarkMenu->addSeparator();
    bookmarkMenu->addAction("清除所有书签", this, &MainWindow::onClearAllBookmarks);
    bookmarkMenu->addAction("复制书签行", this, &MainWindow::onCopyBookmarkedLines);
    bookmarkMenu->addAction("剪切书签行", this, &MainWindow::onCutBookmarkedLines);
    bookmarkMenu->addAction("删除书签行", this, &MainWindow::onDeleteBookmarkedLines);
    bookmarkMenu->addAction("删除未标记书签行", this, &MainWindow::onDeleteNonBookmarkedLines);
    bookmarkMenu->addAction("反向标记书签", this, &MainWindow::onInverseBookmarks);

    auto* markMenu = menu->addMenu("用格式标记选中文本(&A)");
    for (int k = 1; k <= 5; ++k) {
        markMenu->addAction(QString("使用格式 %1").arg(k), [this, k]() { onMarkText(k); });
    }
    markMenu->addSeparator();
    markMenu->addAction("清除所有格式标记", this, &MainWindow::onClearMarks);
}

void MainWindow::setupViewMenu() {
    auto* menu = menuBar()->addMenu("视图(&V)");

    auto* fsAct = menu->addAction("全屏模式", this, &MainWindow::onToggleFullScreen);
    fsAct->setCheckable(true);
    fsAct->setShortcut(QKeySequence(Qt::Key_F11));

    _actAlwaysOnTop = menu->addAction("总在最前", this, &MainWindow::onToggleAlwaysOnTop);
    _actAlwaysOnTop->setCheckable(true);

    // 目录树停靠窗被用户关闭后，仍能从这里重新打开。
    _actFolderTree = menu->addAction("文件夹树(&T)");
    _actFolderTree->setCheckable(true);

    _actWrap = menu->addAction("自动换行", this, &MainWindow::onToggleWordWrap);
    _actWrap->setCheckable(true);

    auto* symMenu = menu->addMenu("显示符号");
    _actShowWs = symMenu->addAction("显示空格与制表符", this, &MainWindow::onToggleShowWhitespace);
    _actShowWs->setCheckable(true);
    _actShowEol = symMenu->addAction("显示换行符", this, &MainWindow::onToggleShowEOL);
    _actShowEol->setCheckable(true);
    _actShowAll = symMenu->addAction("显示所有字符", this, &MainWindow::onToggleShowAllChars);
    _actShowAll->setCheckable(true);
    _actShowGuides = symMenu->addAction("显示缩进参考线", this, &MainWindow::onToggleShowIndentGuides);
    _actShowGuides->setCheckable(true);

    // "显示所有字符"是前两个开关的合体：单独改动任一个时，它只在两者都开时才保持勾选。
    auto syncShowAll = [this]() {
        if (_actShowAll) _actShowAll->setChecked(_actShowWs->isChecked() && _actShowEol->isChecked());
    };
    connect(_actShowWs, &QAction::toggled, this, syncShowAll);
    connect(_actShowEol, &QAction::toggled, this, syncShowAll);

    auto* zoomMenu = menu->addMenu("缩放");
    auto* zoomInAct = zoomMenu->addAction("放大(&I)", this, &MainWindow::onZoomIn);
    zoomInAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Equal));
    auto* zoomOutAct = zoomMenu->addAction("缩小(&O)", this, &MainWindow::onZoomOut);
    zoomOutAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Minus));
    auto* zoomResetAct = zoomMenu->addAction("恢复默认缩放", this, &MainWindow::onZoomReset);
    zoomResetAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));

    auto* foldMenu = menu->addMenu("折叠层次");
    auto* foldCurAct = foldMenu->addAction("折叠当前层次", this, &MainWindow::onFoldCurrent);
    foldCurAct->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_F));
    auto* unfoldCurAct = foldMenu->addAction("展开当前层次", this, &MainWindow::onUnfoldCurrent);
    unfoldCurAct->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F));
    auto* foldAllAct = foldMenu->addAction("折叠所有层次", this, &MainWindow::onFoldAll);
    foldAllAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_0));
    auto* unfoldAllAct = foldMenu->addAction("展开所有层次", this, &MainWindow::onUnfoldAll);
    unfoldAllAct->setShortcut(QKeySequence(Qt::ALT | Qt::SHIFT | Qt::Key_0));
    foldMenu->addSeparator();
    for (int lvl = 1; lvl <= 8; ++lvl) {
        foldMenu->addAction(QString("折叠层次 %1").arg(lvl), [this, lvl]() { onFoldLevel(lvl); });
    }
    foldMenu->addSeparator();
    // unfoldLevel() 早已实现，此前一直没有菜单入口；与「折叠层次 N」成对提供。
    auto* unfoldMenu = foldMenu->addMenu("展开层次(&U)");
    for (int lvl = 1; lvl <= 8; ++lvl) {
        unfoldMenu->addAction(QString("展开层次 %1").arg(lvl), [this, lvl]() { onUnfoldLevel(lvl); });
    }

    auto* tabNavMenu = menu->addMenu("标签页");
    auto* nextTabAct = tabNavMenu->addAction("下个标签页", this, &MainWindow::onNextTab);
    nextTabAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageDown));
    auto* prevTabAct = tabNavMenu->addAction("上个标签页", this, &MainWindow::onPrevTab);
    prevTabAct->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageUp));
    auto* firstTabAct = tabNavMenu->addAction("首个标签页", this, &MainWindow::onFirstTab);
    firstTabAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Home));
    auto* lastTabAct = tabNavMenu->addAction("末尾标签页", this, &MainWindow::onLastTab);
    lastTabAct->setShortcut(QKeySequence(Qt::ALT | Qt::Key_End));
    tabNavMenu->addSeparator();
    auto* moveLeftAct = tabNavMenu->addAction("前移标签页", this, &MainWindow::onMoveTabLeft);
    moveLeftAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_PageUp));
    auto* moveRightAct = tabNavMenu->addAction("后移标签页", this, &MainWindow::onMoveTabRight);
    moveRightAct->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_PageDown));

    menu->addSeparator();
    menu->addAction("摘要信息 / 统计信息...", this, &MainWindow::onShowDocSummary);

    menu->addSeparator();
    _actViewHex = menu->addAction(tr("以十六进制查看(&H)"), this, &MainWindow::onToggleHexView);
    _actViewHex->setObjectName("actViewHex");
    _actViewHex->setCheckable(true);
    _actViewHex->setShortcut(QKeySequence(tr("Ctrl+Alt+H")));

    menu->addSeparator();
    _actCompare = menu->addAction(tr("文件对比(&C)"), this, &MainWindow::onCompareFiles);
    _actCompare->setObjectName("actCompare");
    _actCompare->setShortcut(QKeySequence(tr("Ctrl+Alt+C")));

    _actClearCompare = menu->addAction(tr("关闭对比(&X)"), this, &MainWindow::onCloseCompare);
    _actClearCompare->setObjectName("actClearCompare");
    _actClearCompare->setShortcut(QKeySequence(tr("Ctrl+Alt+X")));
    _actClearCompare->setEnabled(false);

    _actNextDiff = menu->addAction(tr("下一个差异(&N)"), this, &MainWindow::onNextDiff);
    _actNextDiff->setObjectName("actNextDiff");
    _actNextDiff->setShortcut(QKeySequence(Qt::Key_F8));
    _actNextDiff->setEnabled(false);

    _actPrevDiff = menu->addAction(tr("上一个差异(&P)"), this, &MainWindow::onPrevDiff);
    _actPrevDiff->setObjectName("actPrevDiff");
    _actPrevDiff->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F8));
    _actPrevDiff->setEnabled(false);

    _actDarkMode = menu->addAction("深色模式", this, &MainWindow::onToggleDarkMode);
    _actDarkMode->setCheckable(true);
}

void MainWindow::setupEncodingMenu() {
    auto* menu = menuBar()->addMenu("编码(&N)");

    _encodingGroup = new QActionGroup(this);
    _encodingGroup->setExclusive(true);

    QStringList encs = {"ANSI", "UTF-8", "UTF-8 BOM", "UTF-16 BE BOM", "UTF-16 LE BOM"};
    for (const auto& enc : encs) {
        auto* act = menu->addAction(enc);
        act->setCheckable(true);
        act->setData(enc);
        _encodingGroup->addAction(act);
        _encodingActionMap[enc] = act;
        connect(act, &QAction::triggered, this, [this, enc]() { onSetEncoding(enc); });
    }

    auto* csMenu = menu->addMenu("字符集");
    auto addCharset = [this, csMenu](const QString& title, const QStringList& list) {
        auto* sub = csMenu->addMenu(title);
        for (const auto& enc : list) {
            auto* act = sub->addAction(enc);
            act->setCheckable(true);
            act->setData(enc);
            _encodingGroup->addAction(act);
            _encodingActionMap[enc] = act;
            connect(act, &QAction::triggered, this, [this, enc]() { onSetEncoding(enc); });
        }
    };

    addCharset("中文", {"GB2312", "GBK", "GB18030", "Big5"});
    addCharset("西欧", {"ISO-8859-1", "Windows-1252"});
    addCharset("日文", {"Shift-JIS", "EUC-JP"});
    addCharset("韩文", {"EUC-KR"});
    addCharset("西里尔文", {"Windows-1251"});

    menu->addSeparator();
    auto* convMenu = menu->addMenu("转为编码");
    convMenu->addAction("转为 ANSI 编码", [this]() { onConvertToEncoding("ANSI"); });
    convMenu->addAction("转为 UTF-8 编码", [this]() { onConvertToEncoding("UTF-8"); });
    convMenu->addAction("转为 UTF-8 BOM 编码", [this]() { onConvertToEncoding("UTF-8 BOM"); });
    convMenu->addAction("转为 UTF-16 BE BOM 编码", [this]() { onConvertToEncoding("UTF-16BE"); });
    convMenu->addAction("转为 UTF-16 LE BOM 编码", [this]() { onConvertToEncoding("UTF-16LE"); });

    menu->addSeparator();
    auto* eolMenu = menu->addMenu("文档格式转换 (换行符)");
    eolMenu->addAction("转为 Windows 格式", [this]() { onConvertLineEnding(LineEnding::CRLF); });
    eolMenu->addAction("转为 Unix 格式", [this]() { onConvertLineEnding(LineEnding::LF); });
    eolMenu->addAction("转为 Macintosh 格式", [this]() { onConvertLineEnding(LineEnding::CR); });
}

void MainWindow::setupLanguageMenu() {
    auto* langMenu = menuBar()->addMenu("语言(&L)");
    _langActionGroup = new QActionGroup(this);
    _langActionGroup->setExclusive(true);

    auto* normalAct = langMenu->addAction("纯文本");
    normalAct->setCheckable(true);
    normalAct->setChecked(true);
    normalAct->setData("normal");
    _langActionGroup->addAction(normalAct);
    _langActionMap["normal"] = normalAct;
    connect(normalAct, &QAction::triggered, this, [this]() { setLanguage("normal"); });

    langMenu->addSeparator();

    for (const auto& [letter, items] : s_languageGroups) {
        auto* subMenu = langMenu->addMenu(QString(letter));
        for (const auto& item : items) {
            auto* act = subMenu->addAction(item.displayName);
            act->setCheckable(true);
            act->setData(item.id);
            _langActionGroup->addAction(act);
            _langActionMap[item.id] = act;
            connect(act, &QAction::triggered, this, [this, id = item.id]() { setLanguage(id); });
        }
    }

    if (_langActionMap.count("javascript.js")) {
        _langActionMap["javascript"] = _langActionMap["javascript.js"];
    }

    connect(_langActionGroup, &QActionGroup::triggered, this, &MainWindow::onLanguageActionTriggered);
}

void MainWindow::setupSettingsMenu() {
    auto* menu = menuBar()->addMenu("设置(&T)");
    menu->addAction("首选项(&P)...", this, &MainWindow::onPreferences);
    menu->addAction("语言样式配置器(&S)...", this, &MainWindow::onStyleConfigurator);
    menu->addAction("快捷键映射器(&K)...", this, &MainWindow::onShortcutsMapper);
}

void MainWindow::setupHelpMenu() {
    auto* menu = menuBar()->addMenu("帮助(&H)");
    _actAbout = menu->addAction(toolBarIcon(QStringLiteral("info"), _prefs.darkMode),
                                "关于 Noda(&A)...", this, &MainWindow::onAbout);
    _actAbout->setShortcut(QKeySequence(Qt::Key_F1));
}

void MainWindow::onAbout() {
    AboutDlg dlg(this);
    dlg.exec();
}

void MainWindow::setupToolBar() {
    _toolBar = addToolBar("主工具栏");
    _toolBar->setMovable(false);
    _toolBar->setIconSize(QSize(16, 16));

    const QVector<ToolEntry> entries = {
        {"file-plus",       "新建",     &MainWindow::onNewFile,        QKeySequence::New},
        {"folder-open",     "打开",     &MainWindow::onOpenFile,       QKeySequence::Open},
        {"folder-tree",     "打开文件夹", &MainWindow::onOpenFolder,     QKeySequence()},
        {"save",            "保存",     &MainWindow::onSaveFile,       QKeySequence::Save},
        {"save-all",        "全部保存",  &MainWindow::onSaveAllFiles,   QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)},
        {"x",               "关闭",     &MainWindow::onCloseFile,      QKeySequence::Close},
        {"list-x",          "全部关闭",  &MainWindow::onCloseAllFiles,  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_W)},
        {"printer",         "打印",     &MainWindow::onPrint,          QKeySequence::Print},
        {"",                "",         nullptr,                       QKeySequence()},
        {"scissors",        "剪切",     &MainWindow::onCut,            QKeySequence::Cut},
        {"copy",            "复制",     &MainWindow::onCopy,           QKeySequence::Copy},
        {"clipboard-paste", "粘贴",     &MainWindow::onPaste,          QKeySequence::Paste},
        {"",                "",         nullptr,                       QKeySequence()},
        {"undo-2",          "撤销",     &MainWindow::onUndo,           QKeySequence::Undo},
        {"redo-2",          "恢复",     &MainWindow::onRedo,           QKeySequence::Redo},
        {"",                "",         nullptr,                       QKeySequence()},
        {"search",          "查找",     &MainWindow::onFind,           QKeySequence::Find},
        {"replace",         "替换",     &MainWindow::onReplace,        QKeySequence(Qt::CTRL | Qt::Key_H)},
        {"",                "",         nullptr,                       QKeySequence()},
        {"wand-2",          "代码格式化", &MainWindow::onFormatCode,     QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F)},
        {"",                "",         nullptr,                       QKeySequence()},
        {"zoom-in",         "放大",     &MainWindow::onZoomIn,         QKeySequence(Qt::CTRL | Qt::Key_Equal)},
        {"zoom-out",        "缩小",     &MainWindow::onZoomOut,        QKeySequence(Qt::CTRL | Qt::Key_Minus)},
    };

    // 开关型按钮（自动换行 / 显示所有字符 / 缩进参考线）与菜单里同名的 checkable 项共享状态：
    // 两侧只持有各自的 QAction（否则图标会一并出现在菜单里），靠一对 toggled 连接保持勾选同步。
    // setChecked 不会触发 triggered，因此不会把动作执行两遍。
    auto addToggleTool = [this](const char* iconName, const QString& text,
                                QAction* menuAct, void (MainWindow::*toggle)(bool)) {
        QAction* act = _toolBar->addAction(text);
        act->setCheckable(true);
        if (menuAct) {
            act->setChecked(menuAct->isChecked());
            connect(act, &QAction::toggled, menuAct, &QAction::setChecked);
            connect(menuAct, &QAction::toggled, act, &QAction::setChecked);
        }
        connect(act, &QAction::triggered, this, toggle);
        const QString seqText = menuAct ? menuAct->shortcut().toString() : QString();
        act->setToolTip(seqText.isEmpty() ? text : text + " (" + seqText + ")");
        _toolActions.append(qMakePair(act, QString::fromLatin1(iconName)));
    };

    _toolActions.clear();
    for (const auto& e : entries) {
        if (!e.icon || !*e.icon) { // 分隔符
            _toolBar->addSeparator();
            continue;
        }
        const QString text = QString::fromUtf8(e.text);
        QAction* act = _toolBar->addAction(text);
        connect(act, &QAction::triggered, this, e.slot);
        // 图标模式下文字不再可见，tooltip 必须同时给出动作名和快捷键。
        const QString seqText = e.seq.toString();
        act->setToolTip(seqText.isEmpty() ? text : text + " (" + seqText + ")");
        _toolActions.append(qMakePair(act, QString::fromLatin1(e.icon)));
    }

    _toolBar->addSeparator();
    addToggleTool("wrap-text", QStringLiteral("自动换行"), _actWrap, &MainWindow::onToggleWordWrap);
    addToggleTool("pilcrow", QStringLiteral("显示所有字符"), _actShowAll, &MainWindow::onToggleShowAllChars);
    addToggleTool("indent-guide", QStringLiteral("显示缩进参考线"), _actShowGuides, &MainWindow::onToggleShowIndentGuides);
    addToggleTool("binary", QStringLiteral("以十六进制查看"), _actViewHex, &MainWindow::onToggleHexView);

    QAction* tbCompare = _toolBar->addAction(QStringLiteral("文件对比"));
    connect(tbCompare, &QAction::triggered, this, &MainWindow::onCompareFiles);
    tbCompare->setToolTip(QStringLiteral("文件对比 (Ctrl+Alt+C)"));
    _toolActions.append(qMakePair(tbCompare, QStringLiteral("git-compare")));

    refreshToolBarIcons();
}

void MainWindow::refreshToolBarIcons() {
    bool anyIcon = false;
    for (const auto& pair : _toolActions) {
        QIcon ic = toolBarIcon(pair.second, _prefs.darkMode);
        if (!ic.isNull()) anyIcon = true;
        pair.first->setIcon(ic);
    }
    if (_actAbout) {
        _actAbout->setIcon(toolBarIcon(QStringLiteral("info"), _prefs.darkMode));
    }
    // 资源缺失（未跑生成脚本的旧构建目录）时退回纯文字，不能让工具栏变成一排空按钮。
    if (_toolBar) {
        _toolBar->setToolButtonStyle(anyIcon ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextOnly);
    }
}

void MainWindow::setupStatusBar() {
    auto* bar = statusBar();
    // 状态栏文本在运行期按当前语言拼接，不能只靠 Show 时的一次性翻译。
    _statusPosLabel = new QLabel(
        LocalizationManager::instance().translate(QStringLiteral("行: %1  列: %2")).arg(1).arg(1), this);
    _statusLengthLabel = new QLabel(
        LocalizationManager::instance().translate(QStringLiteral("长度: %1  行数: %2")).arg(0).arg(1), this);
    // 这两块是运行期按模板拼接的动态文本，禁止翻译器记录/回滚，否则切语言后会被撤回。
    _statusPosLabel->setProperty("nppNoL10n", true);
    _statusLengthLabel->setProperty("nppNoL10n", true);
    _statusLineEndingLabel = new ClickableLabel("Unix (LF)", this);
    _statusEncodingLabel = new ClickableLabel("UTF-8", this);
    _statusLangLabel = new QLabel("纯文本", this);

    _statusPosLabel->setMinimumWidth(120);
    _statusLengthLabel->setMinimumWidth(150);
    _statusLineEndingLabel->setMinimumWidth(110);
    _statusEncodingLabel->setMinimumWidth(100);
    _statusLangLabel->setMinimumWidth(110);

    bar->addPermanentWidget(_statusLengthLabel);
    bar->addPermanentWidget(_statusPosLabel);
    bar->addPermanentWidget(_statusLineEndingLabel);
    bar->addPermanentWidget(_statusEncodingLabel);
    bar->addPermanentWidget(_statusLangLabel);

    connect(_statusLineEndingLabel, &ClickableLabel::doubleClicked, this, &MainWindow::onToggleLineEnding);
    connect(_statusEncodingLabel, &ClickableLabel::doubleClicked, this, [this]() {
        onSetEncoding("UTF-8");
    });

    // 无边框窗口：状态栏最右端放一个拖拽缩放手柄（QSizeGrip 内部会调用 startSystemResize）。
    bar->addPermanentWidget(new QSizeGrip(bar));
}

// ---------------- 文件操作 ----------------

void MainWindow::onNewFile() {
    auto* ed = _tabManager->createNewTab();
    if (ed) {
        ed->applyDefaultDocFormat(eolFromPref(_prefs.defaultEOL), _prefs.defaultEncoding.toStdString());
        ed->applyLanguage(defaultLanguageId(_prefs.defaultLanguage).toStdString());
        updateStatusBar(ed);
    }
}

void MainWindow::onOpenFile() {
    QString path = QFileDialog::getOpenFileName(
        this, "打开文件", QString(), "所有文件 (*.*)", nullptr,
        QFileDialog::DontUseNativeDialog);
    if (!path.isEmpty()) {
        openFile(path);
    }
}

void MainWindow::openDroppedUrl(const QUrl& url) {
    if (!url.isLocalFile()) return;
    QFileInfo fi(url.toLocalFile());
    if (fi.isFile()) {
        openFile(fi.absoluteFilePath());
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (hasLocalUrl(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        QMainWindow::dragEnterEvent(event);
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event) {
    if (hasLocalUrl(event->mimeData())) {
        event->acceptProposedAction();
    } else {
        QMainWindow::dragMoveEvent(event);
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    if (hasLocalUrl(event->mimeData())) {
        event->acceptProposedAction();
        const QList<QUrl> urls = event->mimeData()->urls();
        for (const QUrl& url : urls) {
            openDroppedUrl(url);
        }
    } else {
        QMainWindow::dropEvent(event);
    }
}

void MainWindow::openFile(const QString& filePath) {
    // 已在某标签中打开：直接切换。
    for (int i = 0; i < _tabManager->count(); ++i) {
        auto* ed = _tabManager->editorAt(i);
        if (ed && QString::fromStdString(ed->buffer()->getFilePath()) == filePath) {
            _tabManager->setCurrentIndex(i);
            return;
        }
    }

    // 优先复用当前“空/未命名/未修改”的标签，否则新建一个。
    EditorView* ed = nullptr;
    auto* curEd = _tabManager->currentEditor();
    if (curEd && curEd->buffer()->getFilePath().empty() && !curEd->buffer()->isModified() && curEd->send(SCI_GETLENGTH) == 0) {
        ed = curEd;
    } else {
        ed = _tabManager->createNewTab();
    }
    if (!ed) return;

    if (ed->loadFromFile(filePath)) {
        _tabManager->updateTabTitle(_tabManager->currentIndex());
        if (!_fileWatcher->files().contains(filePath)) _fileWatcher->addPath(filePath);
        addRecentFile(filePath);
        updateStatusBar(ed);
        updateLanguageUI(QString::fromStdString(ed->currentLanguage()));
        // 含 NUL 字节的二进制文件在文本视图里只会显示乱码：直接切到十六进制查看。
        if (ed->buffer()->isBinary() && _actViewHex && !_actViewHex->isChecked()) {
            _actViewHex->trigger();
        }
    }
}

void MainWindow::setupFolderTree() {
    _folderTree = new FolderTreePane(this);
    _folderTree->setObjectName(QStringLiteral("folderTreeDock"));
    _folderTree->hide();   // 未选择目录时不占左侧空间
    addDockWidget(Qt::LeftDockWidgetArea, _folderTree);

    connect(_folderTree, &FolderTreePane::fileActivated, this,
            [this](const QString& path) { openFile(path); });

    if (_actFolderTree) {
        connect(_actFolderTree, &QAction::toggled, this, [this](bool visible) {
            if (_folderTree) _folderTree->setVisible(visible);
        });
    }
    // 用户点停靠窗自己的关闭按钮时，把菜单勾选状态同步回来；阻断信号避免回环。
    connect(_folderTree, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (!_actFolderTree) return;
        QSignalBlocker block(_actFolderTree);
        _actFolderTree->setChecked(visible);
    });
}

void MainWindow::onOpenFolder() {
    // 强制使用 Qt 内建目录选择对话框，而非原生对话框。
    // 在部分 Linux 桌面（KDE 平台的 KDirSelectDialog、GNOME/Wayland 缺少
    // xdg-desktop-portal 时）原生 getExistingDirectory 会静默返回空、窗口根本不弹出，
    // 表现为「点打开文件夹没反应」。Qt 内建对话框随 QtWidgets 一起链接，必然可用。
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("打开文件夹"), QDir::homePath(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks
            | QFileDialog::DontUseNativeDialog);
    if (!dir.isEmpty()) openFolderPath(dir);
}

bool MainWindow::openFolderPath(const QString& path) {
    if (!_folderTree || !_folderTree->setRootPath(path)) return false;
    _folderTree->show();
    if (_actFolderTree && !_actFolderTree->isChecked())
        _actFolderTree->setChecked(true);
    return true;
}

void MainWindow::openFilesFromIpc(const QStringList& files) {
    for (const QString& f : files) {
        if (!f.isEmpty() && QFile::exists(f)) {
            openFile(f);
        }
    }
    SingleInstance::activateWindow(this);
}

void MainWindow::onOpenFolderInFileManager() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    QString filePath = QString::fromStdString(ed->buffer()->getFilePath());
    QString dir = filePath.isEmpty() ? QDir::currentPath() : QFileInfo(filePath).absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::onOpenFolderInTerminal() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    QString filePath = QString::fromStdString(ed->buffer()->getFilePath());
    QString dir = filePath.isEmpty() ? QDir::currentPath() : QFileInfo(filePath).absolutePath();

    QStringList terminals = {"gnome-terminal", "konsole", "xfce4-terminal", "x-terminal-emulator", "xterm"};
    for (const auto& term : terminals) {
        if (QProcess::startDetached(term, QStringList(), dir)) {
            return;
        }
    }
}

void MainWindow::onReloadFile() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    QString path = QString::fromStdString(ed->buffer()->getFilePath());
    if (path.isEmpty()) return;

    if (ed->buffer()->isModified()) {
        auto reply = QMessageBox::question(this, "重新读取文件", "当前文档已被修改，是否放弃未保存的更改并重新载入？", QMessageBox::Yes | QMessageBox::No);
        if (reply != QMessageBox::Yes) return;
    }
    ed->loadFromFile(path);
    updateStatusBar(ed);
}

void MainWindow::reportSaveFailure(const QString& path, EditorView* ed) {
    const QString reason = ed ? ed->lastSaveError() : QStringLiteral("未知错误");
    emit saveFailed(path, reason);
    QMessageBox::warning(this, QStringLiteral("保存失败"),
        QStringLiteral("未能写入 %1。\n\n原因：%2\n\n"
                       "磁盘文件未被改动，文档内容仍在编辑器中；"
                       "可在「编码」菜单换用其他编码后重试。").arg(path, reason));
}

void MainWindow::onSaveFile() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    if (ed->buffer()->getFilePath().empty()) {
        onSaveAsFile();
        return;
    }
    const QString path = QString::fromStdString(ed->buffer()->getFilePath());
    _savingPaths.insert(path);
    bool ok = false;
    {
        WatchGuard guard(_fileWatcher, path); // 写盘期间解除监视，避免自保存假警报
        ok = ed->saveToFile();
    }
    _savingPaths.remove(path);
    if (!ok) {
        reportSaveFailure(path, ed);
        return;
    }
    _tabManager->updateTabTitle(_tabManager->currentIndex());
    addRecentFile(path);
}

void MainWindow::onSaveAsFile() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    const QString path = QFileDialog::getSaveFileName(
        this, "另存为", QString(), "所有文件 (*.*)", nullptr,
        QFileDialog::DontUseNativeDialog);
    if (path.isEmpty()) return;

    const QString oldPath = QString::fromStdString(ed->buffer()->getFilePath());
    _savingPaths.insert(path);
    if (!oldPath.isEmpty()) _savingPaths.insert(oldPath);
    bool ok = false;
    {
        WatchGuard gNew(_fileWatcher, path);
        WatchGuard gOld(_fileWatcher, oldPath);
        ok = ed->saveToFile(path);
    }
    _savingPaths.remove(path);
    if (!oldPath.isEmpty()) _savingPaths.remove(oldPath);
    if (!ok) {
        reportSaveFailure(path, ed);
        return;
    }
    _tabManager->updateTabTitle(_tabManager->currentIndex());
    if (!_fileWatcher->files().contains(path)) _fileWatcher->addPath(path);
    addRecentFile(path);
    updateStatusBar(ed);
}

void MainWindow::onSaveCopyAs() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    const QString path = QFileDialog::getSaveFileName(
        this, "拷贝另存为", QString(), "所有文件 (*.*)", nullptr,
        QFileDialog::DontUseNativeDialog);
    if (path.isEmpty()) return;

    // 旧实现直接倾倒内存 UTF-8 字节，非 UTF-8 文档会得到编码错误的副本，
    // 而且连 QFile 打不开也完全没有提示。
    if (!ed->saveCopyToFile(path)) {
        reportSaveFailure(path, ed);
    }
}

void MainWindow::onSaveAllFiles() {
    QStringList failures;
    for (int i = 0; i < _tabManager->count(); ++i) {
        auto* ed = _tabManager->editorAt(i);
        if (!ed || !ed->buffer()->isModified()) continue;
        if (ed->buffer()->getFilePath().empty()) {
            _tabManager->setCurrentIndex(i);
            onSaveAsFile();   // 该路径自身已负责失败提示
            continue;
        }
        const QString path = QString::fromStdString(ed->buffer()->getFilePath());
        _savingPaths.insert(path);
        bool ok = false;
        {
            WatchGuard guard(_fileWatcher, path);
            ok = ed->saveToFile();
        }
        _savingPaths.remove(path);
        if (!ok) {
            // 批量保存不逐个弹框：先发信号并收集，循环结束后汇总一次。
            const QString reason = ed->lastSaveError();
            emit saveFailed(path, reason);
            failures << QStringLiteral("%1：%2").arg(path, reason);
            continue;
        }
        _tabManager->updateTabTitle(i);
    }
    if (!failures.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("部分文件保存失败"),
            QStringLiteral("以下文档未保存成功（磁盘文件未被改动）：\n\n%1\n\n"
                           "可在「编码」菜单换用其他编码后重试。")
                .arg(failures.join(QLatin1Char('\n'))));
    }
}

void MainWindow::onRenameFile() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    QString oldPath = QString::fromStdString(ed->buffer()->getFilePath());
    if (oldPath.isEmpty()) return;

    QFileInfo fi(oldPath);
    bool ok = false;
    QString newName = QInputDialog::getText(this, "重命名文件", "请输入新的文件名:", QLineEdit::Normal, fi.fileName(), &ok);
    if (ok && !newName.isEmpty() && newName != fi.fileName()) {
        QString newPath = fi.dir().filePath(newName);
        if (QFile::rename(oldPath, newPath)) {
            ed->buffer()->setFilePath(newPath.toStdString());
            _tabManager->updateTabTitle(_tabManager->currentIndex());
            _fileWatcher->removePath(oldPath);
            _fileWatcher->addPath(newPath);
            addRecentFile(newPath);
        } else {
            QMessageBox::warning(this, "错误", "无法重命名该文件。");
        }
    }
}

void MainWindow::showTabContextMenu(int index, const QPoint& globalPos) {
    std::unique_ptr<QMenu> menu(createTabContextMenu(index));
    if (!menu) return;
    menu->exec(globalPos);
}

QMenu* MainWindow::createTabContextMenu(int index) {
    auto* menu = new QMenu(this);
    // 运行期新建的菜单不在构造时那趟 WA_TranslucentBackground 遍历里；不单独设置的话，
    // QSS 圆角之外的窗口角落会露出方形底色。
    menu->setAttribute(Qt::WA_TranslucentBackground);

    EditorView* ed = _tabManager->editorAt(index);
    if (!ed) return menu;

    const QString path = QString::fromStdString(ed->buffer()->getFilePath());
    const bool hasPath = !path.isEmpty();
    const int count = _tabManager->count();

    // 菜单构建与触发之间当前标签可能已经变化（测试也会直接按 index 构建），
    // 因此每个动作都先定位到被右击的标签，再复用既有处理函数。
    auto selectTab = [this, index]() {
        if (index >= 0 && index < _tabManager->count()) _tabManager->setCurrentIndex(index);
    };
    auto invoke = [this, selectTab](void (MainWindow::*slot)()) {
        return [this, selectTab, slot]() {
            selectTab();
            (this->*slot)();
        };
    };

    // 关闭：按 index 直接关，不依赖 currentIndex。
    menu->addAction(QStringLiteral("关闭"), this, [this, index]() {
        if (index >= 0 && index < _tabManager->count()) _tabManager->closeTab(index);
    });

    auto* closeMany = menu->addMenu(QStringLiteral("关闭多个标签页"));
    closeMany->addAction(QStringLiteral("关闭其他标签页"), this, [this, selectTab]() {
        selectTab();
        _tabManager->closeAllExceptCurrent();
    });
    closeMany->addAction(QStringLiteral("关闭左侧标签页"), this, [this, selectTab]() {
        selectTab();
        _tabManager->closeAllToLeft();
    });
    closeMany->addAction(QStringLiteral("关闭右侧标签页"), this, [this, selectTab]() {
        selectTab();
        _tabManager->closeAllToRight();
    });
    closeMany->addAction(QStringLiteral("关闭所有未修改的标签页"), this, [this, selectTab]() {
        selectTab();
        _tabManager->closeAllUnchanged();
    });

    menu->addAction(QStringLiteral("保存"), this, invoke(&MainWindow::onSaveFile));
    menu->addAction(QStringLiteral("另存为..."), this, invoke(&MainWindow::onSaveAsFile));

    // 上游的 Open into 组：Linux 下对应文件管理器、终端与默认程序。
    auto* openInto = menu->addMenu(QStringLiteral("打开所在文件夹"));
    auto* inFileManager = openInto->addAction(QStringLiteral("在文件管理器中打开"), this,
                                              invoke(&MainWindow::onOpenFolderInFileManager));
    auto* inTerminal = openInto->addAction(QStringLiteral("在终端中打开"), this,
                                           invoke(&MainWindow::onOpenFolderInTerminal));
    openInto->addSeparator();
    auto* inDefaultApp = openInto->addAction(QStringLiteral("用默认程序打开"), this,
        [path]() { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
    inFileManager->setEnabled(hasPath);
    inTerminal->setEnabled(hasPath);
    inDefaultApp->setEnabled(hasPath);

    menu->addAction(QStringLiteral("重命名..."), this,
                    invoke(&MainWindow::onRenameFile))->setEnabled(hasPath);
    menu->addAction(QStringLiteral("移到回收站"), this,
                    invoke(&MainWindow::onMoveToTrashFile))->setEnabled(hasPath);
    menu->addAction(QStringLiteral("从磁盘重新载入"), this,
                    invoke(&MainWindow::onReloadFile))->setEnabled(hasPath);
    menu->addAction(QStringLiteral("打印..."), this, invoke(&MainWindow::onPrint));

    menu->addSeparator();
    QAction* readOnly = menu->addAction(QStringLiteral("只读"));
    readOnly->setCheckable(true);
    readOnly->setChecked(ed->isReadOnly());
    connect(readOnly, &QAction::triggered, this, [this, selectTab](bool on) {
        selectTab();
        onToggleReadOnly(on);
    });

    menu->addSeparator();
    auto* copyMenu = menu->addMenu(QStringLiteral("复制到剪贴板"));
    copyMenu->addAction(QStringLiteral("复制完整路径"), this,
                        invoke(&MainWindow::onCopyFilePath))->setEnabled(hasPath);
    // 新文档也有名字（new 1），所以「复制文件名」不需要磁盘路径。
    copyMenu->addAction(QStringLiteral("复制文件名"), this, invoke(&MainWindow::onCopyFileName));
    copyMenu->addAction(QStringLiteral("复制目录路径"), this,
                        invoke(&MainWindow::onCopyDir))->setEnabled(hasPath);

    // 上游 Move Document 的 Start/End；双视图与新实例本程序没有，不列出。
    auto* moveMenu = menu->addMenu(QStringLiteral("移动标签页"));
    moveMenu->addAction(QStringLiteral("移到最前"), this, [this, index]() {
        if (index >= 0 && index < _tabManager->count()) {
            _tabManager->setCurrentIndex(index);
            _tabManager->moveTabFirst();
        }
    })->setEnabled(index > 0);
    moveMenu->addAction(QStringLiteral("移到最后"), this, [this, index, count]() {
        if (index >= 0 && index < _tabManager->count()) {
            _tabManager->setCurrentIndex(index);
            _tabManager->moveTabLast();
        }
    })->setEnabled(index < count - 1);

    return menu;
}

void MainWindow::onMoveToTrashFile() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    const QString path = QString::fromStdString(ed->buffer()->getFilePath());
    if (path.isEmpty()) return;

    auto reply = QMessageBox::question(this, QStringLiteral("移到回收站"),
        QStringLiteral("确定要把 %1 移到回收站吗？").arg(path),
        QMessageBox::Yes | QMessageBox::No);
    if (reply != QMessageBox::Yes) return;

    QString error;
    if (!QFile::moveToTrash(path, &error)) {
        QMessageBox::warning(this, QStringLiteral("移到回收站失败"),
                             error.isEmpty() ? QStringLiteral("无法移动该文件。") : error);
        return;
    }
    _fileWatcher->removePath(path);
    _tabManager->closeTab(_tabManager->currentIndex());
}

void MainWindow::onDeleteFileFromDisk() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    QString path = QString::fromStdString(ed->buffer()->getFilePath());
    if (path.isEmpty()) return;

    auto reply = QMessageBox::warning(this, "从磁盘删除", QString("确定要从磁盘中永久删除文件 %1 吗？").arg(path), QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        _fileWatcher->removePath(path);
        QFile::remove(path);
        _tabManager->closeTab(_tabManager->currentIndex());
    }
}

void MainWindow::onCloseFile() {
    _tabManager->closeTab(_tabManager->currentIndex());
}

void MainWindow::onCloseAllFiles() {
    _tabManager->closeAll();
}

void MainWindow::onCloseAllExceptCurrent() {
    _tabManager->closeAllExceptCurrent();
}

void MainWindow::onCloseAllToLeft() {
    _tabManager->closeAllToLeft();
}

void MainWindow::onCloseAllToRight() {
    _tabManager->closeAllToRight();
}

void MainWindow::onCloseAllUnchanged() {
    _tabManager->closeAllUnchanged();
}

void MainWindow::onRestoreClosedFile() {
    _tabManager->restoreClosedFile();
}

void MainWindow::onPrint() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;

    // 直接走 SCI_GETTEXT 取全文：EditorView 里现成的 currentTextUtf8() 是 private，
    // 而且分页需要按行遍历，拿到整段文本再 split 比逐行取字更省一次接口改动。
    const sptr_t docLen = ed->send(SCI_GETLENGTH);
    QByteArray raw(docLen + 1, '\0');
    if (docLen > 0) ed->send(SCI_GETTEXT, docLen + 1, reinterpret_cast<sptr_t>(raw.data()));
    raw.chop(1);
    const QStringList lines = QString::fromUtf8(raw).split(QLatin1Char('\n'));

    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, this);
    // 本端口还不支持“仅打印选区”，关掉该选项免得给出一个按了没反应的单选钮。
    dlg.setOption(QAbstractPrintDialog::PrintSelection, false);
    if (dlg.exec() != QDialog::Accepted) return;

    QPainter p;
    if (!p.begin(&printer)) {
        QMessageBox::warning(this, QStringLiteral("打印"), QStringLiteral("无法初始化打印设备。"));
        return;
    }
    // 用编辑器自己的字体打印（等宽提示避免比例字体错位），分页与制表符展开交给 PrintHelper。
    QFont mono(ed->getEditorFontName(), ed->getEditorFontSize());
    mono.setStyleHint(QFont::TypeWriter);
    mono.setFixedPitch(true);
    p.setFont(mono);
    PrintHelper::paintTextLines(p, lines, _prefs.tabWidth,
                                printer.fromPage() > 0 ? printer.fromPage() : 1, printer.toPage());
    p.end();
}

void MainWindow::onOpenRecentFile() {
    auto* act = qobject_cast<QAction*>(sender());
    if (act) {
        openFile(act->data().toString());
    }
}

void MainWindow::onClearRecentFiles() {
    _recentFileList.clear();
    QSettings s;
    s.setValue("recentFiles", _recentFileList);
    updateRecentFilesMenu();
}

void MainWindow::loadRecentFiles() {
    QSettings s;
    _recentFileList = s.value("recentFiles").toStringList();
    updateRecentFilesMenu();
}

void MainWindow::addRecentFile(const QString& path) {
    if (path.isEmpty()) return;
    _recentFileList.removeAll(path);
    _recentFileList.prepend(path);
    while (_recentFileList.size() > 15) {
        _recentFileList.removeLast();
    }
    QSettings s;
    s.setValue("recentFiles", _recentFileList);
    updateRecentFilesMenu();
}

void MainWindow::updateRecentFilesMenu() {
    if (!_recentFilesMenu) return;
    _recentFilesMenu->clear();
    for (const auto& path : _recentFileList) {
        auto* act = _recentFilesMenu->addAction(QFileInfo(path).fileName());
        act->setData(path);
        act->setToolTip(path);
        connect(act, &QAction::triggered, this, &MainWindow::onOpenRecentFile);
    }
    if (!_recentFileList.isEmpty()) {
        _recentFilesMenu->addSeparator();
        _recentFilesMenu->addAction("清除近期文件列表", this, &MainWindow::onClearRecentFiles);
    }
}

// ---------------- 编辑操作 ----------------

void MainWindow::onUndo() { if (auto* ed = _tabManager->currentEditor()) ed->doUndo(); }
void MainWindow::onRedo() { if (auto* ed = _tabManager->currentEditor()) ed->doRedo(); }
void MainWindow::onCut() { if (auto* ed = _tabManager->currentEditor()) ed->doCut(); }
void MainWindow::onCopy() { if (auto* ed = _tabManager->currentEditor()) ed->doCopy(); }
void MainWindow::onPaste() { if (auto* ed = _tabManager->currentEditor()) ed->doPaste(); }
void MainWindow::onDelete() { if (auto* ed = _tabManager->currentEditor()) ed->doDelete(); }
void MainWindow::onSelectAll() { if (auto* ed = _tabManager->currentEditor()) ed->doSelectAll(); }

void MainWindow::onInsertDateTime(bool isLong) {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    QString fmt = isLong ? "yyyy-MM-dd HH:mm:ss dddd" : "yyyy-MM-dd HH:mm";
    QString text = QDateTime::currentDateTime().toString(fmt);
    ed->sends(SCI_REPLACESEL, 0, text.toUtf8().constData());
}

void MainWindow::onCopyFilePath() {
    if (auto* ed = _tabManager->currentEditor()) {
        QApplication::clipboard()->setText(QString::fromStdString(ed->buffer()->getFilePath()));
    }
}

void MainWindow::onCopyFileName() {
    if (auto* ed = _tabManager->currentEditor()) {
        QApplication::clipboard()->setText(QString::fromStdString(ed->buffer()->getFileName()));
    }
}

void MainWindow::onCopyDir() {
    if (auto* ed = _tabManager->currentEditor()) {
        QString fp = QString::fromStdString(ed->buffer()->getFilePath());
        if (!fp.isEmpty()) QApplication::clipboard()->setText(QFileInfo(fp).absolutePath());
    }
}

void MainWindow::onCopyAllFileNames() {
    QStringList names;
    for (int i = 0; i < _tabManager->count(); ++i) {
        names.append(QString::fromStdString(_tabManager->editorAt(i)->buffer()->getFileName()));
    }
    QApplication::clipboard()->setText(names.join("\n"));
}

void MainWindow::onCopyAllFilePaths() {
    QStringList paths;
    for (int i = 0; i < _tabManager->count(); ++i) {
        QString p = QString::fromStdString(_tabManager->editorAt(i)->buffer()->getFilePath());
        if (!p.isEmpty()) paths.append(p);
    }
    QApplication::clipboard()->setText(paths.join("\n"));
}

void MainWindow::onIndent() { if (auto* ed = _tabManager->currentEditor()) ed->indent(); }
void MainWindow::onUnindent() { if (auto* ed = _tabManager->currentEditor()) ed->unindent(); }
void MainWindow::onToUpperCase() { if (auto* ed = _tabManager->currentEditor()) ed->toUpperCase(); }
void MainWindow::onToLowerCase() { if (auto* ed = _tabManager->currentEditor()) ed->toLowerCase(); }
void MainWindow::onToTitleCase() { if (auto* ed = _tabManager->currentEditor()) ed->toTitleCase(); }
void MainWindow::onInvertCase() { if (auto* ed = _tabManager->currentEditor()) ed->invertCase(); }

void MainWindow::onDuplicateCurrentLine() { if (auto* ed = _tabManager->currentEditor()) ed->duplicateCurrentLine(); }
void MainWindow::onDeleteCurrentLine() { if (auto* ed = _tabManager->currentEditor()) ed->deleteCurrentLine(); }
void MainWindow::onMoveLineUp() { if (auto* ed = _tabManager->currentEditor()) ed->moveLineUp(); }
void MainWindow::onMoveLineDown() { if (auto* ed = _tabManager->currentEditor()) ed->moveLineDown(); }
void MainWindow::onJoinLines() { if (auto* ed = _tabManager->currentEditor()) ed->joinLines(); }
void MainWindow::onSplitLines() { if (auto* ed = _tabManager->currentEditor()) ed->splitLines(); }
void MainWindow::onSortLines(bool asc, bool ic) { if (auto* ed = _tabManager->currentEditor()) ed->sortLines(asc, ic); }
void MainWindow::onRemoveDuplicateLines() { if (auto* ed = _tabManager->currentEditor()) ed->removeDuplicateLines(); }
void MainWindow::onReverseLines() { if (auto* ed = _tabManager->currentEditor()) ed->reverseLines(); }
void MainWindow::onInsertEmptyLine(bool above) { if (auto* ed = _tabManager->currentEditor()) ed->insertEmptyLine(above); }

void MainWindow::onToggleLineComment() { if (auto* ed = _tabManager->currentEditor()) ed->toggleLineComment(); }
void MainWindow::onBlockComment() { if (auto* ed = _tabManager->currentEditor()) ed->blockComment(); }
void MainWindow::onUnblockComment() { if (auto* ed = _tabManager->currentEditor()) ed->unblockComment(); }

void MainWindow::onTrimTrailingSpaces() { if (auto* ed = _tabManager->currentEditor()) ed->trimTrailingSpaces(); }
void MainWindow::onTrimLeadingSpaces() { if (auto* ed = _tabManager->currentEditor()) ed->trimLeadingSpaces(); }
void MainWindow::onTrimBoth() { if (auto* ed = _tabManager->currentEditor()) ed->trimBoth(); }
void MainWindow::onTabsToSpaces() { if (auto* ed = _tabManager->currentEditor()) ed->tabsToSpaces(_prefs.tabWidth); }
void MainWindow::onSpacesToTabs() { if (auto* ed = _tabManager->currentEditor()) ed->spacesToTabs(_prefs.tabWidth); }
void MainWindow::onRemoveEmptyLines(bool onlyBlank) { if (auto* ed = _tabManager->currentEditor()) ed->removeEmptyLines(onlyBlank); }
void MainWindow::onToggleReadOnly(bool ro) { if (auto* ed = _tabManager->currentEditor()) ed->setReadOnly(ro); }

void MainWindow::onFormatCode() {
    if (_formatBusy) return;
    EditorView* ed = _tabManager->currentEditor();
    if (!ed) return;
    if (ed->isReadOnly()) {
        statusBar()->showMessage(QStringLiteral("当前文件为只读，无法格式化"), 3000);
        return;
    }
    const std::string langId = ed->currentLanguage();
    if (!CodeFormatter::isSupported(langId)) {
        statusBar()->showMessage(QStringLiteral("代码格式化仅支持 C/C++/C#/Java/Objective-C/JSON"), 4000);
        return;
    }
    auto input = std::make_shared<const std::string>(ed->currentTextUtf8());
    if (input->empty()) return;

    _formatTarget = ed;
    _formatInput = input;
    _formatBusy = true;
    statusBar()->showMessage(QStringLiteral("正在格式化…"));

    if (!_formatWatcher) {
        _formatWatcher = new QFutureWatcher<std::string>(this);
        connect(_formatWatcher, &QFutureWatcher<std::string>::finished,
                this, &MainWindow::onFormatFinished);
    }
    // 格式化是同步阻塞调用（AStyle 1MB 约 0.7s；JSON 流式更快），必须放工作线程，绝不卡 UI。
    _formatWatcher->setFuture(QtConcurrent::run([input, langId]() {
        return CodeFormatter::format(*input, langId);
    }));
}

void MainWindow::onFormatFinished() {
    _formatBusy = false;
    statusBar()->clearMessage();
    const std::string result = _formatWatcher->future().result();
    QPointer<EditorView> target = _formatTarget;
    std::shared_ptr<const std::string> input = _formatInput;
    _formatTarget.clear();
    _formatInput.reset();

    if (!target || !input) return;      // 任务期间标签页已关闭
    if (result.empty()) return;         // 引擎失败：按未改动处理
    // 任务期间用户若改动了文档，丢弃结果，避免覆盖其编辑。
    if (target->currentTextUtf8() != *input) return;
    if (result == *input) return;       // 本就无需改动
    target->applyFormattedText(result);
}

void MainWindow::onBeginEndSelect(bool columnMode) {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    ed->beginOrEndSelect(columnMode);
}

void MainWindow::onColumnModeTip() {
    // 内容对齐 上游 的“列选模式提示”，只把第三种方法的菜单名换成本端口的叫法。
    QMessageBox::information(this, QStringLiteral("列选模式提示"),
        QStringLiteral(
            "有 3 种方法可以切换到列选模式：\n\n"
            "1.（键盘和鼠标）按住 Alt 键，同时用鼠标左键拖动。\n\n"
            "2.（只需键盘）按住 Alt+Shift 键，再使用方向键。\n\n"
            "3.（键盘或鼠标）\n"
            "    把光标放在列块的位置，执行“编辑 > 开始/结束 按列选取”；\n"
            "    再把光标移动到列块的另一端，再次执行同一命令。\n\n"
            "进入列选模式后，可用“编辑 > 列编辑器”(Alt+C) 往每一行批量写入文本或递增数字。"));
}

void MainWindow::onColumnEditor() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;

    // 参数在 MainWindow 里留存，下次打开还能看到上次的输入；
    // 是否已有列选区传给对话框，用于把“没有列选区”时的退化行为先告知用户。
    ColumnEditorDialog dlg(_colEditParams, ed->hasColumnSelection(), this);
    if (dlg.exec() != QDialog::Accepted) return;
    _colEditParams = dlg.params();

    if (_colEditParams.textMode) {
        ed->columnInsertText(_colEditParams.text);
    } else {
        ed->columnInsertNumber(_colEditParams.initialValue, _colEditParams.incrementValue,
                               _colEditParams.repeatValue, _colEditParams.base,
                               _colEditParams.hexUpper, _colEditParams.pad);
    }
}

// ---------------- 搜索操作 ----------------

void MainWindow::onFind() {
    auto* ed = _tabManager->currentEditor();
    if (ed) {
        _findDialog->setTargetEditor(ed);
        auto selLen = ed->send(SCI_GETSELECTIONEND) - ed->send(SCI_GETSELECTIONSTART);
        if (selLen > 0 && selLen < 120) {
            QByteArray buf(selLen + 1, '\0');
            ed->send(SCI_GETSELTEXT, 0, reinterpret_cast<sptr_t>(buf.data()));
            _findDialog->setFindText(QString::fromUtf8(buf.constData()));
        }
    }
    _findDialog->switchToReplaceMode(false);
    _findDialog->show();
    _findDialog->activateWindow();
}

void MainWindow::onReplace() {
    auto* ed = _tabManager->currentEditor();
    if (ed) {
        _findDialog->setTargetEditor(ed);
        auto selLen = ed->send(SCI_GETSELECTIONEND) - ed->send(SCI_GETSELECTIONSTART);
        if (selLen > 0 && selLen < 120) {
            QByteArray buf(selLen + 1, '\0');
            ed->send(SCI_GETSELTEXT, 0, reinterpret_cast<sptr_t>(buf.data()));
            _findDialog->setFindText(QString::fromUtf8(buf.constData()));
        }
    }
    _findDialog->switchToReplaceMode(true);
    _findDialog->show();
    _findDialog->activateWindow();
}

void MainWindow::onFindNext() {
    if (auto* ed = _tabManager->currentEditor()) {
        _findDialog->setTargetEditor(ed);
        _findDialog->onFindNext();
    }
}

void MainWindow::onFindPrev() {
    if (auto* ed = _tabManager->currentEditor()) {
        _findDialog->setTargetEditor(ed);
        _findDialog->onFindPrev();
    }
}

void MainWindow::onQuickFindNext() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    auto len = ed->send(SCI_GETSELECTIONEND) - ed->send(SCI_GETSELECTIONSTART);
    if (len > 0) {
        QByteArray buf(len + 1, '\0');
        ed->send(SCI_GETSELTEXT, 0, reinterpret_cast<sptr_t>(buf.data()));
        ed->findNext(QString::fromUtf8(buf.constData()), true, false, false);
    }
}

void MainWindow::onQuickFindPrev() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    auto len = ed->send(SCI_GETSELECTIONEND) - ed->send(SCI_GETSELECTIONSTART);
    if (len > 0) {
        QByteArray buf(len + 1, '\0');
        ed->send(SCI_GETSELTEXT, 0, reinterpret_cast<sptr_t>(buf.data()));
        ed->findPrev(QString::fromUtf8(buf.constData()), true, false, false);
    }
}

void MainWindow::onGoToLine() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    auto curLine = ed->send(SCI_LINEFROMPOSITION, ed->send(SCI_GETCURRENTPOS)) + 1;
    auto maxLine = ed->send(SCI_GETLINECOUNT);
    GoToLineDialog dlg(static_cast<int>(curLine), static_cast<int>(maxLine), this);
    if (dlg.exec() == QDialog::Accepted) {
        ed->goToLine(dlg.getTargetLine());
    }
}

void MainWindow::onGoToMatchingBrace() { if (auto* ed = _tabManager->currentEditor()) ed->goToMatchingBrace(); }
void MainWindow::onSelectToMatchingBrace() { if (auto* ed = _tabManager->currentEditor()) ed->selectToMatchingBrace(); }

void MainWindow::onToggleBookmark() { if (auto* ed = _tabManager->currentEditor()) ed->toggleBookmark(); }
void MainWindow::onNextBookmark() { if (auto* ed = _tabManager->currentEditor()) ed->nextBookmark(); }
void MainWindow::onPrevBookmark() { if (auto* ed = _tabManager->currentEditor()) ed->prevBookmark(); }
void MainWindow::onClearAllBookmarks() { if (auto* ed = _tabManager->currentEditor()) ed->clearAllBookmarks(); }
void MainWindow::onCopyBookmarkedLines() { if (auto* ed = _tabManager->currentEditor()) ed->copyBookmarkedLines(); }
void MainWindow::onCutBookmarkedLines() { if (auto* ed = _tabManager->currentEditor()) ed->cutBookmarkedLines(); }
void MainWindow::onDeleteBookmarkedLines() { if (auto* ed = _tabManager->currentEditor()) ed->deleteBookmarkedLines(); }
void MainWindow::onDeleteNonBookmarkedLines() { if (auto* ed = _tabManager->currentEditor()) ed->deleteNonBookmarkedLines(); }
void MainWindow::onInverseBookmarks() { if (auto* ed = _tabManager->currentEditor()) ed->inverseBookmarks(); }
void MainWindow::onMarkText(int styleIdx) { if (auto* ed = _tabManager->currentEditor()) ed->markSelectedText(styleIdx); }
void MainWindow::onClearMarks() { if (auto* ed = _tabManager->currentEditor()) ed->clearMarks(); }

// ---------------- 视图操作 ----------------

void MainWindow::onToggleFullScreen(bool fs) {
    if (fs) showFullScreen();
    else showNormal();
}

void MainWindow::onToggleAlwaysOnTop(bool top) {
    setWindowFlag(Qt::WindowStaysOnTopHint, top);
    show();
}

void MainWindow::forEachEditor(const std::function<void(EditorView*)>& fn) {
    for (int i = 0; i < _tabManager->count(); ++i) {
        if (auto* ed = _tabManager->editorAt(i)) fn(ed);
    }
}

// 下面几个视图开关都是“持续状态”：既要写回 _prefs（新标签页靠 editorCreated 从偏好继承），
// 又要当场作用到每一个已打开的标签页，最后落盘，重启后保持不变。
void MainWindow::onToggleWordWrap(bool wrap) {
    _prefs.wordWrap = wrap;
    forEachEditor([wrap](EditorView* ed) { ed->setWrapMode(wrap); });
    savePreferences();
}

void MainWindow::onToggleShowWhitespace(bool show) {
    _prefs.showWhitespace = show;
    forEachEditor([show](EditorView* ed) { ed->setShowWhitespace(show); });
    savePreferences();
}

void MainWindow::onToggleShowEOL(bool show) {
    _prefs.showEOL = show;
    forEachEditor([show](EditorView* ed) { ed->setShowEOL(show); });
    savePreferences();
}

void MainWindow::onToggleShowAllChars(bool show) {
    // “显示所有字符”是前两个开关的合体（与 上游 一致）。
    // 注意 setChecked() 只发射 toggled、不发射 triggered，而渲染逻辑是挂在 triggered 上的，
    // 所以不能指望两个分量开关替我们把命令发出去，必须在这里自己应用一遍。
    if (_actShowWs) _actShowWs->setChecked(show);
    if (_actShowEol) _actShowEol->setChecked(show);
    _prefs.showWhitespace = show;
    _prefs.showEOL = show;
    forEachEditor([show](EditorView* ed) {
        ed->setShowWhitespace(show);
        ed->setShowEOL(show);
    });
    savePreferences();
}

void MainWindow::onToggleShowIndentGuides(bool show) {
    _prefs.showIndentGuides = show;
    forEachEditor([show](EditorView* ed) { ed->setShowIndentGuides(show); });
    savePreferences();
}

void MainWindow::onZoomIn() { if (auto* ed = _tabManager->currentEditor()) ed->zoomIn(); }
void MainWindow::onZoomOut() { if (auto* ed = _tabManager->currentEditor()) ed->zoomOut(); }
void MainWindow::onZoomReset() { if (auto* ed = _tabManager->currentEditor()) ed->zoomReset(); }

void MainWindow::onFoldCurrent() { if (auto* ed = _tabManager->currentEditor()) ed->foldCurrent(); }
void MainWindow::onUnfoldCurrent() { if (auto* ed = _tabManager->currentEditor()) ed->expandCurrent(); }
void MainWindow::onFoldAll() { if (auto* ed = _tabManager->currentEditor()) ed->foldAll(); }
void MainWindow::onUnfoldAll() { if (auto* ed = _tabManager->currentEditor()) ed->unfoldAll(); }
void MainWindow::onFoldLevel(int lvl) { if (auto* ed = _tabManager->currentEditor()) ed->foldLevel(lvl); }
void MainWindow::onUnfoldLevel(int lvl) { if (auto* ed = _tabManager->currentEditor()) ed->unfoldLevel(lvl); }

void MainWindow::onNextTab() {
    int idx = (_tabManager->currentIndex() + 1) % _tabManager->count();
    _tabManager->setCurrentIndex(idx);
}

void MainWindow::onPrevTab() {
    int idx = (_tabManager->currentIndex() - 1 + _tabManager->count()) % _tabManager->count();
    _tabManager->setCurrentIndex(idx);
}

void MainWindow::onFirstTab() { _tabManager->setCurrentIndex(0); }
void MainWindow::onLastTab() { _tabManager->setCurrentIndex(_tabManager->count() - 1); }
void MainWindow::onMoveTabLeft() { _tabManager->moveTabLeft(); }
void MainWindow::onMoveTabRight() { _tabManager->moveTabRight(); }

void MainWindow::onShowDocSummary() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    DocSummary s = ed->getSummary();
    DocSummaryDialog dlg(QString::fromStdString(ed->buffer()->getFilePath()), s, this);
    dlg.exec();
}

void MainWindow::onToggleHexView(bool enabled) {
    _tabManager->toggleHexView(enabled);
}

MainWindow::~MainWindow() {
    // 比对使用独立编辑器副本；CompareWidget 析构会把它们 detach，必须在这里回收。
    delete _compareWidget;
    _compareWidget = nullptr;
    delete _compareEdA;
    _compareEdA = nullptr;
    delete _compareEdB;
    _compareEdB = nullptr;
}

void MainWindow::onCompareFiles() {
    if (_compareWidget) {
        _compareWidget->refreshDiff();
        return;
    }

    if (_tabManager->count() < 2) {
        statusBar()->showMessage(tr("比对功能至少需要打开两个标签页"), 3000);
        return;
    }

    const int curr = _tabManager->currentIndex();
    const int other = (curr > 0) ? (curr - 1) : 1;
    EditorView* edA = _tabManager->editorAt(std::min(curr, other));
    EditorView* edB = _tabManager->editorAt(std::max(curr, other));
    if (!edA || !edB) return;

    // 大文件护栏：单侧 16MB 以上直接拒绝，避免同步比对卡死 UI 或耗尽内存。
    constexpr sptr_t kMaxCompareBytes = 16 * 1024 * 1024;
    if (edA->send(SCI_GETLENGTH) > kMaxCompareBytes || edB->send(SCI_GETLENGTH) > kMaxCompareBytes) {
        statusBar()->showMessage(tr("文件过大，暂不支持比对（单侧上限 16MB）"), 5000);
        return;
    }

    // 用独立副本比对，绝不 reparent TabManager 的页面：关闭对比后标签页仍然完好。
    _compareEdA = new EditorView();
    _compareEdB = new EditorView();
    _compareEdA->setTextFromUtf8(edA->currentTextUtf8());
    _compareEdB->setTextFromUtf8(edB->currentTextUtf8());
    _compareEdA->send(SCI_SETREADONLY, 1);
    _compareEdB->send(SCI_SETREADONLY, 1);
    _compareEdA->setDarkMode(_prefs.darkMode);
    _compareEdB->setDarkMode(_prefs.darkMode);
    _compareEdA->applyLanguage(edA->currentLanguage());
    _compareEdB->applyLanguage(edB->currentLanguage());

    _compareWidget = new CompareWidget(_compareEdA, _compareEdB, _prefs.darkMode, this);
    connect(_compareWidget, &CompareWidget::diffNavigated, this, [this](int cur, int total) {
        if (!_compareWidget) return;
        if (total > 0) {
            statusBar()->showMessage(tr("比对完成：%1 处新增，%2 处删除，%3 处修改 | 差异 %4/%5")
                .arg(_compareWidget->addedCount())
                .arg(_compareWidget->removedCount())
                .arg(_compareWidget->modifiedCount())
                .arg(cur).arg(total));
        } else {
            statusBar()->showMessage(tr("文件完全一致，未发现差异"), 4000);
        }
    });

    setCentralWidget(_compareWidget);

    if (_actClearCompare) _actClearCompare->setEnabled(true);
    if (_actNextDiff) _actNextDiff->setEnabled(_compareWidget->totalDiffCount() > 0);
    if (_actPrevDiff) _actPrevDiff->setEnabled(_compareWidget->totalDiffCount() > 0);
}

void MainWindow::onCloseCompare() {
    if (!_compareWidget) return;

    CompareWidget* cmp = _compareWidget;
    _compareWidget = nullptr;

    cmp->clearDiff();
    cmp->setParent(nullptr);   // 脱离主窗口布局所有权，避免 setCentralWidget 误删
    setCentralWidget(_tabManager);
    delete cmp;                // 析构会把两个副本编辑器 detach

    delete _compareEdA;
    _compareEdA = nullptr;
    delete _compareEdB;
    _compareEdB = nullptr;

    if (_actClearCompare) _actClearCompare->setEnabled(false);
    if (_actNextDiff) _actNextDiff->setEnabled(false);
    if (_actPrevDiff) _actPrevDiff->setEnabled(false);

    updateStatusBar(_tabManager->currentEditor());
}

void MainWindow::onNextDiff() {
    if (_compareWidget) {
        _compareWidget->nextDiff();
    }
}

void MainWindow::onPrevDiff() {
    if (_compareWidget) {
        _compareWidget->prevDiff();
    }
}

void MainWindow::onToggleDarkMode(bool enabled) {
    _isDarkMode = enabled;
    _prefs.darkMode = enabled;
    applyDarkModePalette(enabled);
    refreshToolBarIcons(); // 图标前景色分深/浅两套，必须随主题重建
    _tabManager->setDarkMode(enabled);
    if (_compareWidget) {
        _compareWidget->setDarkMode(enabled);
    }
    for (int i = 0; i < _tabManager->count(); ++i) {
        if (auto* ed = _tabManager->editorAt(i)) {
            ed->setDarkMode(enabled);
            ed->applyLanguage(ed->currentLanguage());
        }
    }
    savePreferences(); // 直接通过菜单切换时也持久化，避免下次启动丢失
}

void MainWindow::applyDarkModePalette(bool dark) {
    ThemeManager::applyTheme(dark);
}

// ---------------- 编码操作 ----------------

void MainWindow::onSetEncoding(const QString& enc) {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    const QString path = QString::fromStdString(ed->buffer()->getFilePath());
    if (!path.isEmpty()) {
        // “设定编码”= 用新编码重新解释磁盘字节（等同 N++ 在指定编码下载入/重载）。
        if (ed->buffer()->isModified()) {
            auto reply = QMessageBox::question(this, "以指定编码重新载入",
                QString("当前文档已修改，以 %1 重新载入将丢弃未保存的更改。是否继续？").arg(enc),
                QMessageBox::Yes | QMessageBox::No);
            if (reply != QMessageBox::Yes) {
                // 撤销菜单勾选态：回到缓冲区当前编码
                updateEncodingUI(QString::fromStdString(ed->buffer()->getEncoding()));
                return;
            }
        }
        ed->reloadWithEncoding(enc.toStdString());
    } else {
        // 新建空文档：仅设定后续保存使用的目标编码。
        ed->buffer()->setEncoding(enc.toStdString());
    }
    updateEncodingUI(enc);
}

void MainWindow::onConvertToEncoding(const QString& enc) {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    ed->buffer()->setEncoding(enc.toStdString());
    ed->buffer()->setModified(true);
    _tabManager->updateTabTitle(_tabManager->currentIndex());
    updateEncodingUI(enc);
}

void MainWindow::onConvertLineEnding(LineEnding le) {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    ed->convertLineEndings(le);
    if (_statusLineEndingLabel) _statusLineEndingLabel->setText(QString::fromStdString(ed->buffer()->getLineEndingString()));
}

void MainWindow::onToggleLineEnding() {
    auto* ed = _tabManager->currentEditor();
    if (!ed) return;
    LineEnding cur = ed->buffer()->getLineEnding();
    LineEnding next = (cur == LineEnding::CRLF) ? LineEnding::LF : LineEnding::CRLF;
    onConvertLineEnding(next);
}

void MainWindow::updateEncodingUI(const QString& enc) {
    auto it = _encodingActionMap.find(enc);
    if (it != _encodingActionMap.end()) {
        it->second->setChecked(true);
    }
    _statusEncodingLabel->setText(enc);
}

// ---------------- 语言操作 ----------------

void MainWindow::onLanguageActionTriggered(QAction* action) {
    if (!action) return;
    setLanguage(action->data().toString());
}

void MainWindow::setLanguage(const QString& langId) {
    if (auto* ed = _tabManager->currentEditor()) {
        ed->applyLanguage(langId.toStdString());
        updateLanguageUI(langId);
    }
}

void MainWindow::updateLanguageUI(const QString& langId) {
    auto it = _langActionMap.find(langId);
    if (it != _langActionMap.end()) {
        it->second->setChecked(true);
        _statusLangLabel->setText(it->second->text());
    } else {
        auto defaultIt = _langActionMap.find("normal");
        if (defaultIt != _langActionMap.end()) {
            defaultIt->second->setChecked(true);
        }
        _statusLangLabel->setText(langId);
    }
}

// ---------------- 设置操作 ----------------

void MainWindow::onPreferences() {
    PreferencesDialog dlg(_prefs, this);
    if (dlg.exec() == QDialog::Accepted) {
        applyPreferences(dlg.getPreferences());
        // 持久化只发生在用户确认之后；applyPreferences 本身保持纯应用，启动不再重复写盘。
        savePreferences();
    }
}

void MainWindow::onStyleConfigurator() {
    StyleConfigDialog dlg(this);
    // 对话框内部写入 QSettings 并 emit styleUpdated；这里回灌覆盖并重绘所有编辑器。
    connect(&dlg, &StyleConfigDialog::styleUpdated, this, &MainWindow::reloadStyleOverrides);
    dlg.exec();
    reloadStyleOverrides(); // 关闭时确保最新覆盖已生效
}

void MainWindow::reloadStyleOverrides() {
    const QList<StyleOverride> overrides = StyleOverride::loadAll();

    int globalFg = -1;
    int globalBg = -1;
    QString globalFontName;
    int globalFontSize = 0;

    for (const auto& o : overrides) {
        if (o.langId == QLatin1String("global")) {
            globalFg = o.fg;
            globalBg = o.bg;
            globalFontName = o.fontName;
            globalFontSize = o.fontSize;
            continue;
        }
        const std::string lang = o.langId.toStdString();
        StyleManager::instance().overrideStyle(lang, o.styleId, o.fg, o.bg,
                                               o.bold, o.italic);
        if (!o.fontName.isEmpty() || o.fontSize > 0) {
            StyleManager::instance().overrideFont(lang, o.styleId,
                                                  o.fontName.toStdString(), o.fontSize);
        }
    }

    for (int i = 0; i < _tabManager->count(); ++i) {
        auto* ed = _tabManager->editorAt(i);
        if (!ed) continue;
        // 先落实全局字体（setEditorFont 会重置 128 个槽位的字体），再叠加语言/槽位级
        // 覆盖；顺序反了语言级字体就会被冲掉。
        if (!globalFontName.isEmpty() || globalFontSize > 0) {
            ed->setEditorFont(globalFontName.isEmpty() ? ed->getEditorFontName() : globalFontName,
                              globalFontSize > 0 ? globalFontSize : ed->getEditorFontSize());
        }
        ed->applyThemeStyles(ed->currentLanguage());
        if (globalFg != -1 || globalBg != -1) {
            ed->applyGlobalStyleOverride(globalFg, globalBg);
        }
    }
}

void MainWindow::onShortcutsMapper() {
    // 第一个 this 是「真实 QAction 的来源」，第二个是 Qt 的父窗口。
    ShortcutsDialog dlg(this, this);
    dlg.exec();
}

void MainWindow::applyUiFonts(const AppPreferences& prefs) {
    QStringList families;
    // 英文字体在前、中文在后形成字体回退链：拉丁字符用 en，CJK 落到 zh。
    if (!prefs.uiFontEn.isEmpty()) families << prefs.uiFontEn;
    if (!prefs.uiFontZh.isEmpty()) families << prefs.uiFontZh;

    QFont target = families.isEmpty() ? _defaultUiFont : QApplication::font();
    if (!families.isEmpty()) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        target.setFamilies(families);
#else
        target.setFamily(families.first());
#endif
    }

    // 只改 QApplication::setFont() 不够：主题 QSS 里的 QWidget font-family 会覆盖它。
    // 两者一起更新，并在后续切换深/浅主题时由 ThemeManager 自动保留。
    ThemeManager::setUiFont(target, families);
}

void MainWindow::applyThemePreference(const QString& themeName) {
    QString name = themeName;
    if (name.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive)) name.chop(4);

    if (name.isEmpty() || name.compare(QStringLiteral("Default"), Qt::CaseInsensitive) == 0) {
        StyleManager::instance().resetTheme();
        return;
    }

    const auto themes = StyleManager::locateThemes(
        QApplication::applicationDirPath().toStdString());
    for (const ThemeInfo& theme : themes) {
        if (QString::fromStdString(theme.name).compare(name, Qt::CaseInsensitive) != 0)
            continue;
        std::string error;
        if (!StyleManager::instance().applyThemeFile(theme.path, &error)) {
            StyleManager::instance().resetTheme();
            statusBar()->showMessage(QString::fromStdString(error), 6000);
        }
        return;
    }

    StyleManager::instance().resetTheme();
    statusBar()->showMessage(
        QStringLiteral("找不到主题「%1」，已回退到内置样式").arg(themeName), 6000);
}

void MainWindow::applyPreferences(const AppPreferences& prefs, bool force) {
    const AppPreferences old = _prefs;
    _prefs = prefs;

    // ---- 纯 UI / 标签栏：值没变就不重排、不重新 polish ----
    if (_toolBar && (force || old.hideToolBar != prefs.hideToolBar)) {
        _toolBar->setVisible(!prefs.hideToolBar);
    }
    if (_tabManager) {
        // 下面多个样式 setter 会改 QTabBar 的动态属性，合并成一次 polish。
        _tabManager->beginStyleBatch();
        if (force || old.tabHide != prefs.tabHide)
            _tabManager->setTabBarHidden(prefs.tabHide);
        if (force || old.tabVertical != prefs.tabVertical)
            _tabManager->setVerticalTabs(prefs.tabVertical);
        if (force || old.tabShowCloseButton != prefs.tabShowCloseButton)
            _tabManager->setTabsClosable(prefs.tabShowCloseButton);
        if (force || old.tabLock != prefs.tabLock)
            _tabManager->setMovable(!prefs.tabLock);
        if (force || old.tabReduce != prefs.tabReduce)
            _tabManager->setTabReduce(prefs.tabReduce);
        if (force || old.tabActiveTopBar != prefs.tabActiveTopBar)
            _tabManager->setActiveTopBar(prefs.tabActiveTopBar);
        if (force || old.tabInactiveColor != prefs.tabInactiveColor)
            _tabManager->setInactiveColored(prefs.tabInactiveColor);
        if (force || old.tabCompactLabelLen != prefs.tabCompactLabelLen)
            _tabManager->setCompactLabelLength(prefs.tabCompactLabelLen);
        if (force || old.tabDoubleClickClose != prefs.tabDoubleClickClose)
            _tabManager->setDoubleClickClose(prefs.tabDoubleClickClose);
        if (force || old.tabQuitOnEmpty != prefs.tabQuitOnEmpty)
            _tabManager->setQuitOnEmpty(prefs.tabQuitOnEmpty);
        _tabManager->endStyleBatch();
    }

    if (force || old.uiFontZh != prefs.uiFontZh || old.uiFontEn != prefs.uiFontEn)
        applyUiFonts(prefs);

    // 界面语言运行时切换，不需要重启。
    if (force || old.uiLanguage != prefs.uiLanguage) {
        LocalizationManager::instance().setLanguage(prefs.uiLanguage);
        refreshStatusBarText();
    }

    // _isDarkMode 本身就是“当前已应用状态”，启动时不需要 force。
    const bool darkChanged = old.darkMode != prefs.darkMode || prefs.darkMode != _isDarkMode;
    if (darkChanged) {
        onToggleDarkMode(prefs.darkMode);
        if (_actDarkMode) _actDarkMode->setChecked(prefs.darkMode);
    }

    if (_actWrap && (force || old.wordWrap != prefs.wordWrap))
        _actWrap->setChecked(prefs.wordWrap);
    if (_actShowWs && (force || old.showWhitespace != prefs.showWhitespace))
        _actShowWs->setChecked(prefs.showWhitespace);
    if (_actShowEol && (force || old.showEOL != prefs.showEOL))
        _actShowEol->setChecked(prefs.showEOL);
    if (_actShowGuides && (force || old.showIndentGuides != prefs.showIndentGuides))
        _actShowGuides->setChecked(prefs.showIndentGuides);
    // 合体项的勾选由 syncShowAll 在分量开关的 toggled 里推导，但两个分量都没变化时
    // toggled 不会发射，所以这里显式算一次，避免从配置文件恢复时漏掉。
    if (_actShowAll && (force || old.showWhitespace != prefs.showWhitespace
                        || old.showEOL != prefs.showEOL)) {
        _actShowAll->setChecked(prefs.showWhitespace && prefs.showEOL);
    }

    const bool themeChanged = force || old.themeName != prefs.themeName;
    if (themeChanged) applyThemePreference(prefs.themeName);

    const bool editorChanged = force
        || old.tabWidth != prefs.tabWidth || old.tabToSpaces != prefs.tabToSpaces
        || old.wordWrap != prefs.wordWrap || old.showLineNumbers != prefs.showLineNumbers
        || old.showFoldMargin != prefs.showFoldMargin
        || old.showIndentGuides != prefs.showIndentGuides
        || old.showWhitespace != prefs.showWhitespace || old.showEOL != prefs.showEOL
        || old.fontName != prefs.fontName || old.fontSize != prefs.fontSize
        || old.fontSmooth != prefs.fontSmooth || old.darkMode != prefs.darkMode
        || old.highlightMatchingWords != prefs.highlightMatchingWords
        || old.highlightMatchingBraces != prefs.highlightMatchingBraces
        || old.highlightMatchCase != prefs.highlightMatchCase
        || old.highlightMatchWholeWord != prefs.highlightMatchWholeWord
        || old.urlClickable != prefs.urlClickable
        || old.autoCompletion != prefs.autoCompletion
        || old.autoCompletionMinChars != prefs.autoCompletionMinChars
        || old.autoCompletionType != prefs.autoCompletionType
        || old.autoCompletionIgnoreNumbers != prefs.autoCompletionIgnoreNumbers;
    if (editorChanged) {
        for (int i = 0; i < _tabManager->count(); ++i) {
            if (auto* ed = _tabManager->editorAt(i)) applyPreferencesToEditor(ed);
        }
    }

    // 主题/深色/字体会改样式槽位，需要重新套用语言样式与用户覆盖。
    if (force || themeChanged || darkChanged
        || old.fontName != prefs.fontName || old.fontSize != prefs.fontSize) {
        reloadStyleOverrides();
    }

    const bool sessionChanged = force
        || old.rememberSession != prefs.rememberSession
        || old.snapshotBackup != prefs.snapshotBackup
        || old.backupIntervalSec != prefs.backupIntervalSec;
    if (sessionChanged) {
        restartBackupTimer();
        // 关掉会话快照后，内存里的备份文件已经没人引用了，顺手清掉。
        if (!prefs.rememberSession || !prefs.snapshotBackup) {
            for (int i = 0; i < _tabManager->count(); ++i)
                dropBackup(_tabManager->editorAt(i));
        }
    }
}

void MainWindow::applyPreferencesToEditor(EditorView* ed) {
    if (!ed) return;
    if (ed->getEditorFontName() != _prefs.fontName
        || ed->getEditorFontSize() != _prefs.fontSize) {
        ed->setEditorFont(_prefs.fontName, _prefs.fontSize);
    }
    ed->send(SCI_SETFONTQUALITY, _prefs.fontSmooth
        ? SC_EFF_QUALITY_LCD_OPTIMIZED : SC_EFF_QUALITY_NON_ANTIALIASED);
    ed->send(SCI_SETTABWIDTH, _prefs.tabWidth);
    ed->send(SCI_SETUSETABS, _prefs.tabToSpaces ? 0 : 1);
    ed->setWrapMode(_prefs.wordWrap);
    ed->setShowLineNumbers(_prefs.showLineNumbers);
    ed->send(SCI_SETMARGINWIDTHN, 1, 14);
    ed->send(SCI_SETMARGINWIDTHN, 2, _prefs.showFoldMargin ? 14 : 0);
    ed->setShowIndentGuides(_prefs.showIndentGuides);
    ed->setShowWhitespace(_prefs.showWhitespace);
    ed->setShowEOL(_prefs.showEOL);
    ed->setDarkMode(_prefs.darkMode);
    ed->setHighlightPrefs(_prefs.highlightMatchingWords, _prefs.highlightMatchingBraces,
                          _prefs.highlightMatchCase, _prefs.highlightMatchWholeWord);
    ed->setUrlClickable(_prefs.urlClickable);
    ed->setAutoCompletion(_prefs.autoCompletion, _prefs.autoCompletionMinChars,
                          _prefs.autoCompletionType, _prefs.autoCompletionIgnoreNumbers);
}

void MainWindow::loadPreferences() {
    QSettings s;
    _prefs.uiLanguage = s.value("prefs/uiLanguage", "zh_CN").toString();
    _prefs.darkMode = s.value("prefs/darkMode", false).toBool();
    _prefs.uiFontZh = s.value("prefs/uiFontZh", "").toString();
    _prefs.uiFontEn = s.value("prefs/uiFontEn", "").toString();

    _prefs.hideToolBar = s.value("prefs/hideToolBar", false).toBool();
    _prefs.tabHide = s.value("prefs/tabHide", false).toBool();
    _prefs.tabVertical = s.value("prefs/tabVertical", false).toBool();
    _prefs.tabLock = s.value("prefs/tabLock", false).toBool();
    _prefs.tabDoubleClickClose = s.value("prefs/tabDoubleClickClose", false).toBool();
    _prefs.tabQuitOnEmpty = s.value("prefs/tabQuitOnEmpty", false).toBool();
    _prefs.tabCompactLabelLen = qBound(0, s.value("prefs/tabCompactLabelLen", 0).toInt(), 257);
    _prefs.tabReduce = s.value("prefs/tabReduce", false).toBool();
    _prefs.tabInactiveColor = s.value("prefs/tabInactiveColor", false).toBool();
    _prefs.tabActiveTopBar = s.value("prefs/tabActiveTopBar", true).toBool();
    _prefs.tabShowCloseButton = s.value("prefs/tabShowCloseButton", true).toBool();

    _prefs.tabWidth = s.value("prefs/tabWidth", 4).toInt();
    _prefs.tabToSpaces = s.value("prefs/tabToSpaces", false).toBool();
    _prefs.wordWrap = s.value("prefs/wordWrap", false).toBool();
    _prefs.showLineNumbers = s.value("prefs/showLineNumbers", true).toBool();
    _prefs.showFoldMargin = s.value("prefs/showFoldMargin", true).toBool();
    _prefs.showIndentGuides = s.value("prefs/showIndentGuides", false).toBool();
    _prefs.showWhitespace = s.value("prefs/showWhitespace", false).toBool();
    _prefs.showEOL = s.value("prefs/showEOL", false).toBool();

    _prefs.fontName = s.value("prefs/fontName", "Monospace").toString();
    _prefs.fontSize = s.value("prefs/fontSize", 10).toInt();
    _prefs.fontSmooth = s.value("prefs/fontSmooth", true).toBool();

    _prefs.themeName = s.value("prefs/themeName", "Default").toString();

    _prefs.defaultEOL = s.value("prefs/defaultEOL", "Unix (LF)").toString();
    _prefs.defaultEncoding = s.value("prefs/defaultEncoding", "UTF-8").toString();
    _prefs.defaultLanguage = s.value("prefs/defaultLanguage", "Normal Text").toString();

    _prefs.autoCompletion = s.value("prefs/autoCompletion", true).toBool();
    _prefs.autoCompletionMinChars = s.value("prefs/autoCompletionMinChars", 2).toInt();
    _prefs.autoCompletionType = qBound(0, s.value("prefs/autoCompletionType", 0).toInt(), 1);
    _prefs.autoCompletionIgnoreNumbers = s.value("prefs/autoCompletionIgnoreNumbers", true).toBool();

    _prefs.highlightMatchingWords = s.value("prefs/highlightMatchingWords", true).toBool();
    _prefs.highlightMatchingBraces = s.value("prefs/highlightMatchingBraces", true).toBool();
    _prefs.highlightMatchCase = s.value("prefs/highlightMatchCase", true).toBool();
    _prefs.highlightMatchWholeWord = s.value("prefs/highlightMatchWholeWord", false).toBool();
    _prefs.urlClickable = s.value("prefs/urlClickable", true).toBool();

    _prefs.rememberSession = s.value("prefs/rememberSession", true).toBool();
    _prefs.snapshotBackup = s.value("prefs/snapshotBackup", true).toBool();
    _prefs.backupIntervalSec = s.value("prefs/backupIntervalSec", 7).toInt();
}

void MainWindow::savePreferences() {
    QSettings s;
    s.setValue("prefs/uiLanguage", _prefs.uiLanguage);
    s.setValue("prefs/darkMode", _prefs.darkMode);
    s.setValue("prefs/uiFontZh", _prefs.uiFontZh);
    s.setValue("prefs/uiFontEn", _prefs.uiFontEn);

    s.setValue("prefs/hideToolBar", _prefs.hideToolBar);
    s.setValue("prefs/tabHide", _prefs.tabHide);
    s.setValue("prefs/tabVertical", _prefs.tabVertical);
    s.setValue("prefs/tabLock", _prefs.tabLock);
    s.setValue("prefs/tabDoubleClickClose", _prefs.tabDoubleClickClose);
    s.setValue("prefs/tabQuitOnEmpty", _prefs.tabQuitOnEmpty);
    s.setValue("prefs/tabCompactLabelLen", _prefs.tabCompactLabelLen);
    s.setValue("prefs/tabReduce", _prefs.tabReduce);
    s.setValue("prefs/tabInactiveColor", _prefs.tabInactiveColor);
    s.setValue("prefs/tabActiveTopBar", _prefs.tabActiveTopBar);
    s.setValue("prefs/tabShowCloseButton", _prefs.tabShowCloseButton);

    s.setValue("prefs/tabWidth", _prefs.tabWidth);
    s.setValue("prefs/tabToSpaces", _prefs.tabToSpaces);
    s.setValue("prefs/wordWrap", _prefs.wordWrap);
    s.setValue("prefs/showLineNumbers", _prefs.showLineNumbers);
    s.setValue("prefs/showFoldMargin", _prefs.showFoldMargin);
    s.setValue("prefs/showIndentGuides", _prefs.showIndentGuides);
    s.setValue("prefs/showWhitespace", _prefs.showWhitespace);
    s.setValue("prefs/showEOL", _prefs.showEOL);

    s.setValue("prefs/fontName", _prefs.fontName);
    s.setValue("prefs/fontSize", _prefs.fontSize);
    s.setValue("prefs/fontSmooth", _prefs.fontSmooth);

    s.setValue("prefs/themeName", _prefs.themeName);

    s.setValue("prefs/defaultEOL", _prefs.defaultEOL);
    s.setValue("prefs/defaultEncoding", _prefs.defaultEncoding);
    s.setValue("prefs/defaultLanguage", _prefs.defaultLanguage);

    s.setValue("prefs/autoCompletion", _prefs.autoCompletion);
    s.setValue("prefs/autoCompletionMinChars", _prefs.autoCompletionMinChars);
    s.setValue("prefs/autoCompletionType", _prefs.autoCompletionType);
    s.setValue("prefs/autoCompletionIgnoreNumbers", _prefs.autoCompletionIgnoreNumbers);

    s.setValue("prefs/highlightMatchingWords", _prefs.highlightMatchingWords);
    s.setValue("prefs/highlightMatchingBraces", _prefs.highlightMatchingBraces);
    s.setValue("prefs/highlightMatchCase", _prefs.highlightMatchCase);
    s.setValue("prefs/highlightMatchWholeWord", _prefs.highlightMatchWholeWord);
    s.setValue("prefs/urlClickable", _prefs.urlClickable);

    s.setValue("prefs/rememberSession", _prefs.rememberSession);
    s.setValue("prefs/snapshotBackup", _prefs.snapshotBackup);
    s.setValue("prefs/backupIntervalSec", _prefs.backupIntervalSec);
}

// ---------------- 会话快照（session.xml + backup/）----------------

SessionFile MainWindow::captureSessionFile(EditorView* ed) const {
    SessionFile f;
    auto buf = ed->buffer();
    f.path = QString::fromStdString(buf->getFilePath());
    f.backupPath = QString::fromStdString(buf->getBackupFileName());
    f.lang = QString::fromStdString(ed->currentLanguage());
    f.encoding = QString::fromStdString(buf->getEncoding());
    f.caretPos = static_cast<int>(ed->send(SCI_GETCURRENTPOS));
    const auto anchor = ed->send(SCI_GETANCHOR);
    const auto caret = ed->send(SCI_GETCURRENTPOS);
    if (anchor != caret) {
        f.selStart = static_cast<int>(qMin(anchor, caret));
        f.selEnd = static_cast<int>(qMax(anchor, caret));
    } else {
        f.selStart = -1;
        f.selEnd = -1;
    }
    // 存“文档行号”而不是屏幕行号：开了自动换行时两者不同，换算后才不会错位。
    f.firstVisibleLine = static_cast<int>(
        ed->send(SCI_DOCLINEFROMVISIBLE, static_cast<uptr_t>(ed->send(SCI_GETFIRSTVISIBLELINE))));
    // 横向滚动偏移必须一起存：否则重开时以光标为准，“把光标带进视野”会把长行滚到中间，
    // 一行只看得见后半截（用户看到的就是“内容只恢复了一部分”）。
    f.xOffset = static_cast<int>(ed->send(SCI_GETXOFFSET));
    // 用 Scintilla 的 EOL 取值而不是 DocumentBuffer 的枚举，恢复时能原样回填。
    f.eolMode = static_cast<int>(ed->send(SCI_GETEOLMODE));
    f.newDocIndex = buf->getNewDocIndex();
    f.readOnly = ed->isReadOnly();
    return f;
}

QVector<SessionFile> MainWindow::collectSessionFiles(int& activeIndex) const {
    QVector<SessionFile> files;
    activeIndex = 0;
    const int cur = _tabManager->currentIndex();

    for (int i = 0; i < _tabManager->count(); ++i) {
        auto* ed = _tabManager->editorAt(i);
        if (!ed) continue;
        auto buf = ed->buffer();
        // 空白的未命名标签没有恢复价值（同上游：isUntitled && docLength()==0 直接跳过），
        // 否则每次启动都会把上次那个空 "new 1" 也恢复出来。
        if (buf->getFilePath().empty() && buf->getBackupFileName().empty()
            && ed->send(SCI_GETLENGTH) == 0) {
            continue;
        }
        if (i == cur) activeIndex = files.size();
        files.append(captureSessionFile(ed));
    }
    return files;
}

void MainWindow::saveSession() {
    if (!_prefs.rememberSession) return;
    int activeIndex = 0;
    const QVector<SessionFile> files = collectSessionFiles(activeIndex);
    SessionManager::write(files, activeIndex);
}

void MainWindow::syncBackups() {
    if (!_prefs.rememberSession || !_prefs.snapshotBackup) return;

    bool sessionChanged = false;
    for (int i = 0; i < _tabManager->count(); ++i) {
        auto* ed = _tabManager->editorAt(i);
        if (!ed) continue;
        auto buf = ed->buffer();
        const QString existing = QString::fromStdString(buf->getBackupFileName());

        if (!buf->isModified()) {
            // 已经回到“已保存”，备份作废。
            if (!existing.isEmpty()) {
                SessionManager::removeBackupFile(existing);
                buf->setBackupFileName(std::string());
                buf->setBackupDirty(false);
                sessionChanged = true;
            }
            continue;
        }

        // 空白未命名标签不落盘，避免 backup/ 里堆满空文件。
        if (existing.isEmpty() && buf->getFilePath().empty() && ed->send(SCI_GETLENGTH) == 0) {
            continue;
        }

        // 增量检查：自上次备份后若内容未发生新的改动，且备份文件已在盘上，跳过无谓重写
        if (!buf->isBackupDirty() && !existing.isEmpty() && QFile::exists(existing)) {
            continue;
        }

        QString target = existing;
        if (target.isEmpty()) {
            target = SessionManager::newBackupPath(QString::fromStdString(buf->getFileName()));
            if (target.isEmpty()) continue;
            buf->setBackupFileName(target.toStdString());
            sessionChanged = true;
        }
        if (SessionManager::writeBackup(target, ed->currentTextUtf8())) {
            buf->setBackupDirty(false);
        }
    }

    // 新的 backupFilePath 必须马上写进 session.xml：否则程序被强杀时 session 里没有这条
    // 记录，下次启动就找不到备份（这正是上游在创建备份文件后立即 saveCurrentSession 的原因）。
    if (sessionChanged) saveSession();
}

void MainWindow::dropBackup(EditorView* ed) {
    if (!ed) return;
    auto buf = ed->buffer();
    const std::string path = buf->getBackupFileName();
    if (path.empty()) return;
    SessionManager::removeBackupFile(QString::fromStdString(path));
    buf->setBackupFileName(std::string());
    buf->setBackupDirty(false);
}

void MainWindow::restartBackupTimer() {
    if (!_backupTimer) return;
    if (!_prefs.rememberSession || !_prefs.snapshotBackup) {
        _backupTimer->stop();
        return;
    }
    _backupTimer->start(_prefs.backupIntervalSec * 1000);
}

bool MainWindow::restoreSession() {
    if (!_prefs.rememberSession) return false;

    QVector<SessionFile> files;
    int activeIndex = 0;
    if (!SessionManager::read(files, activeIndex) || files.isEmpty()) return false;

    int restored = 0;
    int activeTab = -1;

    for (const SessionFile& f : files) {
        const bool hasBackup = _prefs.snapshotBackup && !f.backupPath.isEmpty()
                               && QFile::exists(f.backupPath);
        const bool hasFile = !f.path.isEmpty() && QFile::exists(f.path);
        // 文件已被删且没有备份：没东西可恢复（上游会建占位文档，这里直接跳过）。
        if (!hasBackup && !hasFile) continue;

        EditorView* ed = nullptr;
        if (hasBackup) {
            QFile backup(f.backupPath);
            if (!backup.open(QIODevice::ReadOnly)) continue;
            const QByteArray bytes = backup.readAll();

            ed = _tabManager->createNewTab();
            if (!ed) continue;
            auto buf = ed->buffer();
            // 真正的磁盘路径、编码、语言都由这里回填：备份内容本身只是 UTF-8 文本，
            // 下次保存时会按 buf 里的编码重新转码写回原文件。
            buf->setFilePath(f.path.toStdString());
            if (f.newDocIndex > 0) buf->setNewDocIndex(f.newDocIndex);
            if (!f.encoding.isEmpty()) buf->setEncoding(f.encoding.toStdString());
            ed->send(SCI_SETEOLMODE, f.eolMode);
            buf->setLineEnding(f.eolMode == SC_EOL_CRLF ? LineEnding::CRLF
                                : f.eolMode == SC_EOL_CR ? LineEnding::CR : LineEnding::LF);
            ed->setTextFromUtf8(std::string(bytes.constData(), static_cast<size_t>(bytes.size())));
            if (!f.lang.isEmpty()) ed->applyLanguage(f.lang.toStdString());
            buf->setBackupFileName(f.backupPath.toStdString());
            // 恢复出来的是“未保存的修改”，标签必须带 * 才对得上用户记忆里的状态。
            buf->setModified(true);
            _tabManager->updateTabTitle(_tabManager->indexOf(ed));
        } else {
            openFile(f.path); // 内部会顺带挂 inotify 监视、写入最近文件列表
            ed = _tabManager->currentEditor();
            if (!ed) continue;
            // 编码/语言是用户上次手动选过的，不能被按扩展名重新猜一遍覆盖掉。
            if (!f.encoding.isEmpty()) ed->buffer()->setEncoding(f.encoding.toStdString());
            if (!f.lang.isEmpty()) ed->applyLanguage(f.lang.toStdString());
        }

        ed->restoreViewState(f.caretPos, f.selStart, f.selEnd, f.firstVisibleLine, f.xOffset);
        ed->setReadOnly(f.readOnly);
        if (restored == activeIndex) activeTab = _tabManager->indexOf(ed);
        ++restored;
    }

    if (restored == 0) return false;
    if (activeTab >= 0) _tabManager->setCurrentIndex(activeTab);
    return true;
}

// ---------------- 内部更新 ----------------

void MainWindow::onActiveEditorChanged(EditorView* editor) {
    if (editor) {
        connect(editor, &EditorView::cursorPositionChanged, this, &MainWindow::onCursorChanged, Qt::UniqueConnection);
        connect(editor, &EditorView::lineEndingChanged, this, [this](LineEnding) {
            if (auto* cur = _tabManager->currentEditor()) {
                _statusLineEndingLabel->setText(QString::fromStdString(cur->buffer()->getLineEndingString()));
            }
        });
        connect(editor, &EditorView::languageChanged, this, [this](const QString& langId) {
            updateLanguageUI(langId);
        });

        if (_actReadOnly) _actReadOnly->setChecked(editor->isReadOnly());
        if (_actViewHex) _actViewHex->setChecked(_tabManager->isHexViewActive());
        updateStatusBar(editor);
        updateLanguageUI(QString::fromStdString(editor->currentLanguage()));
        updateEncodingUI(QString::fromStdString(editor->buffer()->getEncoding()));
        syncWindowTitle();
    }
}

void MainWindow::syncWindowTitle() {
    // 直接取当前标签页的文本（去掉修改标记 *）。
    // 标题栏已不再绘制这个文本，但 GNOME/KDE 任务栏悬停提示读的就是窗口标题，
    // 所以仍要保持它与当前标签一致，才能“鼠标移到任务栏上就知道正在编辑哪个文件”。
    const int idx = _tabManager->currentIndex();
    QString name = idx >= 0 ? _tabManager->tabText(idx) : QString();
    if (name.endsWith('*')) name.chop(1); // 去掉 TabManager 追加的修改标记 '*'
    setWindowTitle(name.isEmpty() ? QStringLiteral("Noda")
                                  : name + QStringLiteral(" - Noda"));
}

void MainWindow::onCursorChanged(int line, int col) {
    _statusPosLabel->setText(
        LocalizationManager::instance().translate(QStringLiteral("行: %1  列: %2")).arg(line).arg(col));
    if (auto* ed = _tabManager->currentEditor()) {
        auto len = ed->send(SCI_GETLENGTH);
        auto lineCount = ed->send(SCI_GETLINECOUNT);
        _statusLengthLabel->setText(
            LocalizationManager::instance().translate(QStringLiteral("长度: %1  行数: %2")).arg(len).arg(lineCount));
    }
}

void MainWindow::updateStatusBar(EditorView* editor) {
    if (!editor) return;
    auto buf = editor->buffer();
    if (_statusEncodingLabel) _statusEncodingLabel->setText(QString::fromStdString(buf->getEncoding()));
    if (_statusLineEndingLabel) _statusLineEndingLabel->setText(QString::fromStdString(buf->getLineEndingString()));
    auto len = editor->send(SCI_GETLENGTH);
    auto lineCount = editor->send(SCI_GETLINECOUNT);
    if (_statusLengthLabel)
        _statusLengthLabel->setText(
            LocalizationManager::instance().translate(QStringLiteral("长度: %1  行数: %2")).arg(len).arg(lineCount));
}

// 语言切换后按当前语言重刷状态栏；无打开的文档时也更新位置/长度两块占位文本。
void MainWindow::refreshStatusBarText() {
    EditorView* ed = _tabManager ? _tabManager->currentEditor() : nullptr;
    if (ed) {
        const sptr_t pos = ed->send(SCI_GETCURRENTPOS);
        const int line = static_cast<int>(ed->send(SCI_LINEFROMPOSITION, pos)) + 1;
        const int col = static_cast<int>(ed->send(SCI_GETCOLUMN, pos)) + 1;
        onCursorChanged(line, col);
        updateStatusBar(ed);
        updateLanguageUI(QString::fromStdString(ed->currentLanguage()));
        return;
    }
    if (_statusPosLabel)
        _statusPosLabel->setText(
            LocalizationManager::instance().translate(QStringLiteral("行: %1  列: %2")).arg(1).arg(1));
    if (_statusLengthLabel)
        _statusLengthLabel->setText(
            LocalizationManager::instance().translate(QStringLiteral("长度: %1  行数: %2")).arg(0).arg(1));
}

void MainWindow::onFileChangedOnDisk(const QString& path) {
    // 自身写盘触发的变更：忽略（双保险，WatchGuard 已在此期间解除监视）。
    if (_savingPaths.contains(path)) return;

    for (int i = 0; i < _tabManager->count(); ++i) {
        auto* ed = _tabManager->editorAt(i);
        if (ed && QString::fromStdString(ed->buffer()->getFilePath()) == path) {
            if (!ed->buffer()->isModified()) {
                // 未修改：静默自动重载（与 N++ 默认行为一致），不打断用户。
                ed->loadFromFile(path);
                if (ed == _tabManager->currentEditor()) updateStatusBar(ed);
            } else {
                auto reply = QMessageBox::question(this, "文件已被外部修改",
                    QString("文件 %1 已被外部程序修改。\n是否放弃本地未保存更改并重新载入？").arg(path),
                    QMessageBox::Yes | QMessageBox::No);
                if (reply == QMessageBox::Yes) {
                    ed->loadFromFile(path);
                }
            }
            // 外部工具以“删除重建/重命名”方式改文件会使 inotify watch 失效，此处重新挂上。
            if (QFile::exists(path) && !_fileWatcher->files().contains(path)) {
                _fileWatcher->addPath(path);
            }
            break;
        }
    }
}

void MainWindow::paintEvent(QPaintEvent* event) {
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
    QMainWindow::paintEvent(event);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    // 快照模式：退出不弹“是否保存”。未保存内容先落进 backup/，再写 session.xml，
    // 下次启动原样恢复——这正是“不保存也能恢复”的全部前提，
    // 与上游 isSnapshotMode() 下直接退出、不逐个询问的行为一致。
    if (_prefs.rememberSession && _prefs.snapshotBackup) {
        syncBackups();
        saveSession();
        event->accept();
        return;
    }

    // 只“记住会话”而没开快照时，会话内容必须在 closeAll() 之前抓取：
    // 那个调用会把标签全部删除，之后再取就只剩空表。
    QVector<SessionFile> snapshot;
    int activeIndex = 0;
    if (_prefs.rememberSession) snapshot = collectSessionFiles(activeIndex);

    if (!_tabManager->closeAll()) {
        event->ignore();
        return;
    }
    if (_prefs.rememberSession) SessionManager::write(snapshot, activeIndex);
    event->accept();
}
