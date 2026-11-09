#include "test_folder_tree.h"
#include "widgets/FolderTreePane.h"
#include "utils/ThemeManager.h"

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTreeView>

namespace {

bool touch(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.close();   // Qt6 的 QFile::close() 返回 void
    return true;
}

bool makeDir(const QString& path) {
    return QDir().mkpath(path);
}

// 在根层子项里按名字找 index，避免依赖文件系统返回顺序。
QModelIndex findChildByName(QFileSystemModel* model, const QModelIndex& parent,
                            const QString& name) {
    const int rows = model->rowCount(parent);
    for (int r = 0; r < rows; ++r) {
        const QModelIndex idx = model->index(r, 0, parent);
        if (model->fileName(idx) == name) return idx;
    }
    return QModelIndex();
}

} // namespace

void TestFolderTree::iconProviderCachesBySuffix() {
    FolderTreeIconProvider provider;
    QVERIFY2(!provider.icon(QFileInfo(QStringLiteral("/tmp/a.cpp"))).isNull(),
             "文件图标不能为空");
    QVERIFY2(!provider.icon(QFileInfo(QStringLiteral("/tmp/folder"))).isNull(),
             "目录图标不能为空");

    provider.icon(QFileInfo(QStringLiteral("/tmp/one.cpp")));
    const int afterFirst = provider.cacheSize();
    provider.icon(QFileInfo(QStringLiteral("/tmp/two.cpp")));
    QCOMPARE(provider.cacheSize(), afterFirst);   // 同后缀命中缓存，不新增条目

    provider.icon(QFileInfo(QStringLiteral("/tmp/three.h")));
    QCOMPARE(provider.cacheSize(), afterFirst + 1); // 不同后缀各自缓存
}

void TestFolderTree::setRootPathAppliesRootIndexAndHidesExtraColumns() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(touch(dir.filePath("a.txt")));

    FolderTreePane pane;
    QVERIFY(pane.setRootPath(dir.path()));
    QCOMPARE(pane.rootPath(), QDir(dir.path()).absolutePath());
    QVERIFY(pane.rootIndex().isValid());
    QTRY_VERIFY(pane.model()->rowCount(pane.rootIndex()) >= 1);

    QVERIFY(!pane.view()->isColumnHidden(0));
    for (int column = 1; column <= 3; ++column)
        QVERIFY2(pane.view()->isColumnHidden(column), "只应显示 Name 列");
    QVERIFY(pane.view()->uniformRowHeights());
}

void TestFolderTree::doesNotRecursivelyPrefetchDeepDirectories() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString deep = dir.filePath("deep");
    QVERIFY(makeDir(deep));
    for (int i = 0; i < 1500; ++i)
        QVERIFY(touch(deep + QStringLiteral("/f%1.txt").arg(i)));
    QVERIFY(touch(dir.filePath("root.txt")));

    FolderTreePane pane;
    QVERIFY(pane.setRootPath(dir.path()));
    QTRY_VERIFY(pane.model()->rowCount(pane.rootIndex()) >= 2);

    const QModelIndex deepIndex = findChildByName(pane.model(), pane.rootIndex(),
                                                  QStringLiteral("deep"));
    QVERIFY(deepIndex.isValid());
    // 未被用户展开 => QFileSystemModel 尚未 fetchMore，深层 1500 个文件一个都没读。
    QCOMPARE(pane.model()->rowCount(deepIndex), 0);
    QVERIFY(pane.model()->canFetchMore(deepIndex));
}

void TestFolderTree::activateIndexEmitsOnlyForFiles() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(touch(dir.filePath("note.txt")));
    QVERIFY(makeDir(dir.filePath("subdir")));

    FolderTreePane pane;
    QVERIFY(pane.setRootPath(dir.path()));
    QTRY_VERIFY(pane.model()->rowCount(pane.rootIndex()) >= 2);

    const QModelIndex fileIndex = findChildByName(pane.model(), pane.rootIndex(),
                                                  QStringLiteral("note.txt"));
    const QModelIndex dirIndex = findChildByName(pane.model(), pane.rootIndex(),
                                                 QStringLiteral("subdir"));
    QVERIFY(fileIndex.isValid());
    QVERIFY(dirIndex.isValid());

    QSignalSpy spy(&pane, &FolderTreePane::fileActivated);
    QVERIFY(spy.isValid());
    pane.activateIndex(dirIndex);
    QCOMPARE(spy.count(), 0);          // 目录点击只展开，不打开文件
    pane.activateIndex(fileIndex);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QDir(dir.filePath("note.txt")).absolutePath());
}

void TestFolderTree::invalidRootPathIsRejectedWithoutStateChange() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    FolderTreePane pane;
    QVERIFY(pane.setRootPath(dir.path()));
    const QString saved = pane.rootPath();
    const QModelIndex savedIndex = pane.rootIndex();

    QVERIFY(!pane.setRootPath(QStringLiteral("/definitely/not/an/existing/folder")));
    QVERIFY(!pane.setRootPath(QString()));
    QCOMPARE(pane.rootPath(), saved);
    QCOMPARE(pane.rootIndex(), savedIndex);
}

void TestFolderTree::switchingRootUpdatesRootIndex() {
    QTemporaryDir first;
    QTemporaryDir second;
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());
    QVERIFY(touch(second.filePath("b.txt")));

    FolderTreePane pane;
    QVERIFY(pane.setRootPath(first.path()));
    const QModelIndex oldIndex = pane.rootIndex();
    QVERIFY(pane.setRootPath(second.path()));
    QVERIFY(pane.rootIndex() != oldIndex);
    QCOMPARE(pane.rootPath(), QDir(second.path()).absolutePath());
    QCOMPARE(pane.model()->filePath(pane.rootIndex()), QDir(second.path()).absolutePath());
}

void TestFolderTree::themeStyleSheetsCoverDockAndTree() {
    const QString dark = ThemeManager::getStyleSheet(true);
    const QString light = ThemeManager::getStyleSheet(false);
    QVERIFY2(dark.contains(QStringLiteral("QDockWidget")), "深色 QSS 缺少 QDockWidget 规则");
    QVERIFY2(dark.contains(QStringLiteral("QTreeView")), "深色 QSS 缺少 QTreeView 规则");
    QVERIFY2(light.contains(QStringLiteral("QDockWidget")), "浅色 QSS 缺少 QDockWidget 规则");
    QVERIFY2(light.contains(QStringLiteral("QTreeView")), "浅色 QSS 缺少 QTreeView 规则");
}

void TestFolderTree::toolbarIconResourcesExist() {
    const QStringList themes = {QStringLiteral("dark"), QStringLiteral("light")};
    const QStringList sizes = {QStringLiteral("16"), QStringLiteral("32")};
    for (const QString& theme : themes) {
        for (const QString& size : sizes) {
            const QString path = QStringLiteral(":/toolbar/%1/folder-tree_%2.png").arg(theme, size);
            QVERIFY2(QFile::exists(path), qPrintable(QStringLiteral("缺少工具栏图标 ") + path));
        }
    }
}
