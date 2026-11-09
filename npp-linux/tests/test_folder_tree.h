#pragma once

#include <QObject>

class TestFolderTree : public QObject {
    Q_OBJECT
private slots:
    void iconProviderCachesBySuffix();
    void setRootPathAppliesRootIndexAndHidesExtraColumns();
    // 核心性能契约：深层目录在用户展开前一次都不许被读取。
    void doesNotRecursivelyPrefetchDeepDirectories();
    void activateIndexEmitsOnlyForFiles();
    void invalidRootPathIsRejectedWithoutStateChange();
    void switchingRootUpdatesRootIndex();
    void themeStyleSheetsCoverDockAndTree();
    // 防止工具栏新按钮因 qrc 漏登记而退化成空白/纯文字。
    void toolbarIconResourcesExist();

    // 单击文件夹行即展开/收起，不必去点那条很窄的箭头。
    void singleClickTogglesFolderExpansion();
    // 双击只应净切换一次，不能「展开又收起」。
    void doubleClickTogglesFolderOnlyOnce();
    // 悬停只换背景色，不能让文本/图标产生任何位移。
    void hoverDoesNotShiftItemContents();
    // Dock 标题栏的关闭按钮必须和标签页一样用 lucide x 图标。
    void dockTitleCloseButtonUsesLucideIcon();
    // 子节点缩进区必须画出连接线（默认 Fusion/QSS 分支不提供）。
    void branchAreaDrawsConnectorLines();
    // 悬停底色必须覆盖缩进区，不能只给文字那一截上色。
    void hoverPaintsFullRowIncludingIndent();
};
