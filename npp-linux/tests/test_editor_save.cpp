#include "test_editor_save.h"
#include "EditorView.h"

#include <QtTest>
#include <QFile>
#include <QTemporaryDir>

namespace {

bool put(const QString& path, const QByteArray& data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    return f.write(data) == data.size();
}

QByteArray readAll(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    return f.readAll();
}

void appendByte(EditorView& ed, const char* text, std::size_t len) {
    ed.send(SCI_APPENDTEXT, static_cast<uptr_t>(len), reinterpret_cast<sptr_t>(text));
}

} // namespace

void TestEditorSave::saveSuccessUsesConfiguredEncoding() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("gbk.txt");

    EditorView ed;
    ed.setTextFromUtf8("中文");
    ed.buffer()->setEncoding("GBK");
    QVERIFY(ed.saveToFile(path));
    QCOMPARE(readAll(path), QByteArray("\xD6\xD0\xCE\xC4", 4));
    QCOMPARE(ed.send(SCI_GETMODIFY), sptr_t(0));
}

void TestEditorSave::saveSuccessWritesBom() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("bom.txt");

    EditorView ed;
    ed.setTextFromUtf8("ABC");
    ed.buffer()->setEncoding("UTF-8 BOM");
    QVERIFY(ed.saveToFile(path));
    QCOMPARE(readAll(path), QByteArray("\xEF\xBB\xBF" "ABC", 6));
}

void TestEditorSave::saveFailureKeepsFileAndDirtyFlag() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("original.txt");
    QVERIFY(put(path, QByteArray("ORIGINAL")));

    EditorView ed;
    QVERIFY(ed.loadFromFile(path));
    QCOMPARE(ed.send(SCI_GETMODIFY), sptr_t(0));
    appendByte(ed, "!", 1);
    QVERIFY(ed.send(SCI_GETMODIFY) != 0);

    ed.buffer()->setEncoding("NO-SUCH-CHARSET-NPP");
    QVERIFY2(!ed.saveToFile(path), "转码失败时 saveToFile 必须返回 false");
    QVERIFY2(ed.lastSaveError().contains("NO-SUCH-CHARSET-NPP"),
             "失败原因必须带出编码名，供 UI 明确提示");
    QCOMPARE(readAll(path), QByteArray("ORIGINAL"));
    QVERIFY2(ed.send(SCI_GETMODIFY) != 0, "保存失败后文档必须仍是未保存状态");
    QVERIFY(ed.buffer()->isModified());
}

void TestEditorSave::saveCopyWritesTargetEncodingAndKeepsState() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString original = dir.filePath("orig.txt");
    const QString copy = dir.filePath("copy.txt");
    QVERIFY(put(original, QByteArray("ORIGINAL")));

    EditorView ed;
    QVERIFY(ed.loadFromFile(original));
    ed.setTextFromUtf8("中文");
    ed.buffer()->setEncoding("GBK");
    appendByte(ed, "!", 1);
    QVERIFY(ed.send(SCI_GETMODIFY) != 0);

    QVERIFY(ed.saveCopyToFile(copy));
    QCOMPARE(readAll(copy), QByteArray("\xD6\xD0\xCE\xC4!", 5));
    QCOMPARE(readAll(original), QByteArray("ORIGINAL"));
    QCOMPARE(QString::fromStdString(ed.buffer()->getFilePath()), original);
    QVERIFY2(ed.send(SCI_GETMODIFY) != 0, "拷贝另存为不能把当前文档标成已保存");
}

void TestEditorSave::saveCopyRespectsBom() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString copy = dir.filePath("bom-copy.txt");

    EditorView ed;
    ed.setTextFromUtf8("ABC");
    ed.buffer()->setEncoding("UTF-8 BOM");
    QVERIFY(ed.saveCopyToFile(copy));
    QCOMPARE(readAll(copy), QByteArray("\xEF\xBB\xBF" "ABC", 6));
    QVERIFY(ed.lastSaveError().isEmpty());
}

void TestEditorSave::saveCopyFailureWritesNothing() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString copy = dir.filePath("bad-copy.txt");

    EditorView ed;
    ed.setTextFromUtf8("内容");
    ed.buffer()->setEncoding("NO-SUCH-CHARSET-NPP");
    QVERIFY(!ed.saveCopyToFile(copy));
    QVERIFY2(!QFile::exists(copy), "转码失败不能留下半成品副本");
    QVERIFY(ed.lastSaveError().contains("NO-SUCH-CHARSET-NPP"));
}

void TestEditorSave::loadFromFileDoesNotCreateUndoStep() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("test_load.txt");
    QVERIFY(put(path, QByteArray("Line1\nLine2\nLine3\n")));

    EditorView ed;
    QVERIFY(ed.loadFromFile(path));
    // 载入后不应有可撤销动作
    QCOMPARE(ed.send(SCI_CANUNDO), sptr_t(0));

    // 执行 Undo 后内容不应丢失
    ed.doUndo();
    QCOMPARE(ed.currentTextUtf8(), std::string("Line1\nLine2\nLine3\n"));
    QCOMPARE(ed.send(SCI_CANUNDO), sptr_t(0));
}

void TestEditorSave::columnInsertVirtualSpacesBlock() {
    EditorView ed;
    // 构造两行短文本，并在第 8 列建立跨行矩形选区（超出行尾的虚拟空格区）
    ed.setTextFromUtf8("A\nB\n");
    ed.send(SCI_SETRECTANGULARSELECTIONANCHOR, 0);
    ed.send(SCI_SETRECTANGULARSELECTIONANCHORVIRTUALSPACE, 5);
    ed.send(SCI_SETRECTANGULARSELECTIONCARET, 2);
    ed.send(SCI_SETRECTANGULARSELECTIONCARETVIRTUALSPACE, 5);

    ed.columnInsertText("Z");
    const std::string text = ed.currentTextUtf8();
    QCOMPARE(text, std::string("A    Z\nB    Z\n"));

    // 单步撤销
    ed.doUndo();
    QCOMPARE(ed.currentTextUtf8(), std::string("A\nB\n"));
}

void TestEditorSave::sortLinesSupportsCrAndUndo() {
    EditorView ed;
    ed.setTextFromUtf8("banana\rappele\r");
    ed.convertLineEndings(LineEnding::CR);
    QCOMPARE(ed.buffer()->getLineEnding(), LineEnding::CR);

    ed.sortLines(true, false);
    // 排序后换行风格必须仍为 CR，不能被破坏成 LF
    QCOMPARE(ed.currentTextUtf8(), std::string("appele\rbanana\r"));

    // 撤销必须完整回退到原顺序
    ed.doUndo();
    QCOMPARE(ed.currentTextUtf8(), std::string("banana\rappele\r"));
}

void TestEditorSave::lineNumberMarginWidthDigitsCaching() {
    EditorView ed;
    QCOMPARE(ed.lineNumberMarginDigits(), 4);
    auto width4 = ed.send(SCI_GETMARGINWIDTHN, 0);
    QVERIFY(width4 > 0);

    // 增加到 50 行（仍处于 4 位数基线内）
    std::string text50;
    for (int i = 0; i < 50; ++i) text50 += "line\n";
    ed.setTextFromUtf8(text50);
    QCOMPARE(ed.lineNumberMarginDigits(), 4);
    QCOMPARE(ed.send(SCI_GETMARGINWIDTHN, 0), width4);

    // 增加到 12000 行（升级到 5 位数）
    std::string text12000;
    for (int i = 0; i < 12000; ++i) text12000 += "line\n";
    ed.setTextFromUtf8(text12000);
    QCOMPARE(ed.lineNumberMarginDigits(), 5);
    QVERIFY(ed.send(SCI_GETMARGINWIDTHN, 0) > width4);
}



