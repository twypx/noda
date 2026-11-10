#include "test_single_instance.h"
#include "utils/SingleInstance.h"
#include "MainWindow.h"
#include "TabManager.h"

#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QCoreApplication>

namespace {
QString uniqueServerName() {
    return QStringLiteral("npp_test_socket_%1").arg(QCoreApplication::applicationPid());
}
} // namespace

void TestSingleInstance::serverListensAndRecoversStaleSocket() {
    const QString serverName = uniqueServerName();
    SingleInstance inst1;
    QVERIFY(inst1.listen(serverName));

    // 再次在相同名称上 listen（模拟异常残留/重入）：应能恢复并继续成功监听
    SingleInstance inst2;
    // inst1 正在运行时，inst2 会检测到已有实例
    QVERIFY(!inst2.listen(serverName));

    // inst1 退出监听
    inst1.close();
    // inst2 现在应能成功接管
    QVERIFY(inst2.listen(serverName));
    inst2.close();
}

void TestSingleInstance::clientSendsFileArgumentsToServer() {
    const QString serverName = uniqueServerName();
    SingleInstance server;
    QVERIFY(server.listen(serverName));

    QSignalSpy spy(&server, &SingleInstance::openFilesRequested);
    QVERIFY(spy.isValid());

    const QStringList files = {QStringLiteral("/tmp/test_a.txt"), QStringLiteral("/tmp/test_b.txt")};
    bool sent = SingleInstance::trySendFilesToRunningInstance(files, serverName);
    QVERIFY(sent);

    // 等待异步 IPC 事件送达
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toStringList(), files);

    server.close();
}

void TestSingleInstance::mainWindowOpensTabOnIpcMessage() {
    MainWindow window;
    auto* tabs = window.findChild<TabManager*>();
    QVERIFY(tabs);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString filePath = tempDir.filePath("sample.txt");
    QFile f(filePath);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("IPC Sample Content\n");
    f.close();

    const int initialCount = tabs->count();
    // 将初始标签设为已修改或有内容，避免触发“复用空白初始标签”逻辑，确保验证“新开标签页”能力
    auto* cur = tabs->currentEditor();
    QVERIFY(cur);
    cur->send(SCI_SETTEXT, 0, reinterpret_cast<sptr_t>("Existing Content"));

    // 触发 IPC 文件打开接口
    window.openFilesFromIpc({filePath});

    // 必须在已有窗口内新开标签，而不是新起进程
    QCOMPARE(tabs->count(), initialCount + 1);
    auto* ed = tabs->currentEditor();
    QVERIFY(ed);
    QCOMPARE(QString::fromStdString(ed->buffer()->getFilePath()), filePath);
}
