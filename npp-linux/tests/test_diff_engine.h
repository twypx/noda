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
};
