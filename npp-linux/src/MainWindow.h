#pragma once

#include <QMainWindow>
#include <QLabel>
#include <QFileSystemWatcher>
#include <QActionGroup>
#include <QSettings>
#include <QSet>
#include <QVector>
#include <QPair>
#include <map>
#include <functional>
#include <memory>
#include <string>
#include <QStringList>
#include <QPointer>
#include <QFutureWatcher>
#include "TabManager.h"
#include "SessionManager.h"
#include "dialogs/FindReplaceDialog.h"
#include "dialogs/GoToLineDialog.h"
#include "dialogs/ColumnEditorDialog.h"
#include "dialogs/DocSummaryDialog.h"
#include "dialogs/PreferencesDialog.h"
#include "dialogs/ShortcutsDialog.h"
#include "dialogs/StyleConfigDialog.h"
#include "widgets/FolderTreePane.h"

class QToolBar;
class QTimer;
class QMenu;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QUrl;

class ClickableLabel : public QLabel {
    Q_OBJECT
public:
    explicit ClickableLabel(const QString& text, QWidget* parent = nullptr) : QLabel(text, parent) {}
signals:
    void doubleClicked();
protected:
    void mouseDoubleClickEvent(QMouseEvent*) override { emit doubleClicked(); }
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void openFile(const QString& filePath);

    // 设置左侧目录树的根并显示它；从 onOpenFolder() 与回归测试共用同一入口，
    // 测试因此可以绕开 QFileDialog 的模态对话框。失败返回 false。
    bool openFolderPath(const QString& path);

    // 按 index 构造标签右键菜单但不弹出。正常路径由 showTabContextMenu() 调用，
    // 回归测试直接检查菜单结构与启用/勾选状态。调用方负责 delete。
    QMenu* createTabContextMenu(int index);

signals:
    // 保存/拷贝另存为失败：path 是目标路径，reason 是 EditorView::lastSaveError() 的具体原因。
    // 发出信号的同时会弹提示框；信号便于回归测试观测，也便于后续接状态栏。
    void saveFailed(const QString& path, const QString& reason);

protected:
    void closeEvent(QCloseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    // 文件菜单
    void onNewFile();
    void onOpenFile();
    void onOpenFolder();
    void onOpenFolderInFileManager();
    void onOpenFolderInTerminal();
    void onReloadFile();
    void onSaveFile();
    void onSaveAsFile();
    void onSaveCopyAs();
    void onSaveAllFiles();
    void onRenameFile();
    void onDeleteFileFromDisk();
    // 标签右键菜单的「移到回收站」：可恢复删除，对应上游 Move to Recycle Bin。
    void onMoveToTrashFile();
    void onCloseFile();
    void onCloseAllFiles();
    void onCloseAllExceptCurrent();
    void onCloseAllToLeft();
    void onCloseAllToRight();
    void onCloseAllUnchanged();
    void onRestoreClosedFile();
    void onPrint();
    void onOpenRecentFile();
    void onClearRecentFiles();

    // 编辑菜单
    void onUndo();
    void onRedo();
    void onCut();
    void onCopy();
    void onPaste();
    void onDelete();
    void onSelectAll();
    void onInsertDateTime(bool isLong);
    void onCopyFilePath();
    void onCopyFileName();
    void onCopyDir();
    void onCopyAllFileNames();
    void onCopyAllFilePaths();
    void onIndent();
    void onUnindent();
    void onToUpperCase();
    void onToLowerCase();
    void onToTitleCase();
    void onInvertCase();
    void onDuplicateCurrentLine();
    void onDeleteCurrentLine();
    void onMoveLineUp();
    void onMoveLineDown();
    void onJoinLines();
    void onSplitLines();
    void onSortLines(bool ascending, bool ignoreCase);
    void onRemoveDuplicateLines();
    void onReverseLines();
    void onInsertEmptyLine(bool above);
    void onToggleLineComment();
    void onBlockComment();
    void onUnblockComment();
    void onTrimTrailingSpaces();
    void onTrimLeadingSpaces();
    void onTrimBoth();
    void onTabsToSpaces();
    void onSpacesToTabs();
    void onRemoveEmptyLines(bool onlyBlank);
    void onToggleReadOnly(bool ro);
    void onFormatCode();
    void onFormatFinished();

    // 列选模式与列编辑器
    void onBeginEndSelect(bool columnMode); // 两点定块：第一次记起点，第二次拉出选区
    void onColumnModeTip();
    void onColumnEditor();

    // 搜索菜单
    void onFind();
    void onReplace();
    void onFindNext();
    void onFindPrev();
    void onQuickFindNext();
    void onQuickFindPrev();
    void onGoToLine();
    void onGoToMatchingBrace();
    void onSelectToMatchingBrace();
    void onToggleBookmark();
    void onNextBookmark();
    void onPrevBookmark();
    void onClearAllBookmarks();
    void onCopyBookmarkedLines();
    void onCutBookmarkedLines();
    void onDeleteBookmarkedLines();
    void onDeleteNonBookmarkedLines();
    void onInverseBookmarks();
    void onMarkText(int styleIndex);
    void onClearMarks();

    // 视图菜单
    void onToggleFullScreen(bool fs);
    void onToggleAlwaysOnTop(bool top);
    void onToggleWordWrap(bool wrap);
    void onToggleShowWhitespace(bool show);
    void onToggleShowEOL(bool show);
    void onToggleShowAllChars(bool show);
    void onToggleShowIndentGuides(bool show);
    void onZoomIn();
    void onZoomOut();
    void onZoomReset();
    void onFoldCurrent();
    void onUnfoldCurrent();
    void onFoldAll();
    void onUnfoldAll();
    void onFoldLevel(int level);
    void onUnfoldLevel(int level);
    void onNextTab();
    void onPrevTab();
    void onFirstTab();
    void onLastTab();
    void onMoveTabLeft();
    void onMoveTabRight();
    void onShowDocSummary();
    void onToggleDarkMode(bool enabled);

    // 编码菜单
    void onSetEncoding(const QString& enc);
    void onConvertToEncoding(const QString& enc);
    void onConvertLineEnding(LineEnding le);
    void onToggleLineEnding();

    // 语言菜单
    void onLanguageActionTriggered(QAction* action);

    // 设置菜单
    void onPreferences();
    void onStyleConfigurator();
    void onShortcutsMapper();

    // 内部事件
    void onActiveEditorChanged(EditorView* editor);
    void onCursorChanged(int line, int col);
    void onFileChangedOnDisk(const QString& path);

private:
    // 标签右键菜单的弹出入口；菜单项全部通过 createTabContextMenu() 构建。
    void showTabContextMenu(int index, const QPoint& globalPos);

    // 保存失败统一处理：发信号 + 带具体原因的提示，绝不假装成功。
    void reportSaveFailure(const QString& path, EditorView* ed);

    void setupMenus();
    void setupFolderTree();
    void setupTitleBar();
    void setupFileMenu();
    void setupEditMenu();
    void setupSearchMenu();
    void setupViewMenu();
    void setupEncodingMenu();
    void setupLanguageMenu();
    void setupSettingsMenu();
    void setupToolBar();
    void setupStatusBar();

    void updateStatusBar(EditorView* editor);
    // 让窗口标题始终与当前标签页一致（任务栏悬停提示靠它，标题栏自身已不再绘制文件名）。
    void syncWindowTitle();
    // 主题切换时工具栏图标的前景色也要跟着换（PNG 是离屏生成的两套配色，QSS 改不了 QIcon）。
    void refreshToolBarIcons();
    void updateEncodingUI(const QString& enc);
    void updateLanguageUI(const QString& langId);
    void setLanguage(const QString& langId);
    void applyDarkModePalette(bool dark);
    void applyPreferences(const AppPreferences& prefs);
    // 视图类开关（自动换行 / 显示符号 / 缩进参考线）是全局持续状态，对所有标签页生效，
    // 不能只改当前编辑器，否则切到另一个标签页就发现“没生效”。
    void forEachEditor(const std::function<void(EditorView*)>& fn);
    void loadPreferences();
    void savePreferences();

    // 将 QSettings 中持久化的“样式配置器”覆盖回灌到 StyleManager 并重绘所有编辑器。
    // Q_INVOKABLE 让回归测试能通过元对象验证持久化值真的作用到编辑器。
    Q_INVOKABLE void reloadStyleOverrides();

    void loadRecentFiles();
    void addRecentFile(const QString& path);
    void updateRecentFilesMenu();
    void applyPreferencesToEditor(EditorView* ed);
    void openDroppedUrl(const QUrl& url);

    friend class TestMainWindow;

    // ---- 会话快照（session.xml + backup/）----
    // 复刻 Notepad++ 的“记住上次会话 + 周期性备份”：退出时不弹保存询问，
    // 脏内容落盘到 backup/，下次启动按 session.xml 原样恢复。
    bool restoreSession();
    void saveSession();
    QVector<SessionFile> collectSessionFiles(int& activeIndex) const;
    Q_INVOKABLE void syncBackups();
    void dropBackup(EditorView* ed);
    SessionFile captureSessionFile(EditorView* ed) const;
    void restartBackupTimer();

    TabManager* _tabManager{nullptr};
    // 左侧目录树：既是 QDockWidget 也是数据/视图的持有者，初始隐藏。
    FolderTreePane* _folderTree{nullptr};
    QAction* _actFolderTree{nullptr};
    FindReplaceDialog* _findDialog{nullptr};
    QFileSystemWatcher* _fileWatcher{nullptr};

    // 本程序正在写盘的路径，用于屏蔽 inotify 自触发导致的“文件被外部修改”假警报。
    QSet<QString> _savingPaths;

    AppPreferences _prefs;
    bool _isDarkMode{false};
    // 周期性把脏缓冲区写成备份的定时器（同上游 launchDocumentBackupTask 的睡眠线程）。
    QTimer* _backupTimer{nullptr};

    // 编码菜单单选管理
    QActionGroup* _encodingGroup{nullptr};
    std::map<QString, QAction*> _encodingActionMap;

    // 语言菜单管理
    QActionGroup* _langActionGroup{nullptr};
    std::map<QString, QAction*> _langActionMap;

    // 最近打开文件菜单
    QMenu* _recentFilesMenu{nullptr};
    QStringList _recentFileList;

    // 视图菜单复选动作
    QAction* _actWrap{nullptr};
    QAction* _actShowWs{nullptr};
    QAction* _actShowEol{nullptr};
    QAction* _actShowAll{nullptr};   // "显示所有字符" = 空格/制表符 + 换行符 两个开关同时置位
    QAction* _actShowGuides{nullptr};
    QAction* _actAlwaysOnTop{nullptr};
    QAction* _actReadOnly{nullptr};
    QAction* _actDarkMode{nullptr};

    // 状态栏控件
    QLabel* _statusPosLabel{nullptr};
    ClickableLabel* _statusEncodingLabel{nullptr};
    ClickableLabel* _statusLineEndingLabel{nullptr};
    QLabel* _statusLengthLabel{nullptr};
    QLabel* _statusLangLabel{nullptr};

    // 工具栏：动作与图标基名成对保存，便于主题切换时批量重建 QIcon。
    QToolBar* _toolBar{nullptr};
    QVector<QPair<QAction*, QString>> _toolActions;

    // 列编辑器的上次参数：重开对话框时保留输入（同 Notepad++）。
    ColumnEditorParams _colEditParams;

    // 代码格式化（AStyle）：耗时的格式化放工作线程，同一时刻只跑一个任务。
    // _formatInput 与任务共享同一份输入快照，完成时用它比对文档是否被改动过。
    bool _formatBusy{false};
    QFutureWatcher<std::string>* _formatWatcher{nullptr};
    QPointer<EditorView> _formatTarget;
    std::shared_ptr<const std::string> _formatInput;
};
