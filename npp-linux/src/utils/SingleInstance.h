#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QLocalServer;
class QWidget;

class SingleInstance : public QObject {
    Q_OBJECT
public:
    explicit SingleInstance(QObject* parent = nullptr);
    ~SingleInstance() override;

    // 获取当前用户专用的 socket 名称
    static QString defaultServerName();

    // 检查并发送待打开文件到已有运行实例。
    // 如果已有实例成功接收，返回 true（调用方即可直接退出）。
    // 如果无运行实例，返回 false。
    static bool trySendFilesToRunningInstance(const QStringList& filePaths,
                                              const QString& serverName = defaultServerName(),
                                              int timeoutMs = 500);

    // 开启本地服务端监听（主实例调用）
    bool listen(const QString& serverName = defaultServerName());

    // 停止本地服务端监听
    void close();

    // 激活主窗口至前台（处理最小化还原、raise、activateWindow）
    static void activateWindow(QWidget* window);

signals:
    // 收到其他进程发来的文件打开请求
    void openFilesRequested(const QStringList& filePaths);

private slots:
    void onNewConnection();

private:
    QLocalServer* _server{nullptr};
};
