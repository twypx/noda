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
#include "test_scroll.h"
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
#include "test_diff_engine.h"
#include "test_update.h"

int main(int argc, char* argv[]) {
    // 强制隔离测试沙箱：无论外部是否提供 XDG_CONFIG_HOME，都确保指向独立的空目录，
    // 绝对不读取宿主真实 ~/.config/Noda 下的 session.xml/backup，防止测试互串。
    QByteArray configHome = qgetenv("XDG_CONFIG_HOME");
    if (configHome.isEmpty()) {
        const QString tempConfig = QDir::tempPath() + QStringLiteral("/noda_test_config");
        configHome = tempConfig.toLocal8Bit();
        qputenv("XDG_CONFIG_HOME", configHome);
    }
    QDir(QString::fromLocal8Bit(configHome)).removeRecursively();
    QDir().mkpath(QString::fromLocal8Bit(configHome));
    qputenv("QT_QPA_PLATFORM", "offscreen");

    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Noda"));
    QCoreApplication::setOrganizationName(QStringLiteral("Noda"));

    auto resetConfigSandbox = [&configHome]() {
        QDir(QString::fromLocal8Bit(configHome)).removeRecursively();
        QDir().mkpath(QString::fromLocal8Bit(configHome));
    };

    int failures = 0;
    resetConfigSandbox();
    TestEncoding testEncoding;
    failures += QTest::qExec(&testEncoding, argc, argv);
    resetConfigSandbox();
    TestV108Baseline testV108Baseline;
    failures += QTest::qExec(&testV108Baseline, argc, argv);
    resetConfigSandbox();
    TestEditorSave testEditorSave;
    failures += QTest::qExec(&testEditorSave, argc, argv);
    resetConfigSandbox();
    TestMainWindow testMainWindow;
    failures += QTest::qExec(&testMainWindow, argc, argv);
    resetConfigSandbox();
    TestConfig testConfig;
    failures += QTest::qExec(&testConfig, argc, argv);
    resetConfigSandbox();
    TestStyleConfig testStyleConfig;
    failures += QTest::qExec(&testStyleConfig, argc, argv);
    resetConfigSandbox();
    TestShortcuts testShortcuts;
    failures += QTest::qExec(&testShortcuts, argc, argv);
    resetConfigSandbox();
    TestScroll testScroll;
    failures += QTest::qExec(&testScroll, argc, argv);
    resetConfigSandbox();
    TestFold testFold;
    failures += QTest::qExec(&testFold, argc, argv);
    resetConfigSandbox();
    TestFolderTree testFolderTree;
    failures += QTest::qExec(&testFolderTree, argc, argv);
    resetConfigSandbox();
    TestFolderTreeMainWindow testFolderTreeMainWindow;
    failures += QTest::qExec(&testFolderTreeMainWindow, argc, argv);
    resetConfigSandbox();
    TestTabContext testTabContext;
    failures += QTest::qExec(&testTabContext, argc, argv);
    resetConfigSandbox();
    TestSingleInstance testSingleInstance;
    failures += QTest::qExec(&testSingleInstance, argc, argv);
    resetConfigSandbox();
    TestPreferencesDialog testPreferencesDialog;
    failures += QTest::qExec(&testPreferencesDialog, argc, argv);
    resetConfigSandbox();
    TestTypeScript testTypeScript;
    failures += QTest::qExec(&testTypeScript, argc, argv);
    resetConfigSandbox();
    TestPerformance testPerformance;
    failures += QTest::qExec(&testPerformance, argc, argv);
    resetConfigSandbox();
    TestUrlLink testUrlLink;
    failures += QTest::qExec(&testUrlLink, argc, argv);
    resetConfigSandbox();
    TestHexView testHexView;
    failures += QTest::qExec(&testHexView, argc, argv);
    resetConfigSandbox();
    TestLocalization testLocalization;
    failures += QTest::qExec(&testLocalization, argc, argv);
    resetConfigSandbox();
    TestDiffEngine testDiffEngine;
    failures += QTest::qExec(&testDiffEngine, argc, argv);
    resetConfigSandbox();
    TestUpdate testUpdate;
    failures += QTest::qExec(&testUpdate, argc, argv);

    return failures == 0 ? 0 : 1;
}
