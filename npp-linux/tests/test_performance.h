#pragma once

#include <QObject>

// 五项性能红线：选词高亮、字体枚举、主题扫描、本地化反射、TabBar polish。
class TestPerformance : public QObject {
    Q_OBJECT
private slots:
    void smartHighlightIsLimitedToViewportAndFollowsScrolling();
    void fontCombosShareOneSystemEnumeration();
    void themeListIsCachedUntilInvalidated();
    void chineseUiSkipsReflectionButStillTranslatesLater();
    void tabBarStyleBatchDoesSingleRepolish();
};
