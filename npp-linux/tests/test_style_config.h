#pragma once

#include <QObject>

class TestStyleConfig : public QObject {
    Q_OBJECT
private slots:
    void overrideRoundTrip();
    void legacyValueIsAccepted();
    void malformedValueIsRejected();
    void loadAllReadsPersistedOverrides();
    void styleNamesAreParsed();
    void fontOverrideReachesEditor();
    void dialogPersistsAllFields();
    void mainWindowAppliesPersistedOverrides();
};
