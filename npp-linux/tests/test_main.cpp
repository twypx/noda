#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QTest>
#include <cstdlib>

#include "test_encoding.h"
#include "test_v108_baseline.h"
#include "test_editor_save.h"
#include "test_main_window.h"
#include "test_config.h"
#include "test_style_config.h"
#include "test_shortcuts.h"
#include "test_fold.h"
#include "test_folder_tree.h"
#include "test_folder_tree_main_window.h"

int main(int argc, char* argv[]) {
    // QSettings 的默认路径在 QCoreApplication 构造时确定，隔离必须在此之前。
    const QByteArray configHome = qgetenv("XDG_CONFIG_HOME");
    if (!configHome.isEmpty()) {
        QDir(QString::fromLocal8Bit(configHome)).removeRecursively();
        QDir().mkpath(QString::fromLocal8Bit(configHome));
    }
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("Notepad++");
    QCoreApplication::setOrganizationName("Notepad++");

    int failures = 0;
    TestEncoding testEncoding;
    failures += QTest::qExec(&testEncoding, argc, argv);
    TestV108Baseline testV108Baseline;
    failures += QTest::qExec(&testV108Baseline, argc, argv);
    TestEditorSave testEditorSave;
    failures += QTest::qExec(&testEditorSave, argc, argv);
    TestMainWindow testMainWindow;
    failures += QTest::qExec(&testMainWindow, argc, argv);
    TestConfig testConfig;
    failures += QTest::qExec(&testConfig, argc, argv);
    TestStyleConfig testStyleConfig;
    failures += QTest::qExec(&testStyleConfig, argc, argv);
    TestShortcuts testShortcuts;
    failures += QTest::qExec(&testShortcuts, argc, argv);
    TestFold testFold;
    failures += QTest::qExec(&testFold, argc, argv);
    TestFolderTree testFolderTree;
    failures += QTest::qExec(&testFolderTree, argc, argv);
    TestFolderTreeMainWindow testFolderTreeMainWindow;
    failures += QTest::qExec(&testFolderTreeMainWindow, argc, argv);

    return failures == 0 ? 0 : 1;
}
