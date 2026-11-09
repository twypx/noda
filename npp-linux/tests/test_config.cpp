#include "test_config.h"
#include "StyleManager.h"

#include <QtTest>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

namespace {

const char* kLangsXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<NotepadPlus><Languages>
  <Language name="cpp" ext="cpp h" commentLine="//" commentStart="/*" commentEnd="*/">
    <Keywords name="instre1">if else</Keywords>
  </Language>
</Languages></NotepadPlus>)";

const char* kOtherLangsXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<NotepadPlus><Languages><Language name="python" ext="py"/></Languages></NotepadPlus>)";

const char* kStylersXml = R"(<?xml version="1.0" encoding="UTF-8"?>
<NotepadPlus>
  <GlobalStyles>
    <WidgetStyle name="Default Style" fgColor="112233" bgColor="FFFFFF" fontName="Monospace" fontSize="11"/>
  </GlobalStyles>
  <LexerStyles>
    <LexerType name="cpp" desc="C++" ext="">
      <WordsStyle name="DEFAULT" styleID="0" fgColor="000000" bgColor="FFFFFF" fontStyle="0"/>
      <WordsStyle name="COMMENT" styleID="1" fgColor="008000" bgColor="FFFFFF" fontStyle="1"/>
    </LexerType>
  </LexerStyles>
</NotepadPlus>)";

bool put(const QString& path, const char* content) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray data(content);
    return f.write(data) == data.size();
}

struct Fixture {
    QTemporaryDir dir;
    QString langs;
    QString stylers;

    Fixture() {
        langs = dir.filePath("langs.model.xml");
        stylers = dir.filePath("stylers.model.xml");
        put(langs, kLangsXml);
        put(stylers, kStylersXml);
    }
};

QString errorsOf(const ConfigLoadReport& r) {
    QStringList parts;
    for (const auto& e : r.errors) parts << QString::fromStdString(e);
    return parts.join(QLatin1Char('\n'));
}

} // namespace

void TestConfig::validConfigsReportCounts() {
    Fixture fx;
    QVERIFY(fx.dir.isValid());
    const ConfigLoadReport r = StyleManager::instance().loadConfigs(
        fx.langs.toStdString(), fx.stylers.toStdString());
    QVERIFY2(r.ok, qPrintable(errorsOf(r)));
    QCOMPARE(r.languageCount, std::size_t(1));
    QCOMPARE(r.styleCount, std::size_t(1));
    QVERIFY(r.errors.empty());
    QCOMPARE(StyleManager::instance().lastReport().languageCount, std::size_t(1));
    // 112233(RRGGBB) 转 Scintilla 的 BGR 整数。
    QCOMPARE(StyleManager::instance().getGlobalStyles().defaultFg, 0x332211);
    QCOMPARE(QString::fromStdString(StyleManager::instance().detectLanguageByExt("CPP")),
             QStringLiteral("cpp"));
}

void TestConfig::missingStylersIsReported() {
    Fixture fx;
    const ConfigLoadReport r = StyleManager::instance().loadConfigs(
        fx.langs.toStdString(), fx.dir.filePath("no-such-stylers.xml").toStdString());
    QVERIFY2(!r.ok, "stylers 缺失不能只凭语言数非空就判定成功");
    QVERIFY2(errorsOf(r).contains(QStringLiteral("stylers")), qPrintable(errorsOf(r)));
}

void TestConfig::emptyStructureIsReported() {
    Fixture fx;
    QVERIFY(put(fx.langs, R"(<NotepadPlus><Languages></Languages></NotepadPlus>)"));
    const ConfigLoadReport r = StyleManager::instance().loadConfigs(
        fx.langs.toStdString(), fx.stylers.toStdString());
    QVERIFY(!r.ok);
    QVERIFY2(errorsOf(r).contains(QStringLiteral("语言")), qPrintable(errorsOf(r)));
}

void TestConfig::invalidColorKeepsDefaults() {
    Fixture fx;
    const char* bad = R"(<NotepadPlus>
      <GlobalStyles><WidgetStyle name="Default Style" fgColor="ZZZZZZ" bgColor="??????"/></GlobalStyles>
      <LexerStyles><LexerType name="cpp"><WordsStyle name="DEFAULT" styleID="0" fgColor="000000"/></LexerType></LexerStyles>
    </NotepadPlus>)";
    QVERIFY(put(fx.stylers, bad));
    const ConfigLoadReport r = StyleManager::instance().loadConfigs(
        fx.langs.toStdString(), fx.stylers.toStdString());
    QVERIFY2(r.ok, qPrintable(errorsOf(r)));
    QCOMPARE(StyleManager::instance().getGlobalStyles().defaultFg, 0x000000);
    QCOMPARE(StyleManager::instance().getGlobalStyles().defaultBg, 0xFFFFFF);
    QVERIFY2(!r.warnings.empty(), "非法颜色必须留下可诊断的告警");
}

void TestConfig::repeatedLoadReplacesPreviousState() {
    Fixture fx;
    QVERIFY(StyleManager::instance().loadConfigs(
        fx.langs.toStdString(), fx.stylers.toStdString()).ok);
    const QString other = fx.dir.filePath("other-langs.xml");
    QVERIFY(put(other, kOtherLangsXml));
    QVERIFY(StyleManager::instance().loadConfigs(
        other.toStdString(), fx.stylers.toStdString()).ok);
    const auto names = StyleManager::instance().languageNames();
    QCOMPARE(names.size(), std::size_t(1));
    QCOMPARE(QString::fromStdString(names.front()), QStringLiteral("python"));
}

void TestConfig::locateConfigFilesFindsKnownLayouts() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString bin = dir.filePath("bin");
    QVERIFY(QDir().mkpath(bin));

    // 候选含 "./langs.model.xml"，测试进程平时在 build/（那里有 POST_BUILD 复制的 XML），
    // 必须切到空目录才能真的测到「全缺失」分支。
    struct CwdRestore {
        QString saved;
        explicit CwdRestore(const QString& to) : saved(QDir::currentPath()) { QDir::setCurrent(to); }
        ~CwdRestore() { QDir::setCurrent(saved); }
    } cwd(bin);

    const ConfigPaths missing = StyleManager::locateConfigFiles(bin.toStdString());
    QVERIFY(!missing.complete());
    QVERIFY(!missing.searched.empty());

    QVERIFY(put(bin + "/langs.model.xml", ""));
    QVERIFY(put(bin + "/stylers.model.xml", ""));
    const ConfigPaths besideBinary = StyleManager::locateConfigFiles(bin.toStdString());
    QVERIFY(besideBinary.complete());
    QCOMPARE(QString::fromStdString(besideBinary.langs), bin + "/langs.model.xml");

    QFile::remove(bin + "/langs.model.xml");
    QFile::remove(bin + "/stylers.model.xml");
    const QString share = dir.filePath("share/notepad-plus-plus-linux");
    QVERIFY(QDir().mkpath(share));
    QVERIFY(put(share + "/langs.model.xml", ""));
    QVERIFY(put(share + "/stylers.model.xml", ""));
    const ConfigPaths installed = StyleManager::locateConfigFiles(bin.toStdString());
    QVERIFY(installed.complete());
    QCOMPARE(QString::fromStdString(installed.langs), share + "/langs.model.xml");
}

void TestConfig::buildDiagnosticMentionsCauseAndConsequence() {
    ConfigPaths paths;
    paths.searched.push_back("/tmp/bin/langs.model.xml");
    ConfigLoadReport report;
    report.errors.push_back("stylers.model.xml 读取失败：File not found");
    const QString text = QString::fromStdString(StyleManager::buildDiagnostic(paths, report));
    QVERIFY(text.contains(QStringLiteral("langs.model.xml")));
    QVERIFY(text.contains(QStringLiteral("stylers.model.xml")));
    QVERIFY(text.contains(QStringLiteral("语法高亮")));
    QVERIFY(text.contains(QStringLiteral("/tmp/bin/langs.model.xml")));
}
