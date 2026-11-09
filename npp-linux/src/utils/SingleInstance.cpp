#include "SingleInstance.h"

#include <QDataStream>
#include <QLocalServer>
#include <QLocalSocket>
#include <QWidget>
#include <unistd.h>

SingleInstance::SingleInstance(QObject* parent) : QObject(parent) {
}

SingleInstance::~SingleInstance() {
    close();
}

QString SingleInstance::defaultServerName() {
    return QStringLiteral("notepad-plus-plus-linux-%1").arg(::getuid());
}

bool SingleInstance::trySendFilesToRunningInstance(const QStringList& filePaths,
                                                  const QString& serverName,
                                                  int timeoutMs) {
    QLocalSocket socket;
    socket.connectToServer(serverName);
    if (!socket.waitForConnected(timeoutMs)) {
        return false;
    }

    QByteArray buffer;
    QDataStream out(&buffer, QIODevice::WriteOnly);
    out << filePaths;

    socket.write(buffer);
    socket.waitForBytesWritten(timeoutMs);
    socket.disconnectFromServer();
    return true;
}

bool SingleInstance::listen(const QString& serverName) {
    if (_server && _server->isListening()) {
        if (_server->serverName() == serverName) return true;
        close();
    }

    if (!_server) {
        _server = new QLocalServer(this);
        connect(_server, &QLocalServer::newConnection, this, &SingleInstance::onNewConnection);
    }

    // 检查是否有活跃实例正在运行
    QLocalSocket probe;
    probe.connectToServer(serverName);
    if (probe.waitForConnected(100)) {
        // 已有活跃实例运行中
        return false;
    }

    // 清除可能遗留的僵尸 socket 文件
    QLocalServer::removeServer(serverName);

    return _server->listen(serverName);
}

void SingleInstance::close() {
    if (_server) {
        if (_server->isListening()) {
            _server->close();
        }
        delete _server;
        _server = nullptr;
    }
}

void SingleInstance::activateWindow(QWidget* window) {
    if (!window) return;
    if (window->isMinimized()) {
        window->showNormal();
    }
    window->show();
    window->raise();
    window->activateWindow();
}

void SingleInstance::onNewConnection() {
    while (_server && _server->hasPendingConnections()) {
        QLocalSocket* socket = _server->nextPendingConnection();
        if (!socket) continue;

        auto readData = [this, socket]() {
            if (socket->bytesAvailable() <= 0) return;
            QDataStream in(socket);
            QStringList files;
            in >> files;
            if (!files.isEmpty()) {
                emit openFilesRequested(files);
            }
        };

        connect(socket, &QLocalSocket::readyRead, this, readData);
        connect(socket, &QLocalSocket::disconnected, socket, &QLocalSocket::deleteLater);

        if (socket->bytesAvailable() > 0) {
            readData();
        }
    }
}
