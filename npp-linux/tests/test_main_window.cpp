#include "test_main_window.h"
#include "MainWindow.h"
#include "TabManager.h"
#include "EditorView.h"

#include <QtTest>
#include <QFile>
#include <QMessageBox>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

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

} // namespace

void TestMainWindow::saveFailureNotifiesUserAndKeepsFile() {
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("doc.txt");
    QVERIFY(put(path, QByteArray("ORIGINAL")));

    MainWindow window;
    window.openFile(path);
    auto* tabs = window.findChild<TabManager*>();
    QVERIFY(tabs);
    auto* ed = tabs->currentEditor();
    QVERIFY(ed);

    ed->buffer()->setEncoding("NO-SUCH-CHARSET-NPP");
    const char tail[] = "!";
    ed->send(SCI_APPENDTEXT, 1, reinterpret_cast<sptr_t>(tail));

    QSignalSpy spy(&window, &MainWindow::saveFailed);
    QVERIFY(spy.isValid());

    // 失败提示是模态框，在嵌套事件循环里自动关掉，避免测试进程挂住。
    QTimer::singleShot(0, [&window]() {
        const auto boxes = window.findChildren<QMessageBox*>();
        for (auto* box : boxes) box->reject();
    });

    QVERIFY(QMetaObject::invokeMethod(&window, "onSaveFile"));
    QCOMPARE(spy.count(), 1);
    QVERIFY2(spy.at(0).at(1).toString().contains("NO-SUCH-CHARSET-NPP"),
             "提示原因必须指出具体编码");
    QCOMPARE(readAll(path), QByteArray("ORIGINAL"));
    QVERIFY(ed->send(SCI_GETMODIFY) != 0);
}
