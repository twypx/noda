#include "test_folder_tree.h"
#include "widgets/FolderTreePane.h"
#include "utils/ThemeManager.h"

#include <QtTest>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileSystemModel>
#include <QHoverEvent>
#include <QImage>
#include <QSignalSpy>
#include <QStyle>
#include <QStyleOptionViewItem>
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

void TestFolderTree::singleClickTogglesFolderExpansion() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(makeDir(dir.filePath("sub")));
    QVERIFY(touch(dir.filePath("sub/child.txt")));

    FolderTreePane pane;
    QVERIFY(pane.setRootPath(dir.path()));
    pane.resize(600, 400);
    pane.show();
    QTRY_VERIFY(pane.model()->rowCount(pane.rootIndex()) >= 1);

    const QModelIndex sub = findChildByName(pane.model(), pane.rootIndex(), QStringLiteral("sub"));
    QVERIFY(sub.isValid());
    QVERIFY(pane.model()->hasChildren(sub));
    QVERIFY(!pane.view()->isExpanded(sub));

    const QPoint rowPoint = pane.view()->visualRect(sub).center();
    // 点在行内容上（不是左侧缩进/箭头区），右侧该展开。
    QTest::mouseClick(pane.view()->viewport(), Qt::LeftButton, Qt::NoModifier, rowPoint);
    QTRY_VERIFY2(pane.view()->isExpanded(sub), "单击文件夹行必须展开子节点");

    QTest::mouseClick(pane.view()->viewport(), Qt::LeftButton, Qt::NoModifier, rowPoint);
    QTRY_VERIFY2(!pane.view()->isExpanded(sub), "再次单击同一文件夹必须收起");
}

void TestFolderTree::doubleClickTogglesFolderOnlyOnce() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(makeDir(dir.filePath("sub")));
    QVERIFY(touch(dir.filePath("sub/child.txt")));

    FolderTreePane pane;
    QVERIFY(pane.setRootPath(dir.path()));
    pane.resize(600, 400);
    pane.show();
    QTRY_VERIFY(pane.model()->rowCount(pane.rootIndex()) >= 1);

    const QModelIndex sub = findChildByName(pane.model(), pane.rootIndex(), QStringLiteral("sub"));
    QVERIFY(sub.isValid());
    const QPoint rowPoint = pane.view()->visualRect(sub).center();

    // 真实双击 = 第一次 press/release 切换 + 第二次以 MouseButtonDblClick 到达。
    // 第二次不能再切换，否则用户看到的是「展开后立刻收起」。
    QTest::mouseClick(pane.view()->viewport(), Qt::LeftButton, Qt::NoModifier, rowPoint);
    QVERIFY2(pane.view()->isExpanded(sub), "双击的第一下应先展开");
    QTest::mouseDClick(pane.view()->viewport(), Qt::LeftButton, Qt::NoModifier, rowPoint);
    QTRY_VERIFY2(pane.view()->isExpanded(sub), "双击文件夹应保持展开，不能来回抵消");
}

void TestFolderTree::hoverDoesNotShiftItemContents() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(touch(dir.filePath("note.txt")));

    FolderTreePane pane;
    QVERIFY(pane.setRootPath(dir.path()));
    pane.resize(600, 400);
    pane.setStyleSheet(ThemeManager::getStyleSheet(false));
    pane.show();
    QTRY_VERIFY(pane.model()->rowCount(pane.rootIndex()) >= 1);

    const QModelIndex idx = findChildByName(pane.model(), pane.rootIndex(),
                                           QStringLiteral("note.txt"));
    QVERIFY(idx.isValid());

    QStyleOptionViewItem opt;
    opt.initFrom(pane.view());
    opt.rect = pane.view()->visualRect(idx);
    opt.state |= QStyle::State_Enabled;
    const QRect normal = pane.view()->style()->subElementRect(
        QStyle::SE_ItemViewItemText, &opt, pane.view());

    opt.state |= QStyle::State_MouseOver;
    const QRect hovered = pane.view()->style()->subElementRect(
        QStyle::SE_ItemViewItemText, &opt, pane.view());

    QCOMPARE(hovered, normal);
}

void TestFolderTree::branchAreaDrawsConnectorLines() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(makeDir(dir.filePath("sub")));
    QVERIFY(touch(dir.filePath("sub/child.txt")));

    FolderTreePane pane;
    QVERIFY(pane.setRootPath(dir.path()));
    pane.resize(320, 200);
    pane.show();
    QTRY_VERIFY(pane.model()->rowCount(pane.rootIndex()) >= 1);

    const QModelIndex sub = findChildByName(pane.model(), pane.rootIndex(), QStringLiteral("sub"));
    QVERIFY(sub.isValid());
    pane.view()->expand(sub);
    QTRY_VERIFY(pane.model()->rowCount(sub) >= 1);

    const QModelIndex child = pane.model()->index(0, 0, sub);
    QVERIFY(child.isValid());
    QTRY_VERIFY(pane.view()->visualRect(child).isValid());

    QImage image(pane.view()->viewport()->size(), QImage::Format_ARGB32);
    image.fill(Qt::magenta);
    pane.view()->viewport()->render(&image);

    const QColor background = image.pixelColor(image.width() - 1, image.height() - 1);
    const QRect row = pane.view()->visualRect(child);
    const int branchWidth = 2 * pane.view()->indentation();

    auto sameColor = [](const QColor& a, const QColor& b) {
        return qAbs(a.red() - b.red()) <= 4 && qAbs(a.green() - b.green()) <= 4
            && qAbs(a.blue() - b.blue()) <= 4;
    };

    bool ink = false;
    for (int y = row.top(); y <= row.bottom() && !ink; ++y) {
        for (int x = 0; x <= branchWidth && x < image.width(); ++x) {
            if (!sameColor(image.pixelColor(x, y), background)) {
                ink = true;
                break;
            }
        }
    }
    QVERIFY2(ink, "子节点缩进区必须画出连接线");
}

void TestFolderTree::hoverPaintsFullRowIncludingIndent() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(touch(dir.filePath("note.txt")));

    FolderTreePane pane;
    QVERIFY(pane.setRootPath(dir.path()));
    pane.resize(320, 200);
    pane.setStyleSheet(ThemeManager::getStyleSheet(false));
    pane.show();
    QTRY_VERIFY(pane.model()->rowCount(pane.rootIndex()) >= 1);

    const QModelIndex index = findChildByName(pane.model(), pane.rootIndex(),
                                             QStringLiteral("note.txt"));
    QVERIFY(index.isValid());
    const QPoint rowPoint = pane.view()->visualRect(index).center();
    QHoverEvent hover(QEvent::HoverMove, QPointF(rowPoint), QPointF(rowPoint));
    QApplication::sendEvent(pane.view()->viewport(), &hover);

    QImage image(pane.view()->viewport()->size(), QImage::Format_ARGB32);
    image.fill(Qt::magenta);
    pane.view()->viewport()->render(&image);

    const QColor expected = ThemeManager::folderTreeColors().hover;
    QCOMPARE(image.pixelColor(2, rowPoint.y()), expected);
}

void TestFolderTree::dockTitleCloseButtonUsesLucideIcon() {
    const QString dark = ThemeManager::getStyleSheet(true);
    const QString light = ThemeManager::getStyleSheet(false);

    // 与标签页关闭按钮使用同一枚 lucide x；平台默认的 X 不再出现。
    QVERIFY2(dark.contains(QStringLiteral("titlebar-close-icon: url(:/toolbar/dark/x_")),
             "深色 Dock 标题栏关闭按钮必须使用 lucide x 图标");
    QVERIFY2(light.contains(QStringLiteral("titlebar-close-icon: url(:/toolbar/light/x_")),
             "浅色 Dock 标题栏关闭按钮必须使用 lucide x 图标");
}
