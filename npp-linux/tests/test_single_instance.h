#pragma once

#include <QObject>

class TestSingleInstance : public QObject {
    Q_OBJECT
private slots:
    void serverListensAndRecoversStaleSocket();
    void clientSendsFileArgumentsToServer();
    void mainWindowOpensTabOnIpcMessage();
};
