#include "TabManager.h"
#include <QMessageBox>
#include <QTabBar>

TabManager::TabManager(QWidget* parent) : QTabWidget(parent) {
    setTabsClosable(true);
    setMovable(true);
    setDocumentMode(false);

    connect(this, &QTabWidget::tabCloseRequested, this, &TabManager::onTabCloseRequested);
    connect(this, &QTabWidget::currentChanged, this, &TabManager::onCurrentChanged);
}

EditorView* TabManager::currentEditor() const {
    return qobject_cast<EditorView*>(currentWidget());
}

EditorView* TabManager::editorAt(int index) const {
    return qobject_cast<EditorView*>(widget(index));
}

EditorView* TabManager::createNewTab() {
    auto* editor = new EditorView(this);
    editor->buffer()->setNewDocIndex(_newDocCounter++);

    int idx = addTab(editor, QString::fromStdString(editor->buffer()->getFileName()));
    setCurrentIndex(idx);

    connect(editor, &EditorView::documentModified, this, [this, editor](bool) {
        int i = indexOf(editor);
        if (i >= 0) {
            updateTabTitle(i);
        }
    });

    emit editorCreated(editor);
    return editor;
}

EditorView* TabManager::openFile(const QString& filePath) {
    // 检查是否已经打开
    for (int i = 0; i < count(); ++i) {
        auto* ed = editorAt(i);
        if (ed && QString::fromStdString(ed->buffer()->getFilePath()) == filePath) {
            setCurrentIndex(i);
            return ed;
        }
    }

    // 如果当前只有一个且未修改的空标签，直接在此打开
    if (count() == 1) {
        auto* firstEd = editorAt(0);
        if (firstEd && firstEd->buffer()->getFilePath().empty() && !firstEd->buffer()->isModified()) {
            if (firstEd->loadFromFile(filePath)) {
                updateTabTitle(0);
                return firstEd;
            }
        }
    }

    auto* editor = new EditorView(this);
    if (!editor->loadFromFile(filePath)) {
        delete editor;
        return nullptr;
    }

    int idx = addTab(editor, QString::fromStdString(editor->buffer()->getFileName()));
    setCurrentIndex(idx);

    connect(editor, &EditorView::documentModified, this, [this, editor](bool) {
        int i = indexOf(editor);
        if (i >= 0) {
            updateTabTitle(i);
        }
    });

    emit editorCreated(editor);
    return editor;
}

void TabManager::updateTabTitle(int index) {
    auto* ed = editorAt(index);
    if (!ed) return;

    QString title = QString::fromStdString(ed->buffer()->getFileName());
    if (ed->buffer()->isModified()) {
        title += "*";
    }
    // 只在文本真正变化时写入，避免重复触发标题栏无效刷新。
    if (tabText(index) != title) {
        setTabText(index, title);
        emit tabTitleChanged(index);
    }
    setTabToolTip(index, QString::fromStdString(ed->buffer()->getFilePath()));
}

bool TabManager::closeTab(int index) {
    auto* ed = editorAt(index);
    if (!ed) return false;

    if (ed->buffer()->isModified()) {
        QString msg = QString("是否保存对文件 %1 的更改？")
                          .arg(QString::fromStdString(ed->buffer()->getFileName()));
        auto reply = QMessageBox::question(this, "保存", msg,
                                            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        if (reply == QMessageBox::Save) {
            if (!ed->saveToFile()) {
                return false;
            }
        } else if (reply == QMessageBox::Cancel) {
            return false;
        }
    }

    if (!ed->buffer()->getFilePath().empty()) {
        _closedFilesStack.append(QString::fromStdString(ed->buffer()->getFilePath()));
    }

    emit editorClosing(ed);
    removeTab(index);
    delete ed;

    if (count() == 0) {
        createNewTab();
    }
    return true;
}

bool TabManager::closeAll() {
    while (count() > 0) {
        if (!closeTab(count() - 1)) {
            return false;
        }
        if (count() == 1 && editorAt(0)->buffer()->getFilePath().empty() && !editorAt(0)->buffer()->isModified()) {
            break;
        }
    }
    return true;
}

bool TabManager::closeAllExceptCurrent() {
    auto* curEd = currentEditor();
    if (!curEd) return true;
    for (int i = count() - 1; i >= 0; --i) {
        if (editorAt(i) != curEd) {
            if (!closeTab(i)) {
                return false;
            }
        }
    }
    return true;
}

bool TabManager::closeAllToLeft() {
    int curIdx = currentIndex();
    for (int i = curIdx - 1; i >= 0; --i) {
        if (!closeTab(i)) {
            return false;
        }
    }
    return true;
}

bool TabManager::closeAllToRight() {
    int curIdx = currentIndex();
    for (int i = count() - 1; i > curIdx; --i) {
        if (!closeTab(i)) {
            return false;
        }
    }
    return true;
}

bool TabManager::closeAllUnchanged() {
    for (int i = count() - 1; i >= 0; --i) {
        auto* ed = editorAt(i);
        if (ed && !ed->buffer()->isModified() && !ed->buffer()->getFilePath().empty()) {
            closeTab(i);
        }
    }
    return true;
}

void TabManager::moveTabLeft() {
    int cur = currentIndex();
    if (cur > 0) {
        auto* w = widget(cur);
        QString title = tabText(cur);
        QString tooltip = tabToolTip(cur);
        removeTab(cur);
        insertTab(cur - 1, w, title);
        setTabToolTip(cur - 1, tooltip);
        setCurrentIndex(cur - 1);
    }
}

void TabManager::moveTabRight() {
    int cur = currentIndex();
    if (cur < count() - 1) {
        auto* w = widget(cur);
        QString title = tabText(cur);
        QString tooltip = tabToolTip(cur);
        removeTab(cur);
        insertTab(cur + 1, w, title);
        setTabToolTip(cur + 1, tooltip);
        setCurrentIndex(cur + 1);
    }
}

void TabManager::moveTabFirst() {
    int cur = currentIndex();
    if (cur > 0) {
        auto* w = widget(cur);
        QString title = tabText(cur);
        QString tooltip = tabToolTip(cur);
        removeTab(cur);
        insertTab(0, w, title);
        setTabToolTip(0, tooltip);
        setCurrentIndex(0);
    }
}

void TabManager::moveTabLast() {
    int cur = currentIndex();
    if (cur < count() - 1) {
        auto* w = widget(cur);
        QString title = tabText(cur);
        QString tooltip = tabToolTip(cur);
        removeTab(cur);
        addTab(w, title);
        setTabToolTip(count() - 1, tooltip);
        setCurrentIndex(count() - 1);
    }
}

bool TabManager::restoreClosedFile() {
    if (_closedFilesStack.isEmpty()) return false;
    QString path = _closedFilesStack.takeLast();
    return openFile(path) != nullptr;
}

void TabManager::onTabCloseRequested(int index) {
    closeTab(index);
}

void TabManager::onCurrentChanged(int index) {
    emit activeEditorChanged(editorAt(index));
}
