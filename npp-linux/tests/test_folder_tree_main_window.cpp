#include "test_folder_tree_main_window.h"
#include "MainWindow.h"
#include "TabManager.h"
#include "EditorView.h"
#include "widgets/FolderTreePane.h"
#include "dialogs/ShortcutManager.h"

#include <QtTest>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QModelIndex>
#include <QTemporaryDir>

namespace {

bool touch(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.close();
    return true;
}

QAction* findAction(QWidget* root, const QString& cleanedName) {
    const auto actions = root->findChildren<QAction*>();
    for (QAction* action : actions) {
        if (ShortcutManager::cleanActionText(action->text()) == cleanedName)
            return action;
    }
    return nullptr;
}

QModelIndex childByName(QFileSystemModel* model, const QModelIndex& parent,
                        const QString& name) {
    const int rows = model->rowCount(parent);
    for (int r = 0; r < rows; ++r) {
        const QModelIndex idx = model->index(r, 0, parent);
        if (model->fileName(idx) == name) return idx;
    }
    return QModelIndex();
}

} // namespace

void TestFolderTreeMainWindow::fileMenuAndToolBarExposeOpenFolder() {
    MainWindow window;
    bool menuFound = false;
    bool toolFound = false;
    const auto actions = window.findChildren<QAction*>();
    for (QAction* action : actions) {
        if (ShortcutManager::cleanActionText(action->text()) != QStringLiteral("打开文件夹"))
            continue;
        if (action->parent() && action->parent()->inherits("QToolBar")) toolFound = true;
        else menuFound = true;
    }
    QVERIFY2(menuFound, "文件菜单必须有「打开文件夹」动作");
    QVERIFY2(toolFound, "工具栏必须有「打开文件夹」动作");
}

void TestFolderTreeMainWindow::dockStartsHidden() {
    MainWindow window;
    auto* pane = window.findChild<FolderTreePane*>();
    QVERIFY(pane);
    QCOMPARE(pane->objectName(), QStringLiteral("folderTreeDock"));
    QVERIFY2(pane->isHidden(), "未选择文件夹时目录树不应占用左侧空间");
}

void TestFolderTreeMainWindow::openFolderPathShowsDockAndSetsRoot() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(touch(dir.filePath("a.txt")));

    MainWindow window;
    QVERIFY(window.openFolderPath(dir.path()));
    auto* pane = window.findChild<FolderTreePane*>();
    QVERIFY(pane);
    QVERIFY(!pane->isHidden());
    QCOMPARE(pane->rootPath(), QDir(dir.path()).absolutePath());

    QAction* toggle = findAction(&window, QStringLiteral("文件夹树"));
    QVERIFY(toggle);
    QVERIFY(toggle->isChecked());
}

void TestFolderTreeMainWindow::activatingFileOpensItInMainArea() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString file = dir.filePath("note.txt");
    QVERIFY(touch(file));

    MainWindow window;
    QVERIFY(window.openFolderPath(dir.path()));
    auto* pane = window.findChild<FolderTreePane*>();
    QVERIFY(pane);
    QTRY_VERIFY(pane->model()->rowCount(pane->rootIndex()) >= 1);
    const QModelIndex fileIndex = childByName(pane->model(), pane->rootIndex(),
                                              QStringLiteral("note.txt"));
    QVERIFY(fileIndex.isValid());

    pane->activateIndex(fileIndex);

    auto* tabs = window.findChild<TabManager*>();
    QVERIFY(tabs);
    auto* editor = tabs->currentEditor();
    QVERIFY(editor);
    QCOMPARE(QString::fromStdString(editor->buffer()->getFilePath()),
             QDir(file).absolutePath());
}

void TestFolderTreeMainWindow::viewMenuToggleControlsDockVisibility() {
    MainWindow window;
    auto* pane = window.findChild<FolderTreePane*>();
    QVERIFY(pane);
    QAction* toggle = findAction(&window, QStringLiteral("文件夹树"));
    QVERIFY(toggle);
    QVERIFY(toggle->isCheckable());
    QVERIFY(pane->isHidden());

    toggle->setChecked(true);
    QVERIFY2(!pane->isHidden(), "勾选后目录树应显示");
    toggle->setChecked(false);
    QVERIFY2(pane->isHidden(), "取消勾选后目录树应隐藏");
}

void TestFolderTreeMainWindow::shortcutManagerSeesOpenFolderAction() {
    MainWindow window;
    const QList<ShortcutEntry> entries = ShortcutManager::instance().collectActions(&window);
    bool found = false;
    for (const auto& entry : entries) {
        if (entry.name == QStringLiteral("打开文件夹")) found = true;
    }
    QVERIFY2(found, "新动作必须自动出现在快捷键映射器里");
}
