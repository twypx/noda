#pragma once

#include <QTabWidget>
#include <QStringList>
#include "EditorView.h"

class QMouseEvent;

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

signals:
    void activeEditorChanged(EditorView* editor);
    void editorCreated(EditorView* editor);
    // 标签即将被销毁：会话快照的备份文件要在这时删掉，否则会变成无人引用的孤儿文件。
    void editorClosing(EditorView* editor);
    void tabTitleChanged(int index);
    // 双击标签栏空白处（复刻上游 Notepad++ 的同一手势）。真正新建文档由 MainWindow 做，
    // 这样才能带上首选项里的默认 EOL / 编码。
    void blankAreaActivated();
    // 右键点击某个标签：该标签已经被选中（同上游 WM_RBUTTONDOWN），globalPos 用于弹出菜单。
    // 标签栏空白处不触发，也不改变当前标签。
    void tabContextMenuRequested(int index, const QPoint& globalPos);

protected:
    // 双击标签栏右侧的空白区（标签栏只占标签那一段宽度，右边空白落在 QTabWidget 上）。
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    // 拦截标签栏的 QEvent::ContextMenu：右键先选中被点的标签，再请求弹出菜单。
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void onTabCloseRequested(int index);
    void onCurrentChanged(int index);

private:
    int _newDocCounter{1};
    QStringList _closedFilesStack;
};
