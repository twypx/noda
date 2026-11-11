#pragma once

#include <QObject>
#include <QtTest>

class TestUpdate : public QObject {
    Q_OBJECT
private slots:
    void versionParsing();
    void versionComparison();
    void checksumParsing();
    void assetSelection();
    void channelDetection();
    void sha256OfDataAndFile();
};
