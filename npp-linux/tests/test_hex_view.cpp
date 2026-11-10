#include "test_hex_view.h"
#include <QTest>
#include <QAction>
#include <QKeyEvent>
#include <QTemporaryFile>
#include <QApplication>
#include <QCoreApplication>
#include <QFontMetrics>
#include <QPaintEvent>
#include <QScrollBar>
#include "utils/ThemeManager.h"
#include "widgets/HexView.h"
#include "MainWindow.h"
#include "TabManager.h"
#include "EditorView.h"

void TestHexView::hexFormattingLookupCorrectness() {
    HexView hexView;
    const QByteArray testData = QByteArray::fromHex("00010a20417e7fff");
    hexView.setData(testData);

    QCOMPARE(hexView.dataSize(), static_cast<qint64>(8));
    QCOMPARE(hexView.rowCount(), 1);

    // 格式化验证
    QCOMPARE(HexView::formatByteHex(0x00), QString("00"));
    QCOMPARE(HexView::formatByteHex(0x41), QString("41"));
    QCOMPARE(HexView::formatByteHex(0x7F), QString("7F"));
    QCOMPARE(HexView::formatByteHex(0xFF), QString("FF"));

    // ASCII 字符映射验证（不可打印字符与空白控制字符映射为 '.'）
    QCOMPARE(HexView::byteToAscii(0x00), '.');
    QCOMPARE(HexView::byteToAscii(0x0A), '.');
    QCOMPARE(HexView::byteToAscii(0x20), ' ');
    QCOMPARE(HexView::byteToAscii(0x41), 'A');
    QCOMPARE(HexView::byteToAscii(0x7E), '~');
    QCOMPARE(HexView::byteToAscii(0x7F), '.');
    QCOMPARE(HexView::byteToAscii(0xFF), '.');

    // 地址格式化验证（8 位大写十六进制）
    QCOMPARE(HexView::formatAddress(0), QString("00000000"));
    QCOMPARE(HexView::formatAddress(16), QString("00000010"));
    QCOMPARE(HexView::formatAddress(0x12345), QString("00012345"));
}

void TestHexView::viewportOnlyRenderingCalculatesRows() {
    HexView hexView;
    // 模拟 16MB 的大文件数据（16,777,216 字节）
    const int size = 16 * 1024 * 1024;
    QByteArray bigData;
    bigData.resize(size);
    hexView.setData(bigData);

    // 16 字节一行
    QCOMPARE(hexView.rowCount(), size / 16);

    // 模拟视口滚动：可视高度容纳 30 行
    hexView.resize(800, 600);
    int startRow = 0;
    int endRow = 0;
    hexView.calculateVisibleRange(0, 30, startRow, endRow);

    QCOMPARE(startRow, 0);
    QVERIFY(endRow >= 25 && endRow <= 35);

    // 模拟向下滚动 1000 行
    hexView.calculateVisibleRange(1000, 30, startRow, endRow);
    QCOMPARE(startRow, 1000);
    QVERIFY(endRow >= 1025 && endRow <= 1035);
}

void TestHexView::selectionAndOffsetBidirectionalSync() {
    HexView hexView;
    QByteArray data("Hello Binary World, Testing Hex View Selection!");
    hexView.setData(data);

    // 设置初始光标为 6 ('B')
    hexView.setCursorPosition(6);
    QCOMPARE(hexView.cursorPosition(), static_cast<qint64>(6));
    QCOMPARE(hexView.selectedByte(), static_cast<unsigned char>('B'));

    // 模拟右键或下键键盘移动
    QKeyEvent keyRight(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    hexView.handleKeyEvent(&keyRight);
    QCOMPARE(hexView.cursorPosition(), static_cast<qint64>(7));

    QKeyEvent keyDown(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    hexView.handleKeyEvent(&keyDown);
    QCOMPARE(hexView.cursorPosition(), static_cast<qint64>(7 + 16));

    // 越界保护
    hexView.setCursorPosition(hexView.dataSize() + 100);
    QCOMPARE(hexView.cursorPosition(), hexView.dataSize() - 1);
}

void TestHexView::mainWindowTogglesHexViewSmoothly() {
    MainWindow win;
    win.show();
    auto* tabMgr = win.findChild<TabManager*>();
    QVERIFY(tabMgr != nullptr);

    auto* editor = tabMgr->currentEditor();
    QVERIFY(editor != nullptr);

    // 检查视图菜单中的十六进制切换 Action
    auto* actHex = win.findChild<QAction*>("actViewHex");
    QVERIFY(actHex != nullptr);
    QCOMPARE(actHex->shortcut().toString(), QString("Ctrl+Alt+H"));
    QCOMPARE(actHex->isChecked(), false);

    // 触发切换到 Hex 视图
    actHex->trigger();
    QCOMPARE(actHex->isChecked(), true);
    auto* hexView = tabMgr->currentHexView();
    QVERIFY(hexView != nullptr);
    QCOMPARE(hexView->isHidden(), false);

    // 再次触发切换回普通文本视图
    actHex->trigger();
    QCOMPARE(actHex->isChecked(), false);
    QCOMPARE(hexView->isHidden(), true);
}

// 模块 M：已保存文件必须展示磁盘原始字节（含 0x00/0xFF/0x80 等非法 UTF-8 字节），
// 而不是 Scintilla 解码后的 UTF-8 缓冲。
void TestHexView::rawBytesMatchDisk() {
    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    const QByteArray raw = QByteArray::fromHex("00ff80102a00fe7f");
    QCOMPARE(tmp.write(raw), static_cast<qint64>(raw.size()));
    QVERIFY(tmp.flush());

    HexView hv;
    QVERIFY(hv.setFile(tmp.fileName()));
    QCOMPARE(hv.dataSize(), static_cast<qint64>(raw.size()));
    QCOMPARE(hv.bytesInRange(0, raw.size()), raw);
}

// 稀疏大文件中段读取：验证采用 qint64 偏移且非整份载入（否则内存与耗时不可接受）。
void TestHexView::rangeReadAtHighOffset() {
    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    const qint64 fileSize = 8LL * 1024 * 1024;
    QVERIFY(tmp.resize(fileSize));
    const qint64 off = 4LL * 1024 * 1024 + 123;
    QVERIFY(tmp.seek(off));
    QCOMPARE(tmp.write("MAGIC", 5), static_cast<qint64>(5));
    QVERIFY(tmp.flush());

    HexView hv;
    QVERIFY(hv.setFile(tmp.fileName()));
    QCOMPARE(hv.dataSize(), fileSize);
    QCOMPARE(hv.bytesInRange(off, 5), QByteArray("MAGIC"));
    QCOMPARE(hv.bytesInRange(off - 3, 3), QByteArray(3, '\0'));
    // 越界读取自动夹取
    QCOMPARE(hv.bytesInRange(fileSize - 2, 100), QByteArray(2, '\0'));
}

// 未保存文档回退到内存缓冲，且 COW 不深拷。
void TestHexView::unsavedDocumentUsesMemory() {
    HexView hv;
    const QByteArray data = QByteArray::fromHex("deadbeef00ff");
    hv.setData(data);
    QCOMPARE(hv.dataSize(), static_cast<qint64>(data.size()));
    QCOMPARE(hv.bytesInRange(0, data.size()), data);
    hv.setCursorPosition(3);
    QCOMPARE(hv.selectedByte(), static_cast<unsigned char>(0xEF));
}

// 大文件地址宽度自适应：默认 8 位，>4GB 用 16 位。
void TestHexView::addressWidthAdapts() {
    QCOMPARE(HexView::formatAddress(0, 8), QString("00000000"));
    QCOMPARE(HexView::formatAddress(0x12345, 8), QString("00012345"));
    QCOMPARE(HexView::formatAddress(0x100000000LL, 16), QString("0000000100000000"));
}

// 含 NUL 字节的二进制文件打开后应自动切到十六进制视图，且显示的是磁盘原始字节。
void TestHexView::binaryFileOpensInHexView() {
    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    const QByteArray raw = QByteArray::fromHex("000102030004"); // 含 NUL，非 UTF-16 BOM
    QCOMPARE(tmp.write(raw), static_cast<qint64>(raw.size()));
    QVERIFY(tmp.flush());

    MainWindow win;
    win.show();
    auto* tabMgr = win.findChild<TabManager*>();
    QVERIFY(tabMgr != nullptr);

    win.openFile(tmp.fileName());

    auto* actHex = win.findChild<QAction*>("actViewHex");
    QVERIFY(actHex != nullptr);
    QCOMPARE(actHex->isChecked(), true);
    QVERIFY(tabMgr->isHexViewActive());

    auto* hv = tabMgr->currentHexView();
    QVERIFY(hv != nullptr);
    QCOMPARE(hv->dataSize(), static_cast<qint64>(raw.size()));
    QCOMPARE(hv->bytesInRange(0, raw.size()), raw);
}

namespace {

// 记录重绘次数与重绘时的起始行，用于验证滚动条拖动确实会刷新视口。
class CountingHexView : public HexView {
public:
    int paints{0};
    int lastStartRow{-1};

protected:
    void paintEvent(QPaintEvent* event) override {
        ++paints;
        lastStartRow = static_cast<int>(verticalScrollBar()->value());
        HexView::paintEvent(event);
    }
};

} // namespace

void TestHexView::scrollbarDragRepaintsViewport() {
    CountingHexView hv;
    hv.resize(480, 320);
    hv.setData(QByteArray(16 * 2000, 'A'));
    hv.show();
    QCoreApplication::processEvents();

    const int before = hv.paints;
    // 模拟拖动/点击纵向滚动条：只改变 value，不经过滚轮事件。
    hv.verticalScrollBar()->setValue(150);
    QCoreApplication::processEvents();

    QVERIFY2(hv.paints > before, "滚动条值变化必须触发视口重绘");
    QCOMPARE(hv.lastStartRow, 150);

    hv.hide();
}

// 主题色板随深/浅切换，且与 ThemeManager 提供的值一致。
void TestHexView::themeColorsFollowDarkMode() {
    HexView hv;
    hv.setDarkMode(true);
    const HexViewColors dark = ThemeManager::hexViewColors(true);
    QCOMPARE(hv.themeColors().background, dark.background);
    QCOMPARE(hv.themeColors().text, dark.text);
    QCOMPARE(hv.themeColors().address, dark.address);

    hv.setDarkMode(false);
    const HexViewColors light = ThemeManager::hexViewColors(false);
    QCOMPARE(hv.themeColors().background, light.background);
    QCOMPARE(hv.themeColors().address, light.address);
}

// 全局 QSS 的 QWidget{font-family} 会把比例界面字体套到 HexView 上，
// 导致按「数字宽度」计算的列栅格与实际绘制（空格更窄）逐渐错位——即光标/点击命中不准。
// 修复后 HexView 必须始终使用等宽字体：'0' 与空格的推进宽度相等。
void TestHexView::monospaceFontSurvivesGlobalStyleSheet() {
    const QFont originalAppFont = QApplication::font();

    QFont uiFont(QStringLiteral("DejaVu Sans")); // 典型比例字体
    uiFont.setPointSize(10);
    ThemeManager::setUiFont(uiFont, QStringList{QStringLiteral("DejaVu Sans")});

    HexView hv;
    hv.resize(400, 200);
    hv.show();
    QCoreApplication::processEvents();

    const QFontMetrics fm(hv.font());
    QCOMPARE(fm.horizontalAdvance(QLatin1Char('0')), fm.horizontalAdvance(QLatin1Char(' ')));
    QCOMPARE(fm.horizontalAdvance(QLatin1Char('0')), fm.horizontalAdvance(QLatin1Char('W')));

    // 复原全局字体/样式，避免影响后续测试。
    ThemeManager::setUiFont(originalAppFont, QStringList());
}
