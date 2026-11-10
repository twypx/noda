#include "test_localization.h"

#include <QtTest>
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QFontComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QTableWidget>
#include <QTreeWidget>

#include "MainWindow.h"
#include "dialogs/PreferencesDialog.h"
#include "utils/LocalizationManager.h"

namespace {

bool hasChinese(const QString& text) {
    for (const QChar ch : text) {
        if (ch.unicode() >= 0x4E00 && ch.unicode() <= 0x9FFF) return true;
    }
    return false;
}

void addIfChinese(const QString& text, const QString& context, QStringList& out) {
    if (!text.isEmpty() && hasChinese(text)) out << context + QStringLiteral(" => ") + text;
}

void collectTree(QTreeWidgetItem* item, const QString& context, QStringList& out) {
    if (!item) return;
    for (int c = 0; c < item->columnCount(); ++c)
        addIfChinese(item->text(c), context + QStringLiteral("/QTreeWidget.item"), out);
    for (int i = 0; i < item->childCount(); ++i) collectTree(item->child(i), context, out);
}

void collectObject(QObject* o, QStringList& out, QSet<QObject*>& seen) {
    if (!o || seen.contains(o)) return;
    seen.insert(o);

    const QString cls = QString::fromLatin1(o->metaObject()->className());
    // 文档/输入内容不是界面文案。
    if (o->inherits("QLineEdit") || o->inherits("QTextEdit")
        || o->inherits("QPlainTextEdit") || o->inherits("ScintillaEditBase")
        || cls == QLatin1String("HexView")) {
        return;
    }

    const QString context = cls + QLatin1Char('|') + o->objectName();

    if (auto* action = qobject_cast<QAction*>(o)) {
        addIfChinese(action->text(), context + QStringLiteral("/QAction.text"), out);
        addIfChinese(action->toolTip(), context + QStringLiteral("/QAction.toolTip"), out);
        addIfChinese(action->statusTip(), context + QStringLiteral("/QAction.statusTip"), out);
    }

    for (const char* prop : {"text", "title", "windowTitle", "toolTip", "statusTip"}) {
        const QVariant v = o->property(prop);
        if (v.isValid() && v.userType() == QMetaType::QString)
            addIfChinese(v.toString(), context + QLatin1Char('/') + QLatin1String(prop), out);
    }

    if (auto* combo = qobject_cast<QComboBox*>(o)) {
        // 字体名、语言原生名不是「界面文案」，跳过。
        const bool isFontCombo = o->inherits("QFontComboBox")
                                 || o->objectName().contains(QLatin1String("font"), Qt::CaseInsensitive);
        bool isLang = false;
        for (int i = 0; i < combo->count(); ++i) {
            const QString d = combo->itemData(i).toString();
            if (d == QLatin1String("zh_CN") || d == QLatin1String("en_US")) isLang = true;
        }
        if (!isFontCombo && !isLang) {
            for (int i = 0; i < combo->count(); ++i)
                addIfChinese(combo->itemText(i), context + QStringLiteral("/QComboBox.item"), out);
        }
    }

    if (auto* table = qobject_cast<QTableWidget*>(o)) {
        for (int r = 0; r < table->rowCount(); ++r) {
            for (int c = 0; c < table->columnCount(); ++c) {
                if (QTableWidgetItem* item = table->item(r, c))
                    addIfChinese(item->text(), context + QStringLiteral("/QTableWidget.item"), out);
            }
        }
    }

    if (auto* tree = qobject_cast<QTreeWidget*>(o)) {
        for (int i = 0; i < tree->topLevelItemCount(); ++i) collectTree(tree->topLevelItem(i), context, out);
    }

    const QObjectList children = o->children();
    for (QObject* child : children) collectObject(child, out, seen);
}

void reportAndAssert(const QStringList& offenders) {
    for (const QString& line : offenders) qWarning().noquote() << "L10N_OFFENDER" << line;
}

} // namespace

void TestLocalization::englishModeHasNoChineseInMainWindow() {
    LocalizationManager::instance().initialize(qApp);
    MainWindow win; // 构造时按首选项语言（默认中文）重置翻译器
    win.show();
    QCoreApplication::processEvents();

    // 构造之后再切英文，模拟用户在首选项里选择 English 后的真实状态。
    LocalizationManager::instance().setLanguage(QStringLiteral("en_US"));
    LocalizationManager::instance().applyToAll();

    QStringList offenders;
    QSet<QObject*> seen;
    collectObject(&win, offenders, seen);
    reportAndAssert(offenders);

    QVERIFY2(offenders.isEmpty(), "英文模式主窗口残留中文（见 L10N_OFFENDER 日志）");

    LocalizationManager::instance().setLanguage(QStringLiteral("zh_CN"));
}

void TestLocalization::englishModeHasNoChineseInPreferences() {
    LocalizationManager::instance().initialize(qApp);

    AppPreferences prefs;
    PreferencesDialog dlg(prefs);
    dlg.show();
    QCoreApplication::processEvents();

    LocalizationManager::instance().setLanguage(QStringLiteral("en_US"));
    LocalizationManager::instance().applyToAll();

    // 语言选择项必须保持各自的「原生名称」：简体中文 / English，而不是 English / English。
    QComboBox* langCombo = nullptr;
    for (QComboBox* combo : dlg.findChildren<QComboBox*>()) {
        for (int i = 0; i < combo->count(); ++i) {
            if (combo->itemData(i).toString() == QLatin1String("zh_CN")) langCombo = combo;
        }
    }
    QVERIFY2(langCombo != nullptr, "未找到界面语言下拉框");
    QCOMPARE(langCombo->itemText(0), QStringLiteral("简体中文"));
    QCOMPARE(langCombo->itemText(1), QStringLiteral("English"));

    QStringList offenders;
    QSet<QObject*> seen;
    collectObject(&dlg, offenders, seen);
    reportAndAssert(offenders);

    QVERIFY2(offenders.isEmpty(), "英文模式设置对话框残留中文（见 L10N_OFFENDER 日志）");

    LocalizationManager::instance().setLanguage(QStringLiteral("zh_CN"));
}
