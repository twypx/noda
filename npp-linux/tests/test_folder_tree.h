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
};
