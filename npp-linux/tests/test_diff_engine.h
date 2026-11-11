#pragma once

#include <QObject>
#include <QtTest>

class TestDiffEngine : public QObject
{
    Q_OBJECT

private slots:
    void identicalTextYieldsNoDiff();
    void pureAddAndRemove();
    void modifiedLineAndIntraLineDiff();
    void ignoreWhitespaceAndCase();
    void alignmentMappingCalculatesCorrectVirtualRows();
    void multiSegmentIntraLineDiff();
    void worstCaseAllLinesDifferentIsBounded();
    void shiftedFileUsesAnchorsAndIsFast();
    void compareWidgetRendersMarkersAndAnnotations();
    void compareWidgetIgnoreOptions();
    void mainWindowCompareActions();
    // P.7：极速哈希 / 块移动
    void wyhashIsDeterministicAndDispersive();
    void movedLinesAreDetected();
    void moveDetectionCanBeDisabled();
    // P.7：异步 / 滚动映射 / 小地图
    void asyncDiffDoesNotBlockUi();
    void scrollSyncMapsAlignedDocumentLines();
    void miniMapClickNavigatesToDiff();
    void selectionCompareUsesSelectedTextOnly();
    void legacyConfigIsMigratedOnce();
    void vcsCompareActionsAreWired();
    void compareViewStableAcrossEventLoop();
    void repeatedRefreshWhileAsyncIsSafe();
    void asyncCompareCloseDuringComputationIsSafe();
    void compareModeKeepsTabManagerAlive();
    void toolbarCompareButtonTogglesCompareMode();
    void compareMenuIsGroupedAndToolbarHasNavigationButtons();
    void toolbarDropdownsAndFileCompareEntry();
    void compareWithSelectedFileUsesCurrentDocument();
    void showSymbolDropdownMatchesUpstream();
    void npcAndCcUniEolTogglesSetRepresentations();
    void dropdownMenuButtonStyled();
};
