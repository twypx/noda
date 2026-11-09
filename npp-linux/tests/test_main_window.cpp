#include "test_main_window.h"
#include "MainWindow.h"
#include "TabManager.h"
#include "EditorView.h"
#include "dialogs/AboutDlg.h"
#include "Version.h"

#include <QtTest>
#include <QFile>
#include <QMenu>
#include <QMenuBar>
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

void TestMainWindow::backupOnlyWhenDirty() {
    MainWindow window;
    auto* tabs = window.findChild<TabManager*>();
    QVERIFY(tabs);
    auto* ed = tabs->currentEditor();
    QVERIFY(ed);

    const char text1[] = "Hello Backup";
    ed->send(SCI_APPENDTEXT, static_cast<uptr_t>(sizeof(text1) - 1), reinterpret_cast<sptr_t>(text1));
    QVERIFY(ed->buffer()->isModified());

    window.syncBackups();
    const QString backupPath = QString::fromStdString(ed->buffer()->getBackupFileName());
    QVERIFY(!backupPath.isEmpty());
    QVERIFY(QFile::exists(backupPath));
    QCOMPARE(readAll(backupPath), QByteArray(text1));

    const QDateTime firstModified = QFileInfo(backupPath).lastModified();
    QTest::qWait(50);

    window.syncBackups();
    const QDateTime secondModified = QFileInfo(backupPath).lastModified();
    QCOMPARE(secondModified, firstModified);

    const char text2[] = " More";
    ed->send(SCI_APPENDTEXT, static_cast<uptr_t>(sizeof(text2) - 1), reinterpret_cast<sptr_t>(text2));
    QTest::qWait(50);

    window.syncBackups();
    QCOMPARE(readAll(backupPath), QByteArray("Hello Backup More"));
    const QDateTime thirdModified = QFileInfo(backupPath).lastModified();
    QVERIFY(thirdModified >= secondModified);
}

void TestMainWindow::helpMenuAndAboutActionExist() {
    MainWindow window;
    auto* bar = window.findChild<QMenuBar*>();
    QVERIFY(bar);

    QMenu* helpMenu = nullptr;
    for (auto* act : bar->actions()) {
        if (act->text().contains(QStringLiteral("帮助"))) {
            helpMenu = act->menu();
            break;
        }
    }
    QVERIFY2(helpMenu, "菜单栏必须包含'帮助'菜单");

    QAction* aboutAct = nullptr;
    for (auto* act : helpMenu->actions()) {
        if (act->text().contains(QStringLiteral("关于"))) {
            aboutAct = act;
            break;
        }
    }
    QVERIFY2(aboutAct, "帮助菜单必须包含'关于'子菜单项");
    QCOMPARE(aboutAct->shortcut(), QKeySequence(Qt::Key_F1));
    QVERIFY2(!aboutAct->icon().isNull(), "关于动作必须配置图标");
}

void TestMainWindow::aboutDialogContentMatchesVersion() {
    MainWindow window;
    AboutDlg dlg(&window);
    QVERIFY(dlg.windowTitle().contains(QStringLiteral("关于")));
    QVERIFY2(dlg.versionString().contains(QString::fromUtf8(NPP_VERSION_STRING)), "关于对话框必须显示正确的版本号");
    QVERIFY(dlg.versionString().contains("64-bit"));
    QVERIFY(!dlg.buildTime().isEmpty());
}


