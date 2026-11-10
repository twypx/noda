#pragma once

#include <QTabWidget>
#include <QStringList>
#include <QMap>
#include "EditorView.h"

class QMouseEvent;
class HexView;

class TabManager : public QTabWidget {
    Q_OBJECT
public:
    explicit TabManager(QWidget* parent = nullptr);

    EditorView* currentEditor() const;
    EditorView* editorAt(int index) const;

    EditorView* createNewTab();
    EditorView* openFile(const QString& filePath);
    bool closeTab(int index);

    // 依据“文件名 + 是否修改”刷新指定标签的文本（MainWindow 保存/另存/重命名后调用）。
    void updateTabTitle(int index);

    // 批量关闭
    bool closeAll();
    bool closeAllExceptCurrent();
    bool closeAllToLeft();
    bool closeAllToRight();
    bool closeAllUnchanged();

    // 标签页移动
    void moveTabLeft();
    void moveTabRight();
    void moveTabFirst();
    void moveTabLast();

    // 恢复最近关闭的文件
    bool restoreClosedFile();
    bool canRestoreClosedFile() const { return !_closedFilesStack.isEmpty(); }

    // 标签栏首选项（字段与上游 TabBar GUIConfig 对应）
    void setTabReduce(bool reduce);
    bool isTabReduce() const { return _tabReduce; }

    void setDoubleClickClose(bool enable) { _doubleClickClose = enable; }
    bool isDoubleClickClose() const { return _doubleClickClose; }

    void setTabBarHidden(bool hide);
    bool isTabBarHidden() const { return _tabBarHidden; }

    void setVerticalTabs(bool vertical);
    bool verticalTabs() const { return _verticalTabs; }

    void setActiveTopBar(bool draw);
    bool activeTopBar() const { return _activeTopBar; }

    void setInactiveColored(bool on);
    bool inactiveColored() const { return _inactiveColored; }

    void setCompactLabelLength(int length);
    int compactLabelLength() const { return _compactLabelLength; }

    void setQuitOnEmpty(bool on) { _quitOnEmpty = on; }
    bool quitOnEmpty() const { return _quitOnEmpty; }

    // 十六进制查看器支持
    HexView* currentHexView() const;
    HexView* hexViewAt(int index) const;
    bool isHexViewActive() const;
    void toggleHexView(bool enable);
    void setDarkMode(bool dark);

    // 首选项一次会改多个标签栏动态属性，包在批量区间里只做一次 unpolish/polish。
    void beginStyleBatch();
    void endStyleBatch();
    // 实际 polish 次数，供回归测试与样式抖动诊断。
    int styleRepolishCount() const { return _styleRepolishCount; }

signals:
    void activeEditorChanged(EditorView* editor);
    void hexViewToggled(bool active);
    void editorCreated(EditorView* editor);
    // 标签即将被销毁：会话快照的备份文件要在这时删掉，否则会变成无人引用的孤儿文件。
    void editorClosing(EditorView* editor);
    void tabTitleChanged(int index);
    // 双击标签栏空白处新建文档。真正新建文档由 MainWindow 做，
    // 这样才能带上首选项里的默认 EOL / 编码。
    void blankAreaActivated();
    // 右键点击某个标签：该标签已经被选中（同上游 WM_RBUTTONDOWN），globalPos 用于弹出菜单。
    // 标签栏空白处不触发，也不改变当前标签。
    void tabContextMenuRequested(int index, const QPoint& globalPos);
    // 上游 quitOnEmpty：最后一个标签被关闭时请求退出（由 MainWindow 延迟执行 close）。
    void quitOnEmptyRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;
    // 双击标签栏右侧的空白区（标签栏只占标签那一段宽度，右边空白落在 QTabWidget 上）。
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    // 拦截标签栏的 QEvent::ContextMenu：右键先选中被点的标签，再请求弹出菜单。
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onTabCloseRequested(int index);
    void onCurrentChanged(int index);

private:
    void scheduleTabBarRepolish();

    int _newDocCounter{1};
    QStringList _closedFilesStack;
    mutable QMap<EditorView*, HexView*> _hexViews;
    bool _isDarkMode{false};
    int _styleBatchDepth{0};
    bool _styleBatchDirty{false};
    int _styleRepolishCount{0};
    bool _tabReduce{false};
    bool _doubleClickClose{false};
    bool _tabBarHidden{false};
    bool _verticalTabs{false};
    bool _activeTopBar{true};
    bool _inactiveColored{false};
    int _compactLabelLength{0};
    bool _quitOnEmpty{false};
};
