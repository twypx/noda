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
#include "test_tab_context.h"
#include "test_single_instance.h"
#include "test_preferences_dialog.h"
#include "test_typescript.h"
#include "test_performance.h"
#include "test_url_link.h"
#include "test_hex_view.h"
#include "test_localization.h"

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
    TestTabContext testTabContext;
    failures += QTest::qExec(&testTabContext, argc, argv);
    TestSingleInstance testSingleInstance;
    failures += QTest::qExec(&testSingleInstance, argc, argv);
    TestPreferencesDialog testPreferencesDialog;
    failures += QTest::qExec(&testPreferencesDialog, argc, argv);
    // 放最后：它会重新加载真实配置，避免影响前面的精简 XML 夹具。
    TestTypeScript testTypeScript;
    failures += QTest::qExec(&testTypeScript, argc, argv);
    TestPerformance testPerformance;
    failures += QTest::qExec(&testPerformance, argc, argv);
    TestUrlLink testUrlLink;
    failures += QTest::qExec(&testUrlLink, argc, argv);
    TestHexView testHexView;
    failures += QTest::qExec(&testHexView, argc, argv);
    TestLocalization testLocalization;
    failures += QTest::qExec(&testLocalization, argc, argv);

    return failures == 0 ? 0 : 1;
}
