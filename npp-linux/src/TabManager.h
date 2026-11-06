#pragma once

#include <QTabWidget>
#include <QStringList>
#include "EditorView.h"

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
    void tabTitleChanged(int index);

private slots:
    void onTabCloseRequested(int index);
    void onCurrentChanged(int index);

private:
    int _newDocCounter{1};
    QStringList _closedFilesStack;
};
