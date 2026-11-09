#pragma once

#include <QObject>

class TestShortcuts : public QObject {
    Q_OBJECT
private slots:
    void collectActionsReflectsRealActions();
    void applyOverridesUpdatesActions();
    void conflictingSequencesAreRejected();
    void resetAllRestoresDefaults();
    void dialogEditsRealActions();
    void mainWindowShortcutsComeFromActions();
};
