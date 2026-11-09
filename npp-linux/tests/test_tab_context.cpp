#include "test_tab_context.h"

#include "EditorView.h"
#include "MainWindow.h"
#include "TabManager.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScopedPointer>
#include <QSignalSpy>
#include <QTabBar>
#include <QtTest>

namespace {

QAction* findAction(const QMenu* menu, const QString& text) {
    if (!menu) return nullptr;
    for (QAction* action : menu->actions()) {
        if (!action->isSeparator() && action->text() == text) return action;
    }
    return nullptr;
}

QMenu* findSubmenu(const QMenu* menu, const QString& text) {
    QAction* action = findAction(menu, text);
    return action ? action->menu() : nullptr;
}

// 顶层项按顺序取文本，分隔符统一记作 "<sep>"，便于整体比对顺序。
QStringList menuTexts(const QMenu* menu) {
    QStringList texts;
    for (QAction* action : menu->actions()) {
        texts << (action->isSeparator() ? QStringLiteral("<sep>") : action->text());
    }
    return texts;
}

void sendContextMenu(QWidget* target, const QPoint& localPos) {
    QContextMenuEvent event(QContextMenuEvent::Mouse, localPos,
                            target->mapToGlobal(localPos));
    QApplication::sendEvent(target, &event);
}

TabManager* tabsOf(MainWindow& window) {
    return window.findChild<TabManager*>();
}

} // namespace

void TestTabContext::rightClickSelectsTabAndEmitsRequest() {
    TabManager tabs;
    tabs.resize(900, 300);
    tabs.show();
    tabs.createNewTab();
    tabs.createNewTab();
    tabs.createNewTab();
    tabs.setCurrentIndex(0);

    QSignalSpy spy(&tabs, &TabManager::tabContextMenuRequested);
    QVERIFY(spy.isValid());

    const int target = 2;
    sendContextMenu(tabs.tabBar(), tabs.tabBar()->tabRect(target).center());

    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), target);
    QVERIFY2(spy.at(0).at(1).toPoint() != QPoint(),
             "必须带出可用于弹出的全局坐标");
    // 上游 WM_RBUTTONDOWN 的第一件事就是选中被点的标签。
    QCOMPARE(tabs.currentIndex(), target);
}

void TestTabContext::rightClickOnBlankAreaDoesNothing() {
    TabManager tabs;
    tabs.resize(900, 300);
    // QTabBar 默认 expanding=true，单个标签会铺满整条标签栏，根本不存在空白区。
    // 关闭扩展才能构造出「标签右侧的空白条带」这个被测场景。
    tabs.tabBar()->setExpanding(false);
    tabs.show();
    tabs.createNewTab();
    tabs.setCurrentIndex(0);

    QSignalSpy spy(&tabs, &TabManager::tabContextMenuRequested);
    // 标签栏只占标签那一段宽度，右侧空白属于 QTabWidget 自己（与 blankAreaActivated
    // 的双击判定同一几何前提），右键事件也就落在 QTabWidget 上。
    const QRect barRect = tabs.tabBar()->geometry();
    QVERIFY2(barRect.right() + 20 < tabs.width(), "标签栏右侧必须存在空白区");
    const QPoint blank(barRect.right() + 20, barRect.center().y());

    sendContextMenu(&tabs, blank);

    QCOMPARE(spy.count(), 0);
    QCOMPARE(tabs.currentIndex(), 0);
}

void TestTabContext::menuMatchesUpstreamStructure() {
    MainWindow window;
    TabManager* tabs = tabsOf(window);
    QVERIFY(tabs);
    QVERIFY(tabs->currentEditor());

    QScopedPointer<QMenu> menu(window.createTabContextMenu(tabs->currentIndex()));
    QVERIFY(menu);

    const QStringList expectedTop = {
        QStringLiteral("关闭"),
        QStringLiteral("关闭多个标签页"),
        QStringLiteral("保存"),
        QStringLiteral("另存为..."),
        QStringLiteral("打开所在文件夹"),
        QStringLiteral("重命名..."),
        QStringLiteral("移到回收站"),
        QStringLiteral("从磁盘重新载入"),
        QStringLiteral("打印..."),
        QStringLiteral("<sep>"),
        QStringLiteral("只读"),
        QStringLiteral("<sep>"),
        QStringLiteral("复制到剪贴板"),
        QStringLiteral("移动标签页"),
    };
    QCOMPARE(menuTexts(menu.data()), expectedTop);

    QMenu* closeMany = findSubmenu(menu.data(), QStringLiteral("关闭多个标签页"));
    QVERIFY(closeMany);
    QCOMPARE(menuTexts(closeMany),
             QStringList({QStringLiteral("关闭其他标签页"),
                          QStringLiteral("关闭左侧标签页"),
                          QStringLiteral("关闭右侧标签页"),
                          QStringLiteral("关闭所有未修改的标签页")}));

    QMenu* openInto = findSubmenu(menu.data(), QStringLiteral("打开所在文件夹"));
    QVERIFY(openInto);
    QCOMPARE(menuTexts(openInto),
             QStringList({QStringLiteral("在文件管理器中打开"),
                          QStringLiteral("在终端中打开"),
                          QStringLiteral("<sep>"),
                          QStringLiteral("用默认程序打开")}));

    QMenu* copyMenu = findSubmenu(menu.data(), QStringLiteral("复制到剪贴板"));
    QVERIFY(copyMenu);
    QCOMPARE(menuTexts(copyMenu),
             QStringList({QStringLiteral("复制完整路径"),
                          QStringLiteral("复制文件名"),
                          QStringLiteral("复制目录路径")}));

    QMenu* moveMenu = findSubmenu(menu.data(), QStringLiteral("移动标签页"));
    QVERIFY(moveMenu);
    QCOMPARE(menuTexts(moveMenu),
             QStringList({QStringLiteral("移到最前"),
                          QStringLiteral("移到最后")}));
}

void TestTabContext::untitledTabDisablesPathActions() {
    MainWindow window;
    TabManager* tabs = tabsOf(window);
    QVERIFY(tabs);
    EditorView* ed = tabs->currentEditor();
    QVERIFY(ed);
    QVERIFY2(ed->buffer()->getFilePath().empty(), "用例前提：当前是无路径的新文档");

    QScopedPointer<QMenu> menu(window.createTabContextMenu(tabs->currentIndex()));
    QMenu* openInto = findSubmenu(menu.data(), QStringLiteral("打开所在文件夹"));
    QMenu* copyMenu = findSubmenu(menu.data(), QStringLiteral("复制到剪贴板"));

    QVERIFY(!findAction(openInto, QStringLiteral("在文件管理器中打开"))->isEnabled());
    QVERIFY(!findAction(openInto, QStringLiteral("用默认程序打开"))->isEnabled());
    QVERIFY(!findAction(menu.data(), QStringLiteral("重命名..."))->isEnabled());
    QVERIFY(!findAction(menu.data(), QStringLiteral("移到回收站"))->isEnabled());
    QVERIFY(!findAction(menu.data(), QStringLiteral("从磁盘重新载入"))->isEnabled());
    QVERIFY(!findAction(copyMenu, QStringLiteral("复制完整路径"))->isEnabled());
    QVERIFY(!findAction(copyMenu, QStringLiteral("复制目录路径"))->isEnabled());

    // 不依赖磁盘路径的动作仍必须可用。
    QVERIFY(findAction(menu.data(), QStringLiteral("保存"))->isEnabled());
    QVERIFY(findAction(menu.data(), QStringLiteral("另存为..."))->isEnabled());
    QVERIFY(findAction(menu.data(), QStringLiteral("打印..."))->isEnabled());
    QVERIFY(findAction(menu.data(), QStringLiteral("只读"))->isEnabled());
    QVERIFY(findAction(copyMenu, QStringLiteral("复制文件名"))->isEnabled());
}

void TestTabContext::readOnlyItemReflectsAndToggles() {
    MainWindow window;
    TabManager* tabs = tabsOf(window);
    QVERIFY(tabs);
    EditorView* ed = tabs->currentEditor();
    QVERIFY(ed);
    QCOMPARE(ed->isReadOnly(), false);

    {
        QScopedPointer<QMenu> menu(window.createTabContextMenu(tabs->currentIndex()));
        QAction* ro = findAction(menu.data(), QStringLiteral("只读"));
        QVERIFY(ro);
        QVERIFY(ro->isCheckable());
        QCOMPARE(ro->isChecked(), false);
        ro->trigger();
    }

    QCOMPARE(ed->isReadOnly(), true);

    // 菜单是每次右击重建的，勾选态必须来自编辑器当前状态而不是残留值。
    QScopedPointer<QMenu> menu(window.createTabContextMenu(tabs->currentIndex()));
    QAction* ro = findAction(menu.data(), QStringLiteral("只读"));
    QVERIFY(ro);
    QCOMPARE(ro->isChecked(), true);
}

void TestTabContext::closeToRightAppliesToClickedTab() {
    MainWindow window;
    TabManager* tabs = tabsOf(window);
    QVERIFY(tabs);
    while (tabs->count() < 3) tabs->createNewTab();
    QCOMPARE(tabs->count(), 3);

    EditorView* keep = tabs->editorAt(1);
    QVERIFY(keep);
    QPointer<EditorView> victim = tabs->editorAt(2);
    QVERIFY(victim);
    tabs->setCurrentIndex(0); // 构建菜单时当前标签不是被右击的那一个

    QScopedPointer<QMenu> menu(window.createTabContextMenu(1));
    QMenu* closeMany = findSubmenu(menu.data(), QStringLiteral("关闭多个标签页"));
    QAction* closeRight = findAction(closeMany, QStringLiteral("关闭右侧标签页"));
    QVERIFY(closeRight);
    QVERIFY(closeRight->isEnabled());
    closeRight->trigger();

    QCOMPARE(tabs->count(), 2);
    QVERIFY2(victim.isNull(), "被右击标签右侧的那一个必须被关闭");
    QCOMPARE(tabs->editorAt(1), keep);
    QCOMPARE(tabs->currentIndex(), 1);
}

void TestTabContext::moveItemsRespectEdgesAndReorder() {
    MainWindow window;
    TabManager* tabs = tabsOf(window);
    QVERIFY(tabs);
    while (tabs->count() < 3) tabs->createNewTab();
    QCOMPARE(tabs->count(), 3);

    EditorView* last = tabs->editorAt(2);
    QVERIFY(last);
    tabs->setCurrentIndex(0);

    QScopedPointer<QMenu> menu(window.createTabContextMenu(2));
    QMenu* moveMenu = findSubmenu(menu.data(), QStringLiteral("移动标签页"));
    QAction* toFirst = findAction(moveMenu, QStringLiteral("移到最前"));
    QAction* toLast = findAction(moveMenu, QStringLiteral("移到最后"));
    QVERIFY(toFirst);
    QVERIFY(toLast);
    QVERIFY(toFirst->isEnabled());
    QVERIFY2(!toLast->isEnabled(), "已在最右的标签不能再「移到最后」");

    toFirst->trigger();
    QCOMPARE(tabs->indexOf(last), 0);
    QCOMPARE(tabs->currentIndex(), 0);

    // 移到最前之后，这个标签的「移到最前」必须变为禁用。
    QScopedPointer<QMenu> menuAtZero(window.createTabContextMenu(0));
    QMenu* moveAtZero = findSubmenu(menuAtZero.data(), QStringLiteral("移动标签页"));
    QVERIFY(!findAction(moveAtZero, QStringLiteral("移到最前"))->isEnabled());
    QVERIFY(findAction(moveAtZero, QStringLiteral("移到最后"))->isEnabled());
}

void TestTabContext::closeModifiedTabPromptHasChineseButtons() {
    MainWindow window;
    TabManager* tabs = tabsOf(window);
    QVERIFY(tabs);
    auto* ed = tabs->currentEditor();
    QVERIFY(ed);

    // 修改文档产生脏状态
    const char text[] = "unsaved changes";
    ed->send(SCI_APPENDTEXT, sizeof(text) - 1, reinterpret_cast<sptr_t>(text));
    QVERIFY(ed->buffer()->isModified());

    QString yesText, noText, cancelText;
    QTimer::singleShot(0, [&window, &yesText, &noText, &cancelText]() {
        const auto boxes = window.findChildren<QMessageBox*>();
        for (auto* box : boxes) {
            for (auto* b : box->buttons()) {
                QString t = b->text();
                if (t.contains(QStringLiteral("是"))) yesText = t;
                if (t.contains(QStringLiteral("否"))) noText = t;
                if (t.contains(QStringLiteral("取消"))) cancelText = t;
            }
            box->reject();
        }
    });

    bool closed = tabs->closeTab(0);
    QVERIFY(!closed);
    QVERIFY2(yesText.contains(QStringLiteral("是")), "保存按钮必须包含中文'是'");
    QVERIFY2(noText.contains(QStringLiteral("否")), "不保存按钮必须包含中文'否'");
    QVERIFY2(cancelText.contains(QStringLiteral("取消")), "取消按钮必须包含中文'取消'");
}

