#include "test_preferences_dialog.h"
#include <QTest>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMetaType>
#include <QPushButton>
#include <QSettings>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QTreeWidgetItemIterator>
#include <QVariant>
#include "dialogs/PreferencesDialog.h"
#include "MainWindow.h"
#include "TabManager.h"
#include "EditorView.h"
#include "StyleManager.h"
#include "utils/LocalizationManager.h"
#include "utils/ThemeManager.h"

namespace {

// 前面的 TestConfig 会用精简 XML 覆盖 StyleManager；主题测试需要真实的
// langs.model.xml / stylers.model.xml（构建目录里由 CMake 复制好）。
bool loadRealConfigs() {
    const QString dir = QCoreApplication::applicationDirPath();
    const ConfigLoadReport report = StyleManager::instance().loadConfigs(
        (dir + QStringLiteral("/langs.model.xml")).toStdString(),
        (dir + QStringLiteral("/stylers.model.xml")).toStdString());
    return report.ok;
}

int styleForeground(const std::string& lang, int styleId) {
    const auto* styles = StyleManager::instance().getStyles(lang);
    if (!styles) return -2;
    for (const LexerStyle& style : *styles) {
        if (style.styleID == styleId) return style.fgColor;
    }
    return -1;
}

} // namespace

void TestPreferencesDialog::treeStructureAndPageMapping() {
    AppPreferences prefs;
    PreferencesDialog dlg(prefs);

    auto* tree = dlg.findChild<QTreeWidget*>();
    auto* stack = dlg.findChild<QStackedWidget*>();
    QVERIFY(tree != nullptr);
    QVERIFY(stack != nullptr);

    // 检查顶级节点：外观与行为、编辑器、会话与备份
    QCOMPARE(tree->topLevelItemCount(), 3);
    auto* itemAppearance = tree->topLevelItem(0);
    auto* itemEditor = tree->topLevelItem(1);
    auto* itemSession = tree->topLevelItem(2);

    QVERIFY(itemAppearance->text(0).contains("外观与行为"));
    QVERIFY(itemEditor->text(0).contains("编辑器"));
    QVERIFY(itemSession->text(0).contains("会话与备份"));

    // 外观与行为 子项
    QCOMPARE(itemAppearance->childCount(), 2);
    QVERIFY(itemAppearance->child(0)->text(0).contains("外观"));
    QVERIFY(itemAppearance->child(1)->text(0).contains("标签栏与工具栏"));

    // 编辑器 子项
    QCOMPARE(itemEditor->childCount(), 6);
    QVERIFY(itemEditor->child(0)->text(0).contains("代码编辑"));
    QVERIFY(itemEditor->child(1)->text(0).contains("字体"));
    QVERIFY(itemEditor->child(2)->text(0).contains("配色方案"));
    QVERIFY(itemEditor->child(3)->text(0).contains("新建文档"));
    QVERIFY(itemEditor->child(4)->text(0).contains("自动补全"));
    QVERIFY(itemEditor->child(5)->text(0).contains("高亮匹配"));

    // 验证总页面数（至少对应这 9 个子功能页面）
    QVERIFY(stack->count() >= 9);

    // 测试树选择联动右侧页面堆栈
    tree->setCurrentItem(itemAppearance->child(0));
    int pageAppearanceIdx = stack->currentIndex();

    tree->setCurrentItem(itemAppearance->child(1));
    int pageTabBarIdx = stack->currentIndex();
    QVERIFY(pageAppearanceIdx != pageTabBarIdx);

    tree->setCurrentItem(itemEditor->child(0));
    int pageCodeIdx = stack->currentIndex();
    QVERIFY(pageCodeIdx != pageTabBarIdx);

    tree->setCurrentItem(itemEditor->child(1));
    int pageFontIdx = stack->currentIndex();
    QVERIFY(pageFontIdx != pageCodeIdx);

    tree->setCurrentItem(itemSession);
    int pageSessionIdx = stack->currentIndex();
    QVERIFY(pageSessionIdx != pageFontIdx);
}

void TestPreferencesDialog::loadAndSaveAllCategories() {
    AppPreferences p;
    p.uiLanguage = "en_US";
    p.darkMode = true;
    p.uiFontZh = "Noto Sans CJK SC";
    p.uiFontEn = "DejaVu Sans";
    p.hideToolBar = true;
    p.tabHide = true;
    p.tabVertical = true;
    p.tabLock = true;
    p.tabDoubleClickClose = true;
    p.tabQuitOnEmpty = true;
    p.tabCompactLabelLen = 12;
    p.tabReduce = true;
    p.tabInactiveColor = true;
    p.tabActiveTopBar = false;
    p.tabShowCloseButton = false;

    p.tabWidth = 8;
    p.tabToSpaces = true;
    p.wordWrap = true;
    p.showLineNumbers = false;
    p.showFoldMargin = false;
    p.showIndentGuides = true;
    p.showWhitespace = true;
    p.showEOL = true;

    p.fontName = "DejaVu Sans Mono";
    p.fontSize = 14;
    p.fontSmooth = false;

    p.themeName = "Monokai";
    p.defaultEOL = "Windows (CR LF)";
    p.defaultEncoding = "GB18030";
    p.defaultLanguage = "C++";

    p.autoCompletion = false;
    p.autoCompletionMinChars = 3;
    p.autoCompletionType = 1;
    p.autoCompletionIgnoreNumbers = false;

    p.highlightMatchingWords = false;
    p.highlightMatchingBraces = false;

    p.rememberSession = false;
    p.snapshotBackup = false;
    p.backupIntervalSec = 25;

    PreferencesDialog dlg(p);
    QMetaObject::invokeMethod(&dlg, "onAccept", Qt::DirectConnection);

    const AppPreferences saved = dlg.getPreferences();
    QCOMPARE(saved.uiLanguage, QString("en_US"));
    QCOMPARE(saved.darkMode, true);
    QCOMPARE(saved.hideToolBar, true);
    QCOMPARE(saved.tabHide, true);
    QCOMPARE(saved.tabVertical, true);
    QCOMPARE(saved.tabLock, true);
    QCOMPARE(saved.tabDoubleClickClose, true);
    QCOMPARE(saved.tabQuitOnEmpty, true);
    QCOMPARE(saved.tabCompactLabelLen, 12);
    QCOMPARE(saved.tabReduce, true);
    QCOMPARE(saved.tabInactiveColor, true);
    QCOMPARE(saved.tabActiveTopBar, false);
    QCOMPARE(saved.tabShowCloseButton, false);

    QCOMPARE(saved.tabWidth, 8);
    QCOMPARE(saved.tabToSpaces, true);
    QCOMPARE(saved.wordWrap, true);
    QCOMPARE(saved.showLineNumbers, false);
    QCOMPARE(saved.showFoldMargin, false);
    QCOMPARE(saved.showIndentGuides, true);
    QCOMPARE(saved.showWhitespace, true);
    QCOMPARE(saved.showEOL, true);

    QCOMPARE(saved.fontSize, 14);
    QCOMPARE(saved.fontSmooth, false);
    QCOMPARE(saved.defaultEOL, QString("Windows (CR LF)"));
    QCOMPARE(saved.defaultEncoding, QString("GB18030"));
    QCOMPARE(saved.defaultLanguage, QString("cpp"));

    QCOMPARE(saved.uiFontZh, QString("Noto Sans CJK SC"));
    QCOMPARE(saved.uiFontEn, QString("DejaVu Sans"));
    QCOMPARE(saved.fontName, QString("DejaVu Sans Mono"));
    QCOMPARE(saved.themeName, QString("Monokai"));

    QCOMPARE(saved.autoCompletion, false);
    QCOMPARE(saved.autoCompletionMinChars, 3);
    QCOMPARE(saved.autoCompletionType, 1);
    QCOMPARE(saved.autoCompletionIgnoreNumbers, false);

    QCOMPARE(saved.highlightMatchingWords, false);
    QCOMPARE(saved.highlightMatchingBraces, false);
    QCOMPARE(saved.highlightMatchCase, true);
    QCOMPARE(saved.highlightMatchWholeWord, false);

    QCOMPARE(saved.rememberSession, false);
    QCOMPARE(saved.snapshotBackup, false);
    QCOMPARE(saved.backupIntervalSec, 25);
}

void TestPreferencesDialog::unknownStoredValuesArePreserved() {
    AppPreferences p;
    p.uiFontZh = QStringLiteral("NPP-Definitely-Missing-Font");
    p.fontName = QStringLiteral("NPP-Missing-Editor-Font");
    p.defaultLanguage = QStringLiteral("NoSuchLanguage");
    p.themeName = QStringLiteral("NoSuchTheme");
    p.defaultEOL = QStringLiteral("NoSuchEOL");

    PreferencesDialog dlg(p);
    QMetaObject::invokeMethod(&dlg, "onAccept", Qt::DirectConnection);
    const AppPreferences saved = dlg.getPreferences();

    // 打开对话框点确定不能把未知值改成列表首项。
    QCOMPARE(saved.uiFontZh, p.uiFontZh);
    QCOMPARE(saved.fontName, p.fontName);
    QCOMPARE(saved.defaultLanguage, p.defaultLanguage);
    QCOMPARE(saved.themeName, p.themeName);
    QCOMPARE(saved.defaultEOL, p.defaultEOL);
}

void TestPreferencesDialog::systemDefaultFontMapsToEmpty() {
    AppPreferences p;
    QCOMPARE(p.uiFontZh, QString());
    QCOMPARE(p.uiFontEn, QString());

    PreferencesDialog dlg(p);
    QMetaObject::invokeMethod(&dlg, "onAccept", Qt::DirectConnection);
    const AppPreferences saved = dlg.getPreferences();

    QCOMPARE(saved.uiFontZh, QString());
    QCOMPARE(saved.uiFontEn, QString());
}

void TestPreferencesDialog::officialThemesApplyAndReset() {
    QVERIFY(loadRealConfigs());

    const auto themes = StyleManager::locateThemes(
        QCoreApplication::applicationDirPath().toStdString());
    QVERIFY2(themes.size() >= 20, "必须找到上游官方主题文件");

    std::string monokaiPath;
    for (const ThemeInfo& theme : themes) {
        if (theme.name == "Monokai") monokaiPath = theme.path;
    }
    QVERIFY2(!monokaiPath.empty(), "缺少 Monokai.xml");

    const int original = styleForeground("cpp", 5);
    QVERIFY(original >= 0);

    std::string error;
    QVERIFY2(StyleManager::instance().applyThemeFile(monokaiPath, &error),
             error.c_str());
    // Monokai 的 INSTRUCTION WORD（styleID 5）是 66D9EF，转成 Scintilla 的 BGR。
    QCOMPARE(styleForeground("cpp", 5), 0xEFD966);

    StyleManager::instance().resetTheme();
    QCOMPARE(styleForeground("cpp", 5), original);
}

void TestPreferencesDialog::defaultLanguageAppliedToNewDocument() {
    MainWindow window;
    AppPreferences prefs = window.preferences();
    prefs.defaultLanguage = QStringLiteral("cpp");
    window.applyPreferences(prefs);

    QVERIFY(QMetaObject::invokeMethod(&window, "onNewFile"));
    auto* tabs = window.findChild<TabManager*>();
    QVERIFY(tabs);
    QVERIFY(tabs->currentEditor());
    QCOMPARE(QString::fromStdString(tabs->currentEditor()->currentLanguage()),
             QStringLiteral("cpp"));
}

void TestPreferencesDialog::smartHighlightTogglesIndicator() {
    EditorView ed;
    ed.setTextFromUtf8("foo foo bar");
    ed.setHighlightPrefs(true, true, true, true);
    ed.send(SCI_SETSEL, 0, 3);
    ed.refreshHighlighting();

    QCOMPARE(ed.send(SCI_INDICATORVALUEAT, 7, 0), sptr_t(1));
    QCOMPARE(ed.send(SCI_INDICATORVALUEAT, 7, 4), sptr_t(1));
    QCOMPARE(ed.send(SCI_INDICATORVALUEAT, 7, 8), sptr_t(0));

    ed.setHighlightPrefs(false, true, true, true);
    ed.refreshHighlighting();
    QCOMPARE(ed.send(SCI_INDICATORVALUEAT, 7, 0), sptr_t(0));
}

void TestPreferencesDialog::tabPreferencesApply() {
    MainWindow window;
    auto* tabs = window.findChild<TabManager*>();
    QVERIFY(tabs);

    AppPreferences prefs = window.preferences();
    prefs.tabHide = false;
    prefs.tabVertical = true;
    prefs.tabReduce = true;
    prefs.tabActiveTopBar = false;
    prefs.tabInactiveColor = true;
    prefs.tabCompactLabelLen = 5;
    prefs.tabShowCloseButton = false;
    prefs.tabLock = true;
    prefs.tabDoubleClickClose = true;
    prefs.tabQuitOnEmpty = true;
    window.applyPreferences(prefs);

    QCOMPARE(tabs->tabBar()->property("compact").toBool(), true);
    QCOMPARE(tabs->tabBar()->property("activeTopBar").toBool(), false);
    QCOMPARE(tabs->tabBar()->property("inactiveColored").toBool(), true);
    QCOMPARE(tabs->tabPosition(), QTabWidget::West);
    QCOMPARE(tabs->compactLabelLength(), 5);
    QCOMPARE(tabs->tabsClosable(), false);
    QCOMPARE(tabs->isMovable(), false);
    QCOMPARE(tabs->isDoubleClickClose(), true);
    QCOMPARE(tabs->quitOnEmpty(), true);
    QCOMPARE(tabs->isTabBarHidden(), false);
}

void TestPreferencesDialog::applyPreferencesDoesNotWriteSettings() {
    QSettings().clear();

    MainWindow window;
    AppPreferences prefs = window.preferences();
    prefs.tabWidth = 9;
    window.applyPreferences(prefs);

    // 只有用户在对话框点“确定”后才应该落盘。
    QSettings settings;
    QVERIFY2(!settings.contains(QStringLiteral("prefs/tabWidth")),
             "applyPreferences 不应写 QSettings");
}

void TestPreferencesDialog::wordCompletionTriggers() {
    EditorView ed;
    ed.resize(480, 240);
    ed.show();
    QVERIFY(QTest::qWaitForWindowExposed(&ed));

    ed.setAutoCompletion(true, 2, 1, true); // 仅单词补全，2 字符触发
    ed.setTextFromUtf8("foobar baz\n");
    ed.send(SCI_GOTOPOS, ed.send(SCI_GETLENGTH));
    ed.setFocus();

    QTest::keyClicks(&ed, "foo");
    QTRY_VERIFY2(ed.send(SCI_AUTOCACTIVE) != 0,
                 "输入 3 个字符后应弹出文档词补全候选");

    ed.send(SCI_AUTOCCANCEL);
    QCOMPARE(ed.send(SCI_AUTOCACTIVE), sptr_t(0));
}

void TestPreferencesDialog::mainWindowAppliesOfficialTheme() {
    QVERIFY(loadRealConfigs());

    MainWindow window;
    auto* tabs = window.findChild<TabManager*>();
    QVERIFY(tabs);
    EditorView* editor = tabs->currentEditor();
    QVERIFY(editor);
    editor->applyLanguage("cpp");

    AppPreferences prefs = window.preferences();
    prefs.themeName = QStringLiteral("Monokai");
    window.applyPreferences(prefs);

    // Monokai 的 INSTRUCTION WORD（styleID 5）= RGB 66D9EF，Scintilla 存 BGR。
    QCOMPARE(editor->send(SCI_STYLEGETFORE, 5), sptr_t(0xEFD966));

    prefs.themeName = QStringLiteral("Default");
    window.applyPreferences(prefs);
    // Default 必须恢复内置 stylers 的颜色，而不是保留上一套主题。
    QVERIFY(editor->send(SCI_STYLEGETFORE, 5) != sptr_t(0xEFD966));
}

void TestPreferencesDialog::mainWindowAppliesExtendedPreferences() {
    MainWindow win;
    auto* tabMgr = win.findChild<TabManager*>();
    auto* toolBar = win.findChild<QToolBar*>();
    QVERIFY(tabMgr != nullptr);
    QVERIFY(toolBar != nullptr);

    AppPreferences p;
    p.hideToolBar = true;
    p.tabShowCloseButton = false;
    p.tabLock = true;
    p.fontSmooth = false;
    p.tabWidth = 2;
    p.defaultEOL = "Unix (LF)";

    win.applyPreferences(p);

    // 验证工具栏隐藏生效
    QCOMPARE(toolBar->isVisible(), false);
    // 验证标签栏关闭按钮与移动锁定生效
    QCOMPARE(tabMgr->tabsClosable(), false);
    QCOMPARE(tabMgr->isMovable(), false);

    // 验证当前编辑器设置
    auto* ed = tabMgr->currentEditor();
    QVERIFY(ed != nullptr);
    QCOMPARE(ed->send(SCI_GETTABWIDTH), 2);
}

void TestPreferencesDialog::interfaceLanguageSwitchesFromNativeLangXml() {
    LocalizationManager& l10n = LocalizationManager::instance();
    l10n.setLanguage(QStringLiteral("zh_CN"));

    QWidget root;
    auto* label = new QLabel(QStringLiteral("保存(&S)"), &root);
    l10n.applyToWidget(&root);
    QCOMPARE(label->text(), QStringLiteral("保存(&S)"));

    // 上游 chineseSimplified.xml id=41006 / english.xml id=41006: 保存(&S) -> &Save
    l10n.setLanguage(QStringLiteral("en_US"));
    l10n.applyToWidget(&root);
    QCOMPARE(label->text(), QStringLiteral("&Save"));

    // 切回中文必须用记录的原文恢复，而不是把英文再“翻译”一遍。
    l10n.setLanguage(QStringLiteral("zh_CN"));
    l10n.applyToWidget(&root);
    QCOMPARE(label->text(), QStringLiteral("保存(&S)"));

    // 主窗口真实菜单也来自上游配对：文件(&F) -> &File。
    MainWindow window;
    QMenu* fileMenu = nullptr;
    for (QMenu* menu : window.findChildren<QMenu*>()) {
        if (menu->title() == QStringLiteral("文件(&F)")) {
            fileMenu = menu;
            break;
        }
    }
    QVERIFY(fileMenu);
    QCOMPARE(fileMenu->title(), QStringLiteral("文件(&F)"));
    l10n.setLanguage(QStringLiteral("en_US"));
    QCOMPARE(fileMenu->title(), QStringLiteral("&File"));
    l10n.setLanguage(QStringLiteral("zh_CN"));
    QCOMPARE(fileMenu->title(), QStringLiteral("文件(&F)"));
}

void TestPreferencesDialog::interfaceFontsAffectApplicationAndStyleSheet() {
    MainWindow window;
    AppPreferences prefs = window.preferences();
    prefs.uiFontEn = QStringLiteral("DejaVu Sans");
    prefs.uiFontZh = QStringLiteral("Noto Sans CJK SC");
    window.applyPreferences(prefs);

    const QString expected = QStringLiteral("font-family: \"DejaVu Sans\", \"Noto Sans CJK SC\";");
    QVERIFY2(QApplication::font().families().contains(QStringLiteral("DejaVu Sans")),
             "英文字体必须进入 QApplication 默认字体");
    QVERIFY2(QApplication::font().families().contains(QStringLiteral("Noto Sans CJK SC")),
             "中文字体必须进入字体回退链");
    QVERIFY2(qApp->styleSheet().contains(expected),
             "主题 QSS 不能写死 font-family，否则会覆盖首选项字体");

    // 切换深/浅主题会重建 QSS，字体不能丢失。
    ThemeManager::applyTheme(true);
    QVERIFY2(qApp->styleSheet().contains(expected), "切换深色主题后字体设置丢失");
    QVERIFY(QApplication::font().families().contains(QStringLiteral("DejaVu Sans")));
    ThemeManager::applyTheme(false);
    QVERIFY2(qApp->styleSheet().contains(expected), "切换浅色主题后字体设置丢失");

    // 恢复系统默认，避免影响后续测试。
    prefs.uiFontEn.clear();
    prefs.uiFontZh.clear();
    window.applyPreferences(prefs);
    QVERIFY2(!qApp->styleSheet().contains(expected), "选择系统默认后不应残留字体声明");
}

void TestPreferencesDialog::braceHighlightFollowsPreference() {
    EditorView ed;
    ed.setTextFromUtf8("(x)");
    ed.setHighlightPrefs(true, true, true, false);

    // 光标停在左括号之后：应识别 (0) 与 ) (2) 这一对。
    ed.send(SCI_GOTOPOS, 1);
    ed.refreshHighlighting();
    QCOMPARE(ed.braceHighlightPositions().first, 0);
    QCOMPARE(ed.braceHighlightPositions().second, 2);
    QVERIFY2(ed.send(SCI_STYLEGETBACK, STYLE_BRACELIGHT) != 0,
             "括号高亮样式必须有可见背景色");

    ed.setHighlightPrefs(true, false, true, false);
    ed.refreshHighlighting();
    QCOMPARE(ed.braceHighlightPositions().first, -1);
    QCOMPARE(ed.braceHighlightPositions().second, -1);
}

void TestPreferencesDialog::preferencesUiIsCleanAndHasNoPlaceholders() {
    AppPreferences prefs;
    PreferencesDialog dlg(prefs);

    auto hasChinese = [](const QString& text) {
        for (QChar ch : text) {
            if (ch.unicode() >= 0x4E00 && ch.unicode() <= 0x9FFF) return true;
        }
        return false;
    };
    auto hasEnglishParenthesis = [](const QString& text) {
        for (int i = 0; i + 1 < text.size(); ++i) {
            const QChar open = text.at(i);
            if (open != QLatin1Char('(') && open != QChar(0xFF08)) continue;
            const QChar next = text.at(i + 1);
            if ((next >= QLatin1Char('A') && next <= QLatin1Char('Z'))
                || (next >= QLatin1Char('a') && next <= QLatin1Char('z'))) {
                return true;
            }
        }
        return false;
    };

    QStringList texts;
    QList<QObject*> objects;
    objects.append(&dlg);
    objects.append(dlg.findChildren<QObject*>());
    static const QList<QByteArray> properties = {
        "text", "title", "windowTitle", "toolTip"
    };
    for (QObject* object : objects) {
        for (const QByteArray& property : properties) {
            const QVariant value = object->property(property.constData());
            if (value.isValid() && value.userType() == QMetaType::QString) {
                const QString text = value.toString();
                if (!text.isEmpty()) texts << text;
            }
        }
        if (auto* combo = qobject_cast<QComboBox*>(object)) {
            for (int i = 0; i < combo->count(); ++i) texts << combo->itemText(i);
        }
    }
    for (QTreeWidget* tree : dlg.findChildren<QTreeWidget*>()) {
        QTreeWidgetItemIterator it(tree);
        while (*it) {
            for (int column = 0; column < (*it)->columnCount(); ++column)
                texts << (*it)->text(column);
            ++it;
        }
    }

    static const QStringList forbidden = {
        QStringLiteral("Notepad++"), QStringLiteral("复刻"),
        QStringLiteral("stylers.model.xml"), QStringLiteral("themes"),
        QStringLiteral("官方"), QStringLiteral("APIs"),
        QStringLiteral("当前版本"), QStringLiteral("暂未实现"),
        QStringLiteral("未随包"), QStringLiteral("Scintilla"),
        QStringLiteral("Qt 的"), QStringLiteral("QTabBar")
    };
    static const QStringList removedOptions = {
        QStringLiteral("回滚字体"), QStringLiteral("启用连写"),
        QStringLiteral("多行显示"), QStringLiteral("使用替代图标"),
        QStringLiteral("固定标签"), QStringLiteral("仅显示固定按钮"),
        QStringLiteral("在非当前标签上显示按钮"),
        QStringLiteral("函数参数提示"), QStringLiteral("仅函数补全"),
        QStringLiteral("XML/HTML 标签")
    };

    for (const QString& text : texts) {
        for (const QString& bad : forbidden) {
            QVERIFY2(!text.contains(bad),
                     qPrintable(QStringLiteral("界面文案仍包含实现说明：%1 / %2").arg(text, bad)));
        }
        for (const QString& removed : removedOptions) {
            QVERIFY2(!text.contains(removed),
                     qPrintable(QStringLiteral("不支持的配置项仍在界面中：%1 / %2").arg(text, removed)));
        }
        if (hasChinese(text)) {
            QVERIFY2(!hasEnglishParenthesis(text),
                     qPrintable(QStringLiteral("中文配置项错误带英文括注：%1").arg(text)));
        }
    }
}
