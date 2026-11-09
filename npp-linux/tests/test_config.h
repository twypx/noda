#pragma once

#include <QObject>

class TestConfig : public QObject {
    Q_OBJECT
private slots:
    void validConfigsReportCounts();
    void missingStylersIsReported();
    void emptyStructureIsReported();
    void invalidColorKeepsDefaults();
    void repeatedLoadReplacesPreviousState();
    void locateConfigFilesFindsKnownLayouts();
    void buildDiagnosticMentionsCauseAndConsequence();
};
