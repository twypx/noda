#include "test_shortcuts.h"
#include "dialogs/ShortcutManager.h"
#include "dialogs/ShortcutsDialog.h"
#include "MainWindow.h"

#include <QtTest>
#include <QAction>
#include <QDialogButtonBox>
#include <QKeySequenceEdit>
#include <QMenu>
#include <QPushButton>
#include <QWidget>

namespace {

struct ActionFixture {
    QWidget root;
    QAction* newAction{nullptr};
    QAction* saveAsAction{nullptr};

    ActionFixture() {
        auto* menu = new QMenu(QStringLiteral("文件(&F)"), &root);
        newAction = new QAction(QStringLiteral("新建(&N)"), &root);
        newAction->setShortcut(QKeySequence(QKeySequence::New));
        menu->addAction(newAction);
        saveAsAction = new QAction(QStringLiteral("另存为(&A)..."), &root);
        saveAsAction->setShortcut(QKeySequence(QKeySequence::SaveAs));
        menu->addAction(saveAsAction);
        ShortcutManager::instance().resetAll();
    }
};

QKeySequence seq(const char* text) {
    return QKeySequence(QString::fromLatin1(text));
}

} // namespace

void TestShortcuts::collectActionsReflectsRealActions() {
    ActionFixture fx;
    const QList<ShortcutEntry> entries = ShortcutManager::instance().collectActions(&fx.root);
    QCOMPARE(entries.size(), 2);
    QCOMPARE(entries[0].name, QStringLiteral("新建"));
    QCOMPARE(entries[0].category, QStringLiteral("文件"));
    QCOMPARE(entries[0].defaultSeq, QKeySequence(QKeySequence::New));
    QCOMPARE(entries[1].name, QStringLiteral("另存为"));
    QCOMPARE(entries[1].defaultSeq, QKeySequence(QKeySequence::SaveAs));
    QVERIFY(!entries[0].nativeOnly);
}

void TestShortcuts::applyOverridesUpdatesActions() {
    ActionFixture fx;
    QList<ShortcutEntry> entries = ShortcutManager::instance().collectActions(&fx.root);
    entries[0].currentSeq = seq("Ctrl+Alt+N");
    QString conflict;
    QVERIFY(ShortcutManager::instance().applyOverrides(entries, &conflict));
    QVERIFY(conflict.isEmpty());
    ShortcutManager::instance().applyTo(&fx.root);
    QCOMPARE(fx.newAction->shortcut(), seq("Ctrl+Alt+N"));
    QCOMPARE(fx.saveAsAction->shortcut(), QKeySequence(QKeySequence::SaveAs));
}

void TestShortcuts::conflictingSequencesAreRejected() {
    ActionFixture fx;
    QList<ShortcutEntry> entries = ShortcutManager::instance().collectActions(&fx.root);
    entries[0].currentSeq = seq("Ctrl+Alt+N");
    entries[1].currentSeq = seq("Ctrl+Alt+N");
    QString conflict;
    QVERIFY2(!ShortcutManager::instance().applyOverrides(entries, &conflict),
             "两个命令绑定同一按键必须被拒绝");
    QVERIFY(!conflict.isEmpty());

    ShortcutManager::instance().applyTo(&fx.root);
    QCOMPARE(fx.newAction->shortcut(), QKeySequence(QKeySequence::New));
    QCOMPARE(fx.saveAsAction->shortcut(), QKeySequence(QKeySequence::SaveAs));
}

void TestShortcuts::resetAllRestoresDefaults() {
    ActionFixture fx;
    QList<ShortcutEntry> entries = ShortcutManager::instance().collectActions(&fx.root);
    entries[0].currentSeq = seq("Ctrl+Alt+N");
    QString conflict;
    QVERIFY(ShortcutManager::instance().applyOverrides(entries, &conflict));
    ShortcutManager::instance().applyTo(&fx.root);
    QCOMPARE(fx.newAction->shortcut(), seq("Ctrl+Alt+N"));

    ShortcutManager::instance().resetAll();
    ShortcutManager::instance().applyTo(&fx.root);
    QCOMPARE(fx.newAction->shortcut(), QKeySequence(QKeySequence::New));
}

void TestShortcuts::dialogEditsRealActions() {
    ActionFixture fx;
    ShortcutsDialog dlg(&fx.root);

    QKeySequenceEdit* target = nullptr;
    const auto edits = dlg.findChildren<QKeySequenceEdit*>();
    for (auto* edit : edits) {
        if (edit->property("actionKey").toString() == QStringLiteral("新建")) {
            target = edit;
            break;
        }
    }
    QVERIFY2(target, "快捷键列必须是可编辑控件，而不是只读表格");
    QVERIFY(target->isEnabled());
    target->setKeySequence(seq("Ctrl+Alt+Z"));

    auto* box = dlg.findChild<QDialogButtonBox*>();
    QVERIFY(box);
    auto* applyBtn = box->button(QDialogButtonBox::Apply);
    QVERIFY(applyBtn);
    applyBtn->click();
    QCOMPARE(fx.newAction->shortcut(), seq("Ctrl+Alt+Z"));
}

void TestShortcuts::mainWindowShortcutsComeFromActions() {
    ShortcutManager::instance().resetAll();
    MainWindow window;
    const QList<ShortcutEntry> entries = ShortcutManager::instance().collectActions(&window);

    bool foundSaveAs = false;
    bool foundFormat = false;
    for (const auto& entry : entries) {
        if (entry.name == QStringLiteral("另存为")) {
            QCOMPARE(entry.currentSeq, QKeySequence(QKeySequence::SaveAs));
            foundSaveAs = true;
        }
        // v1.0.8 新增的「代码格式化」必须自动进表，且值与真实注册值一致。
        if (entry.name == QStringLiteral("代码格式化")) {
            QCOMPARE(entry.currentSeq, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
            foundFormat = true;
        }
    }
    QVERIFY2(foundSaveAs, "必须能从真实菜单收集到「另存为」");
    QVERIFY2(foundFormat, "v1.0.8 的「代码格式化」必须自动出现在映射器里");
}
