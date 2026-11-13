#include "test_scroll.h"
#include "EditorView.h"

#include <QApplication>
#include <QScrollBar>
#include <QtTest>

void TestScroll::scrollWidthTrackingEnabled() {
    EditorView ed;
    QCOMPARE(ed.send(SCI_GETSCROLLWIDTHTRACKING), sptr_t(1));
}

void TestScroll::longLineFullyScrollable() {
    EditorView ed;
    ed.resize(400, 200);
    ed.show();

    // 2000 个 ASCII 字符：等宽字体下远超默认 2000px 的 scrollWidth。
    const QByteArray longLine(2000, 'x');
    ed.setTextFromUtf8(longLine.constData());

    QApplication::processEvents();
    // 强制同步绘制：tracking 路径在 paint 时用 lineWidthMaxSeen 扩展 scrollWidth。
    ed.repaint();
    QApplication::processEvents();

    // 行尾像素位置；横向可滚动范围（maximum + pageStep，即最右时可见到 scrollWidth）
    // 必须能到达行尾，否则滚动条滚到最右仍会裁掉内容。
    const sptr_t endPos = ed.send(SCI_POSITIONFROMLINE, 0);
    const sptr_t endX = ed.send(SCI_POINTXFROMPOSITION, 0, endPos);
    QScrollBar* hbar = ed.horizontalScrollBar();
    const int reachable = hbar->maximum() + hbar->pageStep();

    QVERIFY2(reachable >= static_cast<int>(endX),
             qPrintable(QStringLiteral("横向可滚动范围 %1px < 行尾位置 %2px（scrollWidth=%3）")
                            .arg(reachable)
                            .arg(endX)
                            .arg(ed.send(SCI_GETSCROLLWIDTH))));
}

void TestScroll::wrapToggleKeepsTracking() {
    EditorView ed;
    QVERIFY(ed.send(SCI_GETSCROLLWIDTHTRACKING) == 1);

    ed.setWrapMode(true);
    QCOMPARE(ed.send(SCI_GETSCROLLWIDTHTRACKING), sptr_t(1));
    ed.setWrapMode(false);
    QCOMPARE(ed.send(SCI_GETSCROLLWIDTHTRACKING), sptr_t(1));
}
