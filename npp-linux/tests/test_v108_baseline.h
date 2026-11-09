#pragma once

#include <QObject>

// v1.0.7 / v1.0.8 已提交的能力基线：后续任何改造都不得让这些测试变红。
class TestV108Baseline : public QObject {
    Q_OBJECT
private slots:
    void astyleAndJsonFormattingStillWork();
    void mainWindowKeepsFormatActionAndDragDrop();
};
