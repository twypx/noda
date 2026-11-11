#include "test_diff_engine.h"
#include "utils/DiffEngine.h"
#include <QCheckBox>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QPoint>
#include <QAction>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QPointer>
#include <QMenu>
#include <QPushButton>
#include "widgets/DiffMiniMap.h"
#include "utils/ConfigMigration.h"

void TestDiffEngine::identicalTextYieldsNoDiff()
{
    const std::string text = "Line 1\nLine 2\nLine 3\nLine 4\n";
    Diff::DiffOptions opt;
    auto res = Diff::DiffEngine::computeDiff(text, text, opt);

    QCOMPARE(res.addedCount, 0);
    QCOMPARE(res.removedCount, 0);
    QCOMPARE(res.modifiedCount, 0);
    for (const auto& l : res.lines) {
        QCOMPARE(l.type, Diff::ChangeType::Unchanged);
    }
}

void TestDiffEngine::pureAddAndRemove()
{
    const std::string textA = "Line 1\nLine 2\nLine 3\n";
    const std::string textB = "Line 1\nLine inserted\nLine 2\n";

    Diff::DiffOptions opt;
    auto res = Diff::DiffEngine::computeDiff(textA, textB, opt);

    QVERIFY(res.addedCount > 0 || res.modifiedCount > 0);
    // Line inserted should be present in B
    bool foundInserted = false;
    for (const auto& l : res.lines) {
        if (l.type == Diff::ChangeType::Added) {
            foundInserted = true;
        }
    }
    QVERIFY(foundInserted);

    // Test pure removal
    const std::string textC = "Line 1\nLine 3\n";
    auto res2 = Diff::DiffEngine::computeDiff(textA, textC, opt);
    QVERIFY(res2.removedCount > 0);
}

void TestDiffEngine::modifiedLineAndIntraLineDiff()
{
    const std::string textA = "const int count = 100;\n";
    const std::string textB = "const int total = 100;\n";

    Diff::DiffOptions opt;
    auto res = Diff::DiffEngine::computeDiff(textA, textB, opt);

    // 行数与 Scintilla 一致：末尾换行后还有一空行（Modified + Unchanged）。
    QCOMPARE(res.lines.size(), static_cast<size_t>(2));
    QCOMPARE(res.lines[0].type, Diff::ChangeType::Modified);
    QCOMPARE(res.modifiedCount, 1);

    // 行内差异必须落在 "count" -> "total" 这 5 个字符区间 [10,15) 内；
    // 真实的字符级 diff 可能给出多段，而不是把整词当作一段。
    QVERIFY(!res.lines[0].intraA.empty());
    QVERIFY(!res.lines[0].intraB.empty());
    for (const auto& r : res.lines[0].intraA) {
        QVERIFY2(r.start >= 10 && r.start + r.length <= 15, "intraA range out of 'count'");
        QVERIFY(r.length > 0);
    }
    for (const auto& r : res.lines[0].intraB) {
        QVERIFY2(r.start >= 10 && r.start + r.length <= 15, "intraB range out of 'total'");
        QVERIFY(r.length > 0);
    }
}

void TestDiffEngine::ignoreWhitespaceAndCase()
{
    const std::string textA = "   Hello World   \n";
    const std::string textB = "hello world\n";

    Diff::DiffOptions optStrict;
    auto resStrict = Diff::DiffEngine::computeDiff(textA, textB, optStrict);
    QVERIFY(resStrict.modifiedCount > 0 || resStrict.addedCount > 0);

    Diff::DiffOptions optIgnore;
    optIgnore.ignoreWhitespace = true;
    optIgnore.ignoreCase = true;
    auto resIgnore = Diff::DiffEngine::computeDiff(textA, textB, optIgnore);
    QCOMPARE(resIgnore.addedCount, 0);
    QCOMPARE(resIgnore.removedCount, 0);
    QCOMPARE(resIgnore.modifiedCount, 0);
}

void TestDiffEngine::alignmentMappingCalculatesCorrectVirtualRows()
{
    const std::string textA = "A\nB\nC\n";
    const std::string textB = "A\nINS\nB\nC\n";

    Diff::DiffOptions opt;
    auto res = Diff::DiffEngine::computeDiff(textA, textB, opt);
    auto align = Diff::DiffEngine::computeAlignment(res, 3, 4);

    // Alignment list should align A with A, blank on A side for INS on B side, B with B, C with C,
    // 以及末尾换行后的空行（两侧各一行）。
    QCOMPARE(align.size(), static_cast<size_t>(5));
    QCOMPARE(align[0].lineA, 0);
    QCOMPARE(align[0].lineB, 0);
    QCOMPARE(align[0].type, Diff::ChangeType::Unchanged);

    // Row 1: A side has blank (-1), B side has 1 ("INS")
    QCOMPARE(align[1].lineA, -1);
    QCOMPARE(align[1].lineB, 1);
    QCOMPARE(align[1].type, Diff::ChangeType::Added);

    // Row 2: B with B
    QCOMPARE(align[2].lineA, 1);
    QCOMPARE(align[2].lineB, 2);

    // Row 3: C with C
    QCOMPARE(align[3].lineA, 2);
    QCOMPARE(align[3].lineB, 3);

    // Row 4: trailing empty line
    QCOMPARE(align[4].lineA, 3);
    QCOMPARE(align[4].lineB, 4);
}

void TestDiffEngine::multiSegmentIntraLineDiff()
{
    const std::string textA = "abcXdefYghi\n";
    const std::string textB = "abcZdefWghi\n";

    Diff::DiffOptions opt;
    auto res = Diff::DiffEngine::computeDiff(textA, textB, opt);

    QCOMPARE(res.modifiedCount, 1);
    QCOMPARE(res.lines.size(), static_cast<size_t>(2)); // Modified + trailing empty
    const auto& diff = res.lines[0];
    QCOMPARE(diff.type, Diff::ChangeType::Modified);

    // 两处独立修改必须得到两段区间，而不是被合并成一大段。
    QCOMPARE(diff.intraA.size(), static_cast<size_t>(2));
    QCOMPARE(diff.intraB.size(), static_cast<size_t>(2));
    QCOMPARE(diff.intraA[0].start, 3); // X
    QCOMPARE(diff.intraA[0].length, 1);
    QCOMPARE(diff.intraA[1].start, 7); // Y
    QCOMPARE(diff.intraA[1].length, 1);
    QCOMPARE(diff.intraB[0].start, 3); // Z
    QCOMPARE(diff.intraB[1].start, 7); // W
}

// 最坏情况：两份文档每一行都不同、无任何锚点。必须走有界路径，不能 O(N²) 爆内存。
void TestDiffEngine::worstCaseAllLinesDifferentIsBounded()
{
    constexpr int kLines = 20000;
    std::string textA;
    std::string textB;
    textA.reserve(static_cast<size_t>(kLines) * 16);
    textB.reserve(static_cast<size_t>(kLines) * 16);
    for (int i = 0; i < kLines; ++i) {
        textA += "A_" + std::to_string(i) + "\n";
        textB += "B_" + std::to_string(i) + "\n";
    }

    QElapsedTimer timer;
    timer.start();
    Diff::DiffOptions opt;
    auto res = Diff::DiffEngine::computeDiff(textA, textB, opt);
    const qint64 elapsedMs = timer.elapsed();

    QVERIFY2(elapsedMs < 500, qPrintable(QStringLiteral("worst-case diff too slow: %1 ms").arg(elapsedMs)));
    // 整块替换按位置配对：每行都应是 Modified。
    QCOMPARE(res.modifiedCount, kLines);
    QCOMPARE(res.addedCount, 0);
    QCOMPARE(res.removedCount, 0);
}

// 只在头部插入一行：应通过锚点/LIS 快速定位，而不是全量 Myers。
void TestDiffEngine::shiftedFileUsesAnchorsAndIsFast()
{
    constexpr int kLines = 20000;
    std::string base;
    base.reserve(static_cast<size_t>(kLines) * 16);
    for (int i = 0; i < kLines; ++i) {
        base += "line_" + std::to_string(i) + "\n";
    }
    const std::string textA = base;
    const std::string textB = "INSERTED\n" + base;

    QElapsedTimer timer;
    timer.start();
    Diff::DiffOptions opt;
    auto res = Diff::DiffEngine::computeDiff(textA, textB, opt);
    const qint64 elapsedMs = timer.elapsed();

    QVERIFY2(elapsedMs < 300, qPrintable(QStringLiteral("anchored diff too slow: %1 ms").arg(elapsedMs)));
    QCOMPARE(res.addedCount, 1);
    QCOMPARE(res.removedCount, 0);
    QCOMPARE(res.modifiedCount, 0);
}

#include "widgets/CompareWidget.h"
#include "EditorView.h"

void TestDiffEngine::compareWidgetRendersMarkersAndAnnotations()
{
    auto* edA = new EditorView();
    auto* edB = new EditorView();
    edA->setTextFromUtf8("Line 1\nLine 2 to modify\nLine 3\n");
    edB->setTextFromUtf8("Line 1\nLine 2 modified!\nLine inserted\nLine 3\n");

    {
        CompareWidget cmpWidget(edA, edB);

        QVERIFY(cmpWidget.totalDiffCount() > 0);
        QCOMPARE(cmpWidget.modifiedCount(), 1);
        QCOMPARE(cmpWidget.addedCount(), 1);

        // 检查 Marker 标记是否存在
        auto maskA = edA->send(SCI_MARKERGET, 1);
        QVERIFY(maskA & (1 << CompareWidget::MARK_DIFF_MODIFIED));

        auto maskB_mod = edB->send(SCI_MARKERGET, 1);
        QVERIFY(maskB_mod & (1 << CompareWidget::MARK_DIFF_MODIFIED));

        auto maskB_add = edB->send(SCI_MARKERGET, 2);
        QVERIFY(maskB_add & (1 << CompareWidget::MARK_DIFF_ADDED));

        // 导航测试
        cmpWidget.nextDiff();
        QVERIFY(cmpWidget.currentDiffIndex() >= 0);

        // 清理测试
        cmpWidget.clearDiff();
        QCOMPARE(edA->send(SCI_MARKERGET, 1), 0);
        QCOMPARE(edB->send(SCI_MARKERGET, 2), 0);
    }

    delete edA;
    delete edB;
}

void TestDiffEngine::compareWidgetIgnoreOptions()
{
    auto* edA = new EditorView();
    auto* edB = new EditorView();
    edA->setTextFromUtf8("   Hello World   \n");
    edB->setTextFromUtf8("hello world\n");

    {
        CompareWidget cmpWidget(edA, edB);
        QVERIFY(cmpWidget.modifiedCount() > 0 || cmpWidget.addedCount() > 0);

        const auto boxes = cmpWidget.findChildren<QCheckBox*>();
        QCOMPARE(boxes.size(), 2);
        for (QCheckBox* box : boxes) box->setChecked(true);

        QCOMPARE(cmpWidget.modifiedCount(), 0);
        QCOMPARE(cmpWidget.addedCount(), 0);
        QCOMPARE(cmpWidget.removedCount(), 0);
        QCOMPARE(cmpWidget.totalDiffCount(), 0);

        cmpWidget.clearDiff();
    }

    delete edA;
    delete edB;
}

#include "MainWindow.h"
#include "TabManager.h"

void TestDiffEngine::mainWindowCompareActions()
{
    MainWindow win;
    win.show();

    auto* tabMgr = win.findChild<TabManager*>();
    QVERIFY(tabMgr != nullptr);

    // 默认已有 1 个标签，再建 1 个标签
    tabMgr->createNewTab();
    QVERIFY(tabMgr->count() >= 2);

    auto* ed1 = tabMgr->editorAt(0);
    auto* ed2 = tabMgr->editorAt(1);
    QVERIFY(ed1 != nullptr && ed2 != nullptr);

    ed1->setTextFromUtf8("hello\nworld\n");
    ed2->setTextFromUtf8("hello\nmodified world\n");

    auto* actCompare = win.findChild<QAction*>("actCompare");
    auto* actClear = win.findChild<QAction*>("actClearCompare");
    auto* actNext = win.findChild<QAction*>("actNextDiff");
    auto* actPrev = win.findChild<QAction*>("actPrevDiff");

    QVERIFY(actCompare != nullptr);
    QVERIFY(actClear != nullptr);
    QVERIFY(actNext != nullptr);
    QVERIFY(actPrev != nullptr);

    QVERIFY(!actClear->isEnabled());

    // 触发对比
    actCompare->trigger();

    auto* cmpWidget = win.findChild<CompareWidget*>();
    QVERIFY(cmpWidget != nullptr);
    QCOMPARE(win.centralWidget(), cmpWidget);
    // 比对必须使用独立副本，绝不能把 TabManager 的页面抢进 splitter。
    QVERIFY(cmpWidget->editorA() != ed1);
    QVERIFY(cmpWidget->editorB() != ed2);
    QVERIFY(actClear->isEnabled());
    QVERIFY(actNext->isEnabled());
    QVERIFY(actPrev->isEnabled());

    // 差异导航
    actNext->trigger();
    QVERIFY(cmpWidget->currentDiffIndex() >= 0);

    // 关闭对比
    actClear->trigger();
    QCOMPARE(win.centralWidget(), tabMgr);
    QVERIFY(!actClear->isEnabled());
    QVERIFY(!actNext->isEnabled());

    // 关闭对比后，原标签页与编辑器必须仍然完好可用。
    QCOMPARE(tabMgr->count() >= 2, true);
    QCOMPARE(tabMgr->editorAt(0), ed1);
    QCOMPARE(tabMgr->editorAt(1), ed2);
    QCOMPARE(QString::fromStdString(ed1->currentTextUtf8()), QStringLiteral("hello\nworld\n"));
    QCOMPARE(QString::fromStdString(ed2->currentTextUtf8()), QStringLiteral("hello\nmodified world\n"));
}

// P.7：wyhash 必须确定、可区分，且长行（8 字节块读路径）同样稳定。
void TestDiffEngine::wyhashIsDeterministicAndDispersive()
{
    QCOMPARE(Diff::DiffEngine::hashLine("hello"), Diff::DiffEngine::hashLine("hello"));
    QVERIFY(Diff::DiffEngine::hashLine("hello") != Diff::DiffEngine::hashLine("world"));
    QVERIFY(Diff::DiffEngine::hashLine("") != 0);
    const std::string longLine(4096, 'x');
    QCOMPARE(Diff::DiffEngine::hashLine(longLine), Diff::DiffEngine::hashLine(longLine));
    QVERIFY(Diff::DiffEngine::hashLine(longLine) != Diff::DiffEngine::hashLine(longLine + "y"));
}

// P.7：内容相同、仅位置变化的块应识别为 Moved，且对侧行号正确。
void TestDiffEngine::movedLinesAreDetected()
{
    const std::string textA = "head\nalpha\nbeta\ngamma\ntail\n";
    const std::string textB = "head\nbeta\ngamma\nalpha\ntail\n";
    const auto res = Diff::DiffEngine::computeDiff(textA, textB, Diff::DiffOptions{});

    QVERIFY2(res.movedCount >= 1, "应识别出至少一个被移动的块");
    bool found = false;
    for (const auto& line : res.lines) {
        if (line.type == Diff::ChangeType::Moved) {
            found = true;
            QVERIFY(line.lineA >= 0 && line.lineB >= 0);
            QVERIFY(line.movedToLine >= 0);
        }
    }
    QVERIFY(found);
}

// P.7：detectMoves=false 时不做移动检测。
void TestDiffEngine::moveDetectionCanBeDisabled()
{
    const std::string textA = "head\nalpha\nbeta\ngamma\ntail\n";
    const std::string textB = "head\nbeta\ngamma\nalpha\ntail\n";
    Diff::DiffOptions opt;
    opt.detectMoves = false;
    const auto res = Diff::DiffEngine::computeDiff(textA, textB, opt);
    QCOMPARE(res.movedCount, 0);
}

// P.7：超过阈值的文本必须走工作线程，构造不阻塞，完成后结果完整。
void TestDiffEngine::asyncDiffDoesNotBlockUi()
{
    auto* edA = new EditorView();
    auto* edB = new EditorView();
    std::string a;
    std::string b;
    a.reserve(2 * 1024 * 1024);
    b.reserve(2 * 1024 * 1024);
    for (int i = 0; i < 90000; ++i) {
        a += "A_" + std::to_string(i) + "_padding_padding_padding\n";
        b += "B_" + std::to_string(i) + "_padding_padding_padding\n";
    }
    edA->setTextFromUtf8(a);
    edB->setTextFromUtf8(b);

    {
        CompareWidget cmpWidget(edA, edB);
        QVERIFY2(cmpWidget.isComputing(), "大文本应走异步路径，构造期间 UI 不阻塞");
        QTRY_VERIFY_WITH_TIMEOUT(!cmpWidget.isComputing(), 15000);
        QVERIFY(cmpWidget.totalDiffCount() > 0);
        QVERIFY(cmpWidget.miniMap()->diffCount() > 0);
        cmpWidget.clearDiff();
    }
    delete edA;
    delete edB;
}

// P.7：垂直滚动按 alignment 的文档行映射，而不是照搬滚动条 value。
void TestDiffEngine::scrollSyncMapsAlignedDocumentLines()
{
    auto* edA = new EditorView();
    auto* edB = new EditorView();
    edA->setTextFromUtf8("A\nB\nC\n");
    edB->setTextFromUtf8("A\nINS\nB\nC\n");
    {
        CompareWidget cmpWidget(edA, edB);
        QVERIFY(!cmpWidget.isComputing()); // 小文本同步路径
        // alignment: 0/0, -1/1(Added), 1/2, 2/3, 3/4(末尾空行)
        QCOMPARE(cmpWidget.alignedLineBForLineA(0), 0);
        QCOMPARE(cmpWidget.alignedLineBForLineA(1), 2);
        QCOMPARE(cmpWidget.alignedLineBForLineA(2), 3);
        QCOMPARE(cmpWidget.alignedLineAForLineB(2), 1);
        cmpWidget.clearDiff();
    }
    delete edA;
    delete edB;
}

// P.7：小地图把点击纵坐标吸附到最近的差异并发出跳转信号。
void TestDiffEngine::miniMapClickNavigatesToDiff()
{
    DiffMiniMap map;
    map.resize(14, 200);

    Diff::AlignmentList align(20);
    for (auto& item : align) item.type = Diff::ChangeType::Unchanged;
    align[7].type = Diff::ChangeType::Modified;
    align[15].type = Diff::ChangeType::Added;
    map.setAlignment(align);

    QCOMPARE(map.diffCount(), 2);
    QCOMPARE(map.indexAtY(0), 7);     // 吸附到最近差异
    QCOMPARE(map.indexAtY(150), 15);

    QSignalSpy spy(&map, &DiffMiniMap::navigateTo);
    QVERIFY(spy.isValid());
    QTest::mouseClick(&map, Qt::LeftButton, Qt::NoModifier, QPoint(7, 150));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 15);
}

// P.7：选区比对只把选中的文本放进副本视图，不夹带前后缀。
void TestDiffEngine::selectionCompareUsesSelectedTextOnly()
{
    MainWindow win;
    win.show();
    auto* tabMgr = win.findChild<TabManager*>();
    QVERIFY(tabMgr != nullptr);
    tabMgr->createNewTab();

    auto* ed1 = tabMgr->editorAt(0);
    auto* ed2 = tabMgr->editorAt(1);
    QVERIFY(ed1 != nullptr && ed2 != nullptr);
    ed1->setTextFromUtf8("prefix\nSELECT_A_1\nSELECT_A_2\nsuffix\n");
    ed2->setTextFromUtf8("prefix\nSELECT_B_1\nSELECT_B_2\nsuffix\n");

    // 选中各自中间两行：[7, 29)
    ed1->send(SCI_SETSELECTIONSTART, 7);
    ed1->send(SCI_SETSELECTIONEND, 29);
    ed2->send(SCI_SETSELECTIONSTART, 7);
    ed2->send(SCI_SETSELECTIONEND, 29);

    auto* act = win.findChild<QAction*>("actCompareSelection");
    QVERIFY(act != nullptr);
    act->trigger();

    auto* cmp = win.findChild<CompareWidget*>();
    QVERIFY(cmp != nullptr);
    QCOMPARE(QString::fromStdString(cmp->editorA()->currentTextUtf8()),
             QStringLiteral("SELECT_A_1\nSELECT_A_2\n"));
    QCOMPARE(QString::fromStdString(cmp->editorB()->currentTextUtf8()),
             QStringLiteral("SELECT_B_1\nSELECT_B_2\n"));
    QVERIFY(cmp->totalDiffCount() > 0);
}

// P.7：改名后旧配置一次性迁移到 Noda 目录，且不覆盖已有新配置、可重复调用。
void TestDiffEngine::legacyConfigIsMigratedOnce()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString legacyDir = tmp.path() + QStringLiteral("/Notepad++");
    QVERIFY(QDir().mkpath(legacyDir));

    QFile conf(legacyDir + QStringLiteral("/Notepad++.conf"));
    QVERIFY(conf.open(QIODevice::WriteOnly));
    conf.write("[prefs]\nuiLanguage=zh_CN\n");
    conf.close();
    QFile session(legacyDir + QStringLiteral("/session.xml"));
    QVERIFY(session.open(QIODevice::WriteOnly));
    session.write("<Noda/>");
    session.close();

    QVERIFY(ConfigMigration::migrateLegacy(tmp.path()));
    const QString newConf = tmp.path() + QStringLiteral("/Noda/Noda.conf");
    QVERIFY(QFile::exists(newConf));
    QVERIFY(QFile::exists(tmp.path() + QStringLiteral("/Noda/session.xml")));
    QFile out(newConf);
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(out.readAll(), QByteArray("[prefs]\nuiLanguage=zh_CN\n"));
    out.close();

    // 幂等：已有新配置时不再迁移，也不覆盖用户后来的修改。
    QVERIFY(!ConfigMigration::migrateLegacy(tmp.path()));
}

// P.7：Git / SVN 比对入口已接线；未保存文件触发时给出提示且不创建比对视图。
void TestDiffEngine::vcsCompareActionsAreWired()
{
    MainWindow win;
    win.show();
    auto* actGit = win.findChild<QAction*>("actCompareGit");
    auto* actSvn = win.findChild<QAction*>("actCompareSvn");
    auto* actSelection = win.findChild<QAction*>("actCompareSelection");
    QVERIFY(actGit != nullptr);
    QVERIFY(actSvn != nullptr);
    QVERIFY(actSelection != nullptr);

    // 新建未保存文档：应安全返回，不产生比对视图、不崩溃。
    actGit->trigger();
    actSvn->trigger();
    QVERIFY(win.findChild<CompareWidget*>() == nullptr);
}

namespace {
// 造两份「内容完全不同」的中等文本，稳定触发 >256KB 的异步比对路径。
void fillLargeDifferingText(EditorView* a, EditorView* b, int lines) {
    std::string textA;
    std::string textB;
    textA.reserve(static_cast<size_t>(lines) * 40);
    textB.reserve(static_cast<size_t>(lines) * 40);
    for (int i = 0; i < lines; ++i) {
        textA += "A_" + std::to_string(i) + "_padding_padding_padding\n";
        textB += "B_" + std::to_string(i) + "_padding_padding_padding\n";
    }
    a->setTextFromUtf8(textA);
    b->setTextFromUtf8(textB);
}
} // namespace

// 进入对比模式后持续跑事件循环：验证滚动同步/annotation/异步回调不会导致崩溃。
void TestDiffEngine::compareViewStableAcrossEventLoop()
{
    MainWindow win;
    win.show();
    auto* tabMgr = win.findChild<TabManager*>();
    QVERIFY(tabMgr != nullptr);
    tabMgr->createNewTab();
    auto* ed1 = tabMgr->editorAt(0);
    auto* ed2 = tabMgr->editorAt(1);
    QVERIFY(ed1 != nullptr && ed2 != nullptr);
    ed1->setTextFromUtf8("head\nA1\nA2\nA3\ntail\n");
    ed2->setTextFromUtf8("head\nB1\nINSERTED\nB2\nB3\ntail\n");

    auto* actCompare = win.findChild<QAction*>("actCompare");
    QVERIFY(actCompare != nullptr);
    actCompare->trigger();
    auto* cmp = win.findChild<CompareWidget*>();
    QVERIFY(cmp != nullptr);

    for (int i = 0; i < 60; ++i) {
        QCoreApplication::processEvents();
        QTest::qWait(5);
    }
    QVERIFY(cmp->totalDiffCount() > 0);
    actCompare->trigger(); // 已处于对比模式时走 refreshDiff()
    for (int i = 0; i < 20; ++i) {
        QCoreApplication::processEvents();
        QTest::qWait(5);
    }
    auto* actClear = win.findChild<QAction*>("actClearCompare");
    QVERIFY(actClear != nullptr);
    actClear->trigger();
}

// 异步计算在途时反复 refreshDiff / 切换选项，验证 generation 防护有效且不崩溃。
void TestDiffEngine::repeatedRefreshWhileAsyncIsSafe()
{
    auto* edA = new EditorView();
    auto* edB = new EditorView();
    fillLargeDifferingText(edA, edB, 20000);
    {
        CompareWidget cmpWidget(edA, edB);
        QVERIFY(cmpWidget.isComputing());
        for (int i = 0; i < 8; ++i) {
            cmpWidget.refreshDiff();
            QCoreApplication::processEvents();
        }
        QTRY_VERIFY_WITH_TIMEOUT(!cmpWidget.isComputing(), 15000);
        QVERIFY(cmpWidget.totalDiffCount() > 0);
        cmpWidget.clearDiff();
    }
    delete edA;
    delete edB;
}

// 异步计算在途时直接销毁 CompareWidget（等价于用户立刻关闭对比）。
void TestDiffEngine::asyncCompareCloseDuringComputationIsSafe()
{
    auto* edA = new EditorView();
    auto* edB = new EditorView();
    fillLargeDifferingText(edA, edB, 30000);
    {
        auto* cmpWidget = new CompareWidget(edA, edB);
        QVERIFY(cmpWidget->isComputing());
        delete cmpWidget; // 析构必须取消并等待在途任务
        QCoreApplication::processEvents();
        QTest::qWait(50);
        QCoreApplication::processEvents();
    }
    delete edA;
    delete edB;
}

// 进入比对模式时 setCentralWidget 会把旧的 central widget（TabManager）排队删除；
// 7 秒后的 syncBackups() 定时器再访问它就崩溃（真实环境 SIGSEGV @ QTabBar::count()）。
void TestDiffEngine::compareModeKeepsTabManagerAlive()
{
    MainWindow win;
    win.show();
    auto* tabMgr = win.findChild<TabManager*>();
    QVERIFY(tabMgr != nullptr);
    tabMgr->createNewTab();

    auto* ed1 = tabMgr->editorAt(0);
    auto* ed2 = tabMgr->editorAt(1);
    QVERIFY(ed1 != nullptr && ed2 != nullptr);
    ed1->setTextFromUtf8("hello\nworld\n");
    ed2->setTextFromUtf8("hello\nchanged\n");

    QPointer<QWidget> guard(tabMgr);
    auto* actCompare = win.findChild<QAction*>("actCompare");
    QVERIFY(actCompare != nullptr);
    actCompare->trigger();
    auto* cmp = win.findChild<CompareWidget*>();
    QVERIFY(cmp != nullptr);

    // 让 DeferredDelete / 定时器等事件真正派发
    for (int i = 0; i < 20; ++i) {
        QCoreApplication::processEvents();
        QTest::qWait(10);
    }
    QVERIFY2(!guard.isNull(), "进入比对后 TabManager 被销毁，备份定时器将访问悬空指针");
    QCOMPARE(win.centralWidget(), static_cast<QWidget*>(cmp));

    // 显式触发备份槽（真实场景由 7 秒定时器触发），必须安全返回
    QVERIFY(QMetaObject::invokeMethod(&win, "syncBackups", Qt::DirectConnection));

    auto* actClear = win.findChild<QAction*>("actClearCompare");
    QVERIFY(actClear != nullptr);
    actClear->trigger();
    QVERIFY(!guard.isNull());
    QCOMPARE(win.centralWidget(), static_cast<QWidget*>(tabMgr));
}

// 工具栏「文件对比」按钮为可切换开关：点击进入（选中态），再次点击退出比对。
void TestDiffEngine::toolbarCompareButtonTogglesCompareMode()
{
    MainWindow win;
    win.show();
    auto* btn = win.findChild<QAction*>("toolCompareToggle");
    QVERIFY(btn != nullptr);
    QVERIFY(btn->isCheckable());
    QVERIFY(!btn->isChecked());

    auto* tabMgr = win.findChild<TabManager*>();
    QVERIFY(tabMgr != nullptr);
    QPointer<QWidget> guard(tabMgr);

    // 标签页不足时进入被拒绝，按钮必须回滚为未选中。
    btn->trigger();
    QVERIFY(!btn->isChecked());
    QVERIFY(win.findChild<CompareWidget*>() == nullptr);

    tabMgr->createNewTab();
    auto* ed1 = tabMgr->editorAt(0);
    auto* ed2 = tabMgr->editorAt(1);
    QVERIFY(ed1 != nullptr && ed2 != nullptr);
    ed1->setTextFromUtf8("hello\nworld\n");
    ed2->setTextFromUtf8("hello\nchanged\n");

    btn->trigger();
    QVERIFY(btn->isChecked());
    QVERIFY(win.findChild<CompareWidget*>() != nullptr);

    btn->trigger(); // 再次点击退出
    QVERIFY(!btn->isChecked());
    QVERIFY(win.findChild<CompareWidget*>() == nullptr);
    QVERIFY2(!guard.isNull(), "退出比对不能销毁 TabManager");
    QCOMPARE(win.centralWidget(), static_cast<QWidget*>(tabMgr));

    // 菜单/快捷键进入时按钮也要变选中，Ctrl+Alt+X 退出时要取消选中。
    auto* actCompare = win.findChild<QAction*>("actCompare");
    QVERIFY(actCompare != nullptr);
    actCompare->trigger();
    QVERIFY(btn->isChecked());
    QVERIFY(win.findChild<CompareWidget*>() != nullptr);

    auto* actClear = win.findChild<QAction*>("actClearCompare");
    QVERIFY(actClear != nullptr);
    actClear->trigger();
    QVERIFY(!btn->isChecked());
    QVERIFY(win.findChild<CompareWidget*>() == nullptr);
}

// 视图菜单里的比对项收敛为一个子菜单；差异导航/关闭对比只在比对模式的工具条上出现。
void TestDiffEngine::compareMenuIsGroupedAndToolbarHasNavigationButtons()
{
    MainWindow win;
    win.show();

    auto* compareMenu = win.findChild<QMenu*>("menuCompare");
    QVERIFY(compareMenu != nullptr);
    QCOMPARE(compareMenu->actions().size(), 5);
    // 五个入口都归属子菜单，而不是平铺在「视图」里。
    for (const char* name : {"actCompare", "actCompareSelection", "actCompareGit",
                            "actCompareSvn", "actClearCompare"}) {
        auto* action = win.findChild<QAction*>(QString::fromLatin1(name));
        QVERIFY2(action != nullptr, name);
        QCOMPARE(action->parent(), static_cast<QObject*>(compareMenu));
    }
    // F8 / Shift+F8 仍作为窗口级快捷动作保留，只是不再占菜单位置。
    QVERIFY(win.findChild<QAction*>("actNextDiff") != nullptr);
    QVERIFY(win.findChild<QAction*>("actPrevDiff") != nullptr);

    // 未进入比对模式时不应存在这三个按钮。
    QVERIFY(win.findChild<QPushButton*>("btnNextDiff") == nullptr);
    QVERIFY(win.findChild<QPushButton*>("btnPrevDiff") == nullptr);
    QVERIFY(win.findChild<QPushButton*>("btnCloseCompare") == nullptr);

    auto* tabMgr = win.findChild<TabManager*>();
    QVERIFY(tabMgr != nullptr);
    tabMgr->createNewTab();
    auto* ed1 = tabMgr->editorAt(0);
    auto* ed2 = tabMgr->editorAt(1);
    QVERIFY(ed1 != nullptr && ed2 != nullptr);
    ed1->setTextFromUtf8("a\nb\nc\n");
    ed2->setTextFromUtf8("a\nB\nc\n");
    win.findChild<QAction*>("actCompare")->trigger();

    auto* cmp = win.findChild<CompareWidget*>();
    QVERIFY(cmp != nullptr);
    auto* btnNext = win.findChild<QPushButton*>("btnNextDiff");
    auto* btnPrev = win.findChild<QPushButton*>("btnPrevDiff");
    auto* btnClose = win.findChild<QPushButton*>("btnCloseCompare");
    QVERIFY(btnNext != nullptr && btnPrev != nullptr && btnClose != nullptr);
    QCoreApplication::processEvents();
    QVERIFY2(btnNext->isVisible() && btnPrev->isVisible() && btnClose->isVisible(),
             "三个按钮必须在比对模式工具条上实际可见");
    QVERIFY(btnNext->isEnabled());

    const int before = cmp->currentDiffIndex();
    btnNext->click();
    QVERIFY(cmp->currentDiffIndex() != before || cmp->totalDiffCount() <= 1);
    btnPrev->click();
    QVERIFY(cmp->currentDiffIndex() >= 0);

    btnClose->click();
    QVERIFY(win.findChild<CompareWidget*>() == nullptr);
    QVERIFY(win.findChild<QPushButton*>("btnNextDiff") == nullptr);
    QCOMPARE(win.centralWidget(), static_cast<QWidget*>(tabMgr));
}
