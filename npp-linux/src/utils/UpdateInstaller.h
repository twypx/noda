#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <functional>
#include <memory>

#include "UpdateChecker.h"

class QNetworkAccessManager;
class QTemporaryDir;

// 负责“下载 → 校验 → 提权安装”的完整流程。
// 所有动作都由用户点击触发；不监听、不轮询、不静默执行。
class UpdateInstaller : public QObject {
    Q_OBJECT
public:
    enum class Channel {
        AppImage,    // 单文件，可原地替换后重启
        Deb,         // 系统包，必须经 polkit 提权 + apt/dpkg
        Unsupported  // 源码构建/未知安装方式：只提示手动下载
    };
    Q_ENUM(Channel)

    explicit UpdateInstaller(QObject* parent = nullptr);
    ~UpdateInstaller() override;

    void start(const UpdateInfo& info, Channel channel);
    bool isRunning() const { return _running; }

    // 运行期检测当前安装形态。
    static Channel detectChannel();
    // 纯函数版本，便于测试。
    static Channel detectChannelFor(const QString& appImageEnv, bool dpkgOwned);
    // 当前进程是否由 deb 包管理（/usr/bin/noda 属于 noda 包）。
    static bool isDebOwnedByNoda(const QString& exePath);
    // 真实 AppImage 路径（$APPIMAGE），非 AppImage 运行时为空。
    static QString appImageTargetPath();
    // 用新二进制替换当前进程并退出。
    static void restartApplication(const QString& executablePath);

signals:
    void stage(const QString& text);
    void progress(qint64 received, qint64 total);
    void failed(const QString& reason);
    void finished(const QString& newPath);

private:
    using SmallCallback = std::function<void(bool ok, const QByteArray& data)>;
    using FileCallback = std::function<void(bool ok, const QString& path)>;

    void downloadSmall(const QUrl& url, const SmallCallback& callback);
    void downloadToFile(const QUrl& url, const QString& path, const FileCallback& callback);
    void verifyAndInstall(const QString& path);
    void installDeb(const QString& path);
    void installAppImage(const QString& path);
    void fail(const QString& reason);

    QNetworkAccessManager* _nam{nullptr};
    std::unique_ptr<QTemporaryDir> _tmpDir;
    UpdateInfo _info;
    Channel _channel{Channel::Unsupported};
    QString _packageName;
    QUrl _packageUrl;
    qint64 _packageSize{0};
    QString _expectedSha;
    bool _running{false};
};
