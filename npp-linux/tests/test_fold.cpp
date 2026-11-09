#include "test_fold.h"
#include "EditorView.h"
#include "MainWindow.h"

#include <QtTest>
#include <QAction>
#include <QMenu>

namespace {

// 不依赖 lexer，直接给 Scintilla 设置折叠层级，精确验证 fold/expand 状态机。
void makeFoldable(EditorView& ed) {
    ed.setTextFromUtf8("head\n  body\n  body2\nnext\n");
    ed.send(SCI_SETFOLDLEVEL, 0, SC_FOLDLEVELBASE | SC_FOLDLEVELHEADERFLAG);
    ed.send(SCI_SETFOLDLEVEL, 1, SC_FOLDLEVELBASE + 1);
    ed.send(SCI_SETFOLDLEVEL, 2, SC_FOLDLEVELBASE + 1);
    ed.send(SCI_SETFOLDLEVEL, 3, SC_FOLDLEVELBASE);
    const sptr_t bodyPos = ed.send(SCI_POSITIONFROMLINE, 1);
    ed.send(SCI_GOTOPOS, bodyPos);
}

} // namespace

void TestFold::foldAndExpandCurrentAreIdempotent() {
    EditorView ed;
    makeFoldable(ed);
    QVERIFY(ed.send(SCI_GETFOLDEXPANDED, 0) != 0);

    ed.foldCurrent();
    QCOMPARE(ed.send(SCI_GETFOLDEXPANDED, 0), sptr_t(0));
    ed.foldCurrent();   // 已折叠再点「折叠」不能反转
    QCOMPARE(ed.send(SCI_GETFOLDEXPANDED, 0), sptr_t(0));

    ed.expandCurrent();
    QCOMPARE(ed.send(SCI_GETFOLDEXPANDED, 0), sptr_t(1));
    ed.expandCurrent(); // 已展开再点「展开」不能反转
    QCOMPARE(ed.send(SCI_GETFOLDEXPANDED, 0), sptr_t(1));
}

void TestFold::foldAndUnfoldLevelAreComplementary() {
    EditorView ed;
    makeFoldable(ed);
    ed.foldLevel(1);
    QCOMPARE(ed.send(SCI_GETFOLDEXPANDED, 0), sptr_t(0));
    ed.unfoldLevel(1);
    QCOMPARE(ed.send(SCI_GETFOLDEXPANDED, 0), sptr_t(1));
}

void TestFold::viewMenuWiresAllUnfoldLevels() {
    MainWindow window;
    QMenu* foldMenu = nullptr;
    const auto menus = window.findChildren<QMenu*>();
    for (auto* menu : menus) {
        if (menu->title().startsWith(QStringLiteral("折叠层次"))) {
            foldMenu = menu;
            break;
        }
    }
    QVERIFY2(foldMenu, "必须存在「折叠层次」菜单");

    int foldLevels = 0;
    QMenu* unfoldMenu = nullptr;
    for (auto* action : foldMenu->actions()) {
        if (action->text().startsWith(QStringLiteral("折叠层次 "))) ++foldLevels;
        if (action->menu() && action->menu()->title().startsWith(QStringLiteral("展开层次")))
            unfoldMenu = action->menu();
    }
    QCOMPARE(foldLevels, 8);
    QVERIFY2(unfoldMenu, "「展开层次 1~8」必须接线到菜单");

    int unfoldLevels = 0;
    for (auto* action : unfoldMenu->actions()) {
        if (action->text().startsWith(QStringLiteral("展开层次 "))) ++unfoldLevels;
    }
    QCOMPARE(unfoldLevels, 8);
}
