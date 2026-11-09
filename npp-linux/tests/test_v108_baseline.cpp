#include "test_v108_baseline.h"
#include "utils/CodeFormatter.h"
#include "utils/JsonFormatter.h"
#include "MainWindow.h"

#include <QtTest>
#include <QAction>
#include <QKeySequence>
#include <QString>
#include <string>

namespace {

// 两个格式化引擎的共同契约：只重排空白，去掉空白后逐字节一致。
std::string stripWhitespace(const std::string& s) {
    std::string r;
    for (unsigned char c : s) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') r.push_back(static_cast<char>(c));
    }
    return r;
}

} // namespace

void TestV108Baseline::astyleAndJsonFormattingStillWork() {
    QVERIFY(CodeFormatter::isSupported("cpp"));
    QVERIFY(CodeFormatter::isSupported("json"));
    QVERIFY(!CodeFormatter::isSupported("normal"));

    const std::string cpp = "int main(){return 0;}";
    const std::string cppFormatted = CodeFormatter::format(cpp, "cpp");
    QVERIFY2(!cppFormatted.empty(), "AStyle 格式化必须返回结果");
    QCOMPARE(QString::fromStdString(stripWhitespace(cppFormatted)),
             QString::fromStdString(stripWhitespace(cpp)));

    const std::string json = "{\"a\":1,\"b\":[2,3]}";
    const std::string jsonFormatted = JsonFormatter::format(json);
    QVERIFY2(!jsonFormatted.empty(), "JSON 格式化必须返回结果");
    QCOMPARE(QString::fromStdString(stripWhitespace(jsonFormatted)),
             QString::fromStdString(stripWhitespace(json)));
}

void TestV108Baseline::mainWindowKeepsFormatActionAndDragDrop() {
    MainWindow window;
    QVERIFY2(window.acceptDrops(), "v1.0.7 的拖拽打开能力必须保留");

    bool found = false;
    const auto actions = window.findChildren<QAction*>();
    for (QAction* action : actions) {
        if (action->text().contains(QStringLiteral("代码格式化")) && !action->shortcut().isEmpty()) {
            QCOMPARE(action->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F));
            found = true;
        }
    }
    QVERIFY2(found, "v1.0.8 的代码格式化菜单动作必须保留且带快捷键");
}
