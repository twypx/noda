#include "test_performance.h"

#include <QtTest>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFontComboBox>
#include <QLabel>
#include <QTemporaryDir>
#include <QWidget>
#include <algorithm>

#include "EditorView.h"
#include "StyleManager.h"
#include "TabManager.h"
#include "dialogs/PreferencesDialog.h"
#include "utils/LocalizationManager.h"

namespace {

bool hasTheme(const std::vector<ThemeInfo>& themes, const char* name) {
    return std::any_of(themes.begin(), themes.end(), [name](const ThemeInfo& info) {
        return info.name == name;
    });
}

} // namespace

void TestPerformance::smartHighlightIsLimitedToViewportAndFollowsScrolling() {
    EditorView ed;
    ed.resize(640, 480);
    ed.show();

    // 3000 行、每行一个 needle：全文会命中数千次，视口窗口只覆盖约 ±200 行。
    constexpr int kLines = 3000;
    const QByteArray line = "needle 000000\n";
    QByteArray text;
    text.reserve(line.size() * kLines);
    for (int i = 0; i < kLines; ++i) text += line;
    ed.setTextFromUtf8(std::string(text.constData(), static_cast<size_t>(text.size())));

    ed.setHighlightPrefs(true, false, true, false);
    ed.send(SCI_SETSEL, 0, 6);
    ed.refreshHighlighting();

    // 视口内必须高亮；视口之外数万字节不该被全文涂满指示器。
    QCOMPARE(ed.send(SCI_INDICATORVALUEAT, 7, 0), sptr_t(1));
    const sptr_t farPos = static_cast<sptr_t>(line.size()) * 2500;
    QCOMPARE(ed.send(SCI_INDICATORVALUEAT, 7, farPos), sptr_t(0));

    // 滚到远端后窗口重算：新视口内有高亮，旧视口的指示器被清掉。
    ed.send(SCI_SETFIRSTVISIBLELINE, 2450);
    ed.refreshHighlighting();
    QCOMPARE(ed.send(SCI_INDICATORVALUEAT, 7, farPos), sptr_t(1));
    QCOMPARE(ed.send(SCI_INDICATORVALUEAT, 7, 0), sptr_t(0));
}

void TestPerformance::fontCombosShareOneSystemEnumeration() {
    AppPreferences prefs;
    PreferencesDialog dlg(prefs);
    QVERIFY2(dlg.findChildren<QFontComboBox*>().isEmpty(),
             "三个 QFontComboBox 会各自重复枚举全量系统字体");

    auto* zh = dlg.findChild<QComboBox*>(QStringLiteral("uiFontZh"));
    auto* en = dlg.findChild<QComboBox*>(QStringLiteral("uiFontEn"));
    auto* editor = dlg.findChild<QComboBox*>(QStringLiteral("editorFont"));
    QVERIFY(zh);
    QVERIFY(en);
    QVERIFY(editor);
    QCOMPARE(zh->count(), en->count());
    QVERIFY2(zh->count() > 1, "字体下拉必须列出系统字体");
    QCOMPARE(zh->itemText(0), QStringLiteral("（系统默认）"));

    const int before = FontFamilies::buildCount();
    PreferencesDialog another(prefs);
    Q_UNUSED(another);
    QVERIFY2(FontFamilies::buildCount() - before <= 1,
             "字体清单必须常驻缓存，不能每个对话框重新枚举全量字体");
}

void TestPerformance::themeListIsCachedUntilInvalidated() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir().mkpath(dir.filePath("themes")));
    auto writeTheme = [&dir](const QString& name) {
        QFile f(dir.filePath("themes/" + name + ".xml"));
        return f.open(QIODevice::WriteOnly) && f.write("<x/>") > 0;
    };

    QVERIFY(writeTheme(QStringLiteral("CacheProbeAlpha")));
    const std::string root = dir.path().toStdString();
    QVERIFY(hasTheme(StyleManager::locateThemes(root), "CacheProbeAlpha"));

    QVERIFY(writeTheme(QStringLiteral("CacheProbeBeta")));
    QVERIFY2(!hasTheme(StyleManager::locateThemes(root), "CacheProbeBeta"),
             "主题目录运行期是静态的，第二次调用必须命中内存缓存");

    StyleManager::invalidateThemeCache();
    QVERIFY(hasTheme(StyleManager::locateThemes(root), "CacheProbeBeta"));
}

void TestPerformance::chineseUiSkipsReflectionButStillTranslatesLater() {
    LocalizationManager& l10n = LocalizationManager::instance();
    l10n.setLanguage(QStringLiteral("zh_CN"));

    QWidget root;
    auto* label = new QLabel(QStringLiteral("保存(&S)"), &root);
    QEvent showEvent(QEvent::Show);
    QApplication::sendEvent(&root, &showEvent);
    QCOMPARE(label->text(), QStringLiteral("保存(&S)"));
    QVERIFY2(label->dynamicPropertyNames().isEmpty(),
             "中文是原文，Show 事件不应再写翻译记录属性");

    // 中文模式下创建的控件，切英文时由 applyToAll() 兜底翻译，切回还能恢复。
    l10n.setLanguage(QStringLiteral("en_US"));
    QCOMPARE(label->text(), QStringLiteral("&Save"));
    l10n.setLanguage(QStringLiteral("zh_CN"));
    QCOMPARE(label->text(), QStringLiteral("保存(&S)"));
}

void TestPerformance::tabBarStyleBatchDoesSingleRepolish() {
    TabManager tabs;
    const int before = tabs.styleRepolishCount();
    tabs.beginStyleBatch();
    tabs.setTabReduce(true);
    tabs.setActiveTopBar(false);
    tabs.setInactiveColored(true);
    tabs.endStyleBatch();
    QCOMPARE(tabs.styleRepolishCount() - before, 1);
}
