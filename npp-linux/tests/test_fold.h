#pragma once

#include <QObject>

class TestFold : public QObject {
    Q_OBJECT
private slots:
    void foldAndExpandCurrentAreIdempotent();
    void foldAndUnfoldLevelAreComplementary();
    void viewMenuWiresAllUnfoldLevels();
};
