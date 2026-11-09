#include "test_style_config.h"
#include "dialogs/StyleOverride.h"
#include "dialogs/StyleConfigDialog.h"
#include "StyleManager.h"
#include "EditorView.h"
#include "MainWindow.h"
#include "TabManager.h"

#include <QtTest>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontComboBox>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTemporaryDir>

namespace {

const char* kLangsXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<NotepadPlus><Languages><Language name="cpp" ext="cpp"/></Languages></NotepadPlus>)";

const char* kStylersXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<NotepadPlus>
  <GlobalStyles><WidgetStyle name="Default Style" fgColor="000000" bgColor="FFFFFF"/></GlobalStyles>
  <LexerStyles><LexerType name="cpp">
    <WordsStyle name="DEFAULT" styleID="0" fgColor="000000" bgColor="FFFFFF" fontStyle="0"/>
    <WordsStyle name="COMMENT" styleID="1" fgColor="008000" bgColor="FFFFFF" fontStyle="0"/>
    <WordsStyle name="KEYWORD" styleID="5" fgColor="0000FF" bgColor="FFFFFF" fontStyle="1"/>
  </LexerType></LexerStyles>
</NotepadPlus>)";

bool put(const QString& path, const char* content) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray data(content);
    return f.write(data) == data.size();
}

const ConfigLoadReport& fixture() {
    static QTemporaryDir dir;
    static const QString langs = dir.filePath("langs.model.xml");
    static const QString stylers = dir.filePath("stylers.model.xml");
    static const ConfigLoadReport report = [&]() {
        put(langs, kLangsXml);
        put(stylers, kStylersXml);
        return StyleManager::instance().loadConfigs(
            langs.toStdString(), stylers.toStdString());
    }();
    return report;
}

StyleOverride sample() {
    StyleOverride o;
    o.langId = QStringLiteral("cpp");
    o.styleId = 1;
    o.fg = 0x112233;
    o.bg = 0x445566;
    o.bold = true;
    o.italic = false;
    o.fontName = QStringLiteral("DejaVu Sans Mono");
    o.fontSize = 14;
    return o;
}

} // namespace

void TestStyleConfig::overrideRoundTrip() {
    const StyleOverride o = sample();
    const QString key = StyleOverride::settingsKey(o.langId, o.styleId);
    QCOMPARE(key, QStringLiteral("cpp/1"));
    const auto parsed = StyleOverride::deserialize(key, o.serialize());
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->langId, o.langId);
    QCOMPARE(parsed->styleId, o.styleId);
    QCOMPARE(parsed->fg, o.fg);
    QCOMPARE(parsed->bg, o.bg);
    QCOMPARE(parsed->bold, o.bold);
    QCOMPARE(parsed->italic, o.italic);
    QCOMPARE(parsed->fontName, o.fontName);
    QCOMPARE(parsed->fontSize, o.fontSize);
}

void TestStyleConfig::legacyValueIsAccepted() {
    const auto parsed = StyleOverride::deserialize(
        QStringLiteral("cpp"), QStringLiteral("123;456;1;0"));
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->langId, QStringLiteral("cpp"));
    QCOMPARE(parsed->styleId, 0);
    QCOMPARE(parsed->fg, 123);
    QCOMPARE(parsed->bg, 456);
    QVERIFY(parsed->bold);
    QVERIFY(!parsed->italic);
    QVERIFY(parsed->fontName.isEmpty());
    QCOMPARE(parsed->fontSize, 0);
}

void TestStyleConfig::malformedValueIsRejected() {
    QVERIFY(!StyleOverride::deserialize(QStringLiteral("a/b/c"), QStringLiteral("{}")).has_value());
    QVERIFY(!StyleOverride::deserialize(QStringLiteral("cpp/1"), QStringLiteral("garbage")).has_value());
    QVERIFY(!StyleOverride::deserialize(QStringLiteral("cpp/not-a-number"),
                                        QStringLiteral("1;2;0;0")).has_value());
}

void TestStyleConfig::loadAllReadsPersistedOverrides() {
    QSettings s;
    s.beginGroup("styleOverride");
    s.remove(QStringLiteral("cpp"));
    s.remove(QStringLiteral("cpp/1"));
    s.setValue(QStringLiteral("cpp"), QStringLiteral("1;2;0;1"));
    const StyleOverride o = sample();
    s.setValue(StyleOverride::settingsKey(o.langId, o.styleId), o.serialize());
    s.endGroup();
    s.sync();

    const QList<StyleOverride> all = StyleOverride::loadAll();
    QCOMPARE(all.size(), 2);
    bool sawLegacy = false;
    bool sawModern = false;
    for (const auto& item : all) {
        if (item.styleId == 0 && item.langId == QStringLiteral("cpp")) sawLegacy = item.italic;
        if (item.styleId == 1) sawModern = (item.fontName == o.fontName && item.fontSize == 14);
    }
    QVERIFY(sawLegacy);
    QVERIFY(sawModern);
}

void TestStyleConfig::styleNamesAreParsed() {
    QVERIFY2(fixture().ok, "测试用精简配置必须载入成功");
    const auto* styles = StyleManager::instance().getStyles("cpp");
    QVERIFY(styles);
    QCOMPARE(styles->size(), std::size_t(3));
    QCOMPARE(QString::fromStdString(styles->at(1).name), QStringLiteral("COMMENT"));
    QCOMPARE(styles->at(2).styleID, 5);
}

void TestStyleConfig::fontOverrideReachesEditor() {
    QVERIFY(fixture().ok);
    StyleManager::instance().overrideFont("cpp", 1, "DejaVu Sans Mono", 14);

    EditorView ed;
    ed.setEditorFont(QStringLiteral("Monospace"), 10);
    ed.applyThemeStyles("cpp");
    char font[128] = {};
    ed.send(SCI_STYLEGETFONT, 1, reinterpret_cast<sptr_t>(font));
    QCOMPARE(QByteArray(font), QByteArray("DejaVu Sans Mono"));
    QCOMPARE(ed.send(SCI_STYLEGETSIZE, 1), sptr_t(14));
    QCOMPARE(ed.send(SCI_STYLEGETSIZE, 5), sptr_t(10));
}

void TestStyleConfig::dialogPersistsAllFields() {
    QVERIFY(fixture().ok);
    QSettings s;
    s.beginGroup("styleOverride");
    s.remove(QStringLiteral("cpp/1"));
    s.endGroup();

    StyleConfigDialog dlg;
    auto* langList = dlg.findChild<QListWidget*>();
    QVERIFY(langList);
    int langRow = -1;
    for (int i = 0; i < langList->count(); ++i) {
        if (langList->item(i)->data(Qt::UserRole).toString() == QStringLiteral("cpp")) {
            langRow = i;
            break;
        }
    }
    QVERIFY2(langRow >= 0, "语言列表必须包含已加载语言");
    langList->setCurrentRow(langRow);

    auto* slotCombo = dlg.findChild<QComboBox*>(QStringLiteral("styleSlotCombo"));
    QVERIFY(slotCombo);
    QVERIFY2(slotCombo->count() >= 2, "必须能编辑多个样式槽位，而不只是槽位 0");
    const int slotRow = slotCombo->findData(1);
    QVERIFY2(slotRow >= 0, "槽位下拉必须以 styleID 为 data");
    slotCombo->setCurrentIndex(slotRow);

    auto* fontCombo = dlg.findChild<QFontComboBox*>();
    auto* sizeSpin = dlg.findChild<QSpinBox*>();
    QVERIFY(fontCombo);
    QVERIFY(sizeSpin);
    const QString family = fontCombo->currentFont().family();
    sizeSpin->setValue(15);

    auto* box = dlg.findChild<QDialogButtonBox*>();
    QVERIFY(box);
    auto* applyBtn = box->button(QDialogButtonBox::Apply);
    QVERIFY(applyBtn);
    applyBtn->click();

    QSettings out;
    out.beginGroup("styleOverride");
    const QString key = StyleOverride::settingsKey(QStringLiteral("cpp"), 1);
    QVERIFY2(out.contains(key), qPrintable(QStringLiteral("缺少键 ") + key));
    const auto parsed = StyleOverride::deserialize(key, out.value(key).toString());
    out.endGroup();
    QVERIFY(parsed.has_value());
    QCOMPARE(parsed->styleId, 1);
    QCOMPARE(parsed->fontName, family);
    QCOMPARE(parsed->fontSize, 15);
}

void TestStyleConfig::mainWindowAppliesPersistedOverrides() {
    QVERIFY(fixture().ok);
    const StyleOverride o = sample();
    QSettings s;
    s.beginGroup("styleOverride");
    s.setValue(StyleOverride::settingsKey(QStringLiteral("cpp"), 1), o.serialize());
    s.endGroup();
    s.sync();

    MainWindow window;
    auto* tabs = window.findChild<TabManager*>();
    QVERIFY(tabs);
    auto* ed = tabs->currentEditor();
    QVERIFY(ed);
    // applyLanguage() 会用全局字体重置所有槽位，之后再回灌覆盖，验证两者不互相冲掉。
    ed->applyLanguage("cpp");
    QVERIFY(QMetaObject::invokeMethod(&window, "reloadStyleOverrides"));

    QCOMPARE(ed->send(SCI_STYLEGETSIZE, 1), sptr_t(14));
    char font[128] = {};
    ed->send(SCI_STYLEGETFONT, 1, reinterpret_cast<sptr_t>(font));
    QCOMPARE(QByteArray(font), QByteArray("DejaVu Sans Mono"));
}
