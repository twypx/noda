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
