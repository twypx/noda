#include "test_typescript.h"

#include <QtTest>
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include "EditorView.h"
#include "StyleManager.h"

namespace {

// 取自 lexilla/include/SciLexer.h，避免测试目标额外依赖 Lexilla 头文件路径。
constexpr sptr_t kCppLexer = 3;    // SCLEX_CPP
constexpr sptr_t kWordStyle = 5;   // SCE_C_WORD

bool loadRealConfigs() {
    const QString dir = QCoreApplication::applicationDirPath();
    const ConfigLoadReport report = StyleManager::instance().loadConfigs(
        (dir + QStringLiteral("/langs.model.xml")).toStdString(),
        (dir + QStringLiteral("/stylers.model.xml")).toStdString());
    return report.ok;
}

QString detect(const QString& ext) {
    return QString::fromStdString(
        StyleManager::instance().detectLanguageByExt(ext.toStdString()));
}

bool put(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(data) == data.size();
}

QString lexerProperty(EditorView* ed, const char* key) {
    char buffer[64] = {};
    ed->send(SCI_GETPROPERTY, reinterpret_cast<uptr_t>(key),
             reinterpret_cast<sptr_t>(buffer));
    return QString::fromUtf8(buffer);
}

} // namespace

void TestTypeScript::realConfigsMapTsAndTsxToTypescript() {
    QVERIFY2(loadRealConfigs(), "构建目录必须有真实的 langs/stylers 配置");
    QCOMPARE(detect(QStringLiteral("ts")), QStringLiteral("typescript"));
    QCOMPARE(detect(QStringLiteral("tsx")), QStringLiteral("typescript"));
    QCOMPARE(detect(QStringLiteral("TS")), QStringLiteral("typescript"));
}

void TestTypeScript::typescriptUsesCppLexer() {
    QVERIFY(loadRealConfigs());
    QCOMPARE(QString::fromStdString(
                 StyleManager::instance().getLexerName("typescript")),
             QStringLiteral("cpp"));
}

void TestTypeScript::configlessFallbackUsesTypescript() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    // 两份 XML 未随包提供时 _extToLang 为空，兜底同样必须认 TypeScript。
    StyleManager::instance().loadConfigs(
        dir.filePath("missing-langs.xml").toStdString(),
        dir.filePath("missing-stylers.xml").toStdString());
    QCOMPARE(detect(QStringLiteral("ts")), QStringLiteral("typescript"));
    QCOMPARE(detect(QStringLiteral("tsx")), QStringLiteral("typescript"));
    QVERIFY(loadRealConfigs());
}

void TestTypeScript::openingTsFileColoursKeywords() {
    QVERIFY(loadRealConfigs());
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("sample.ts");
    const QByteArray content =
        "interface User {\n"
        "  name: string;\n"
        "}\n"
        "const msg = `hi ${name}`;\n";
    QVERIFY(put(path, content));

    EditorView ed;
    QVERIFY2(ed.loadFromFile(path), "打开 .ts 文件必须成功");
    QCOMPARE(QString::fromStdString(ed.currentLanguage()),
             QStringLiteral("typescript"));
    // 必须真的挂上 cpp 词法器，而不是 CreateLexer 失败后静默无词法器。
    QCOMPARE(ed.send(SCI_GETLEXER), kCppLexer);
    // "interface" 在 langs.model.xml 的 instre1 里，应染成 SCE_C_WORD。
    QCOMPARE(ed.send(SCI_GETSTYLEAT, 0), kWordStyle);
    QVERIFY2(ed.send(SCI_STYLEGETFORE, kWordStyle)
                 != ed.send(SCI_STYLEGETFORE, STYLE_DEFAULT),
             "TypeScript 关键字样式必须有别于默认前景色");
}

void TestTypeScript::typescriptLexerPropertiesMatchUpstream() {
    QVERIFY(loadRealConfigs());
    EditorView ed;
    ed.applyLanguage("typescript");
    // 上游 ScintillaEditView::setTypeScriptLexer() 的两项 cpp 词法器设置。
    QCOMPARE(lexerProperty(&ed, "lexer.cpp.track.preprocessor"), QStringLiteral("0"));
    QCOMPARE(lexerProperty(&ed, "lexer.cpp.backquoted.strings"), QStringLiteral("1"));

    // 同一编辑器切回 C++ 时不能残留 TypeScript 的词法器属性。
    ed.applyLanguage("cpp");
    QCOMPARE(lexerProperty(&ed, "lexer.cpp.track.preprocessor"), QStringLiteral("1"));
    QCOMPARE(lexerProperty(&ed, "lexer.cpp.backquoted.strings"), QStringLiteral("0"));
}
