#pragma once

#include <QObject>

// 横向滚动回归测试：修复「横向滚动条滚到最右，右侧内容仍被裁掉」的问题。
// 根因：Scintilla 默认 scrollWidth 固定 2000px 且不跟踪实际内容宽度，
// 超长行（如 markdown 长段落）无法完整滚出。
class TestScroll : public QObject {
    Q_OBJECT
private slots:
    // 编辑器初始化必须开启 SCI_SETSCROLLWIDTHTRACKING，否则 scrollWidth 永远锁死。
    void scrollWidthTrackingEnabled();

    // 行为验证：超长行经真实绘制测量后，横向可滚动范围必须覆盖整行宽度。
    void longLineFullyScrollable();

    // 关闭自动换行（wrap off）后横向范围不得回退到固定值。
    void wrapToggleKeepsTracking();
};
