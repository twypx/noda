#include "UpdateInstaller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTemporaryDir>

namespace {
constexpr int kRequestTimeoutMs = 60000;
constexpr qint64 kMaxChecksumBytes = 4 * 1024 * 1024; // 校验和文件不该超过 4MB
} // namespace

UpdateInstaller::UpdateInstaller(QObject* parent)
    : QObject(parent), _nam(new QNetworkAccessManager(this)) {}

UpdateInstaller::~UpdateInstaller() = default;

// ---------------------------------------------------------------------------
// 安装形态检测
// ---------------------------------------------------------------------------
UpdateInstaller::Channel UpdateInstaller::detectChannelFor(const QString& appImageEnv,
                                                           bool dpkgOwned) {
    if (!appImageEnv.trimmed().isEmpty())
        return Channel::AppImage;
    return dpkgOwned ? Channel::Deb : Channel::Unsupported;
}

UpdateInstaller::Channel UpdateInstaller::detectChannel() {
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    const bool dpkgOwned =
        appImage.isEmpty() && isDebOwnedByNoda(QCoreApplication::applicationFilePath());
    return detectChannelFor(appImage, dpkgOwned);
}

bool UpdateInstaller::isDebOwnedByNoda(const QString& exePath) {
    if (exePath.isEmpty() || !QFile::exists(QStringLiteral("/var/lib/dpkg/status")))
        return false;
    QProcess proc;
    proc.start(QStringLiteral("dpkg-query"), {QStringLiteral("-S"), exePath});
    if (!proc.waitForFinished(3000) || proc.exitCode() != 0)
        return false;
    const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    // dpkg-query 形如 "noda: /usr/bin/noda"；多包命中时以 "noda, xxx:" 开头。
    return out.startsWith(QStringLiteral("noda:"), Qt::CaseInsensitive) ||
           out.startsWith(QStringLiteral("noda,"), Qt::CaseInsensitive);
}

QString UpdateInstaller::appImageTargetPath() {
    const QString path = qEnvironmentVariable("APPIMAGE");
    if (path.trimmed().isEmpty())
        return {};
    return QFileInfo(path).absoluteFilePath();
}

void UpdateInstaller::restartApplication(const QString& executablePath) {
    QStringList args = QCoreApplication::arguments();
    if (!args.isEmpty())
        args.removeFirst(); // 去掉程序自身路径，只保留用户参数
    QProcess::startDetached(executablePath, args);
    QCoreApplication::quit();
}

// ---------------------------------------------------------------------------
// 主流程
// ---------------------------------------------------------------------------
void UpdateInstaller::start(const UpdateInfo& info, Channel channel) {
    if (_running)
        return;
    _info = info;
    _channel = channel;
    _running = true;

    if (channel == Channel::Unsupported) {
        fail(QStringLiteral("当前安装方式不支持自动更新，请打开下载页手动安装。"));
        return;
    }

    const UpdateAsset* checksum = NodaUpdate::findChecksumAsset(info.assets);
    if (!checksum) {
        // 没有校验和就拒绝：宁可让用户手动装，也不接受无法验证的二进制。
        fail(QStringLiteral("发布中缺少 SHA256SUMS 校验文件，出于安全考虑已中止自动更新。"));
        return;
    }

    const QString suffix =
        (channel == Channel::Deb) ? QStringLiteral(".deb") : QStringLiteral(".AppImage");
    const UpdateAsset* package = NodaUpdate::findAssetBySuffix(info.assets, suffix);
    if (!package) {
        fail(QStringLiteral("未找到适用于当前安装方式的新版本安装包。"));
        return;
    }
    _packageName = package->name;
    _packageUrl = package->url;
    _packageSize = package->size;

    QString baseDir;
    if (channel == Channel::AppImage) {
        const QString target = appImageTargetPath();
        if (target.isEmpty()) {
            fail(QStringLiteral("无法定位当前 AppImage 文件，已中止自动更新。"));
            return;
        }
        const QFileInfo targetInfo(target);
        baseDir = targetInfo.absolutePath();
        if (!QFileInfo(baseDir).isWritable()) {
            fail(QStringLiteral("AppImage 所在目录不可写（%1），无法自动更新。"
                                "请改用 deb 安装，或把 AppImage 移到可写目录。")
                     .arg(baseDir));
            return;
        }
    }

    _tmpDir = std::make_unique<QTemporaryDir>(
        (baseDir.isEmpty() ? QDir::tempPath() : baseDir) +
        QStringLiteral("/noda-update-XXXXXX"));
    if (!_tmpDir->isValid()) {
        fail(QStringLiteral("无法创建临时目录。"));
        return;
    }

    emit stage(QStringLiteral("正在获取校验和…"));
    downloadSmall(checksum->url, [this](bool ok, const QByteArray& data) {
        if (!_running)
            return;
        if (!ok) {
            fail(QStringLiteral("下载校验和失败。"));
            return;
        }
        const QHash<QString, QString> sums = NodaUpdate::parseChecksums(data);
        _expectedSha = sums.value(QFileInfo(_packageName).fileName()).toLower();
        if (_expectedSha.isEmpty()) {
            fail(QStringLiteral("校验和文件中未包含安装包条目，已中止。"));
            return;
        }

        emit stage(QStringLiteral("正在下载安装包…"));
        const QString path = _tmpDir->path() + QLatin1Char('/') +
                             QFileInfo(_packageName).fileName();
        downloadToFile(_packageUrl, path, [this](bool ok2, const QString& file) {
            if (!_running)
                return;
            if (!ok2) {
                fail(QStringLiteral("下载安装包失败，请检查网络后重试。"));
                return;
            }
            verifyAndInstall(file);
        });
    });
}

void UpdateInstaller::downloadSmall(const QUrl& url, const SmallCallback& callback) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Noda-Updater/1.0"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kRequestTimeoutMs);

    QNetworkReply* reply = _nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [reply, callback]() {
        const QByteArray data = reply->readAll();
        const bool ok = reply->error() == QNetworkReply::NoError &&
                        data.size() <= kMaxChecksumBytes;
        reply->deleteLater();
        callback(ok, data);
    });
}

void UpdateInstaller::downloadToFile(const QUrl& url, const QString& path,
                                     const FileCallback& callback) {
    auto* file = new QFile(path, this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file->deleteLater();
        callback(false, path);
        return;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Noda-Updater/1.0"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kRequestTimeoutMs);

    QNetworkReply* reply = _nam->get(request);
    connect(reply, &QNetworkReply::readyRead, this,
            [reply, file]() { file->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 received, qint64 total) { emit progress(received, total); });
    connect(reply, &QNetworkReply::finished, this,
            [reply, file, path, callback]() {
                file->write(reply->readAll()); // 收尾：把最后一块写干净
                const bool ok = reply->error() == QNetworkReply::NoError;
                file->flush();
                file->close();
                file->deleteLater();
                reply->deleteLater();
                callback(ok, path);
            });
}

void UpdateInstaller::verifyAndInstall(const QString& path) {
    emit stage(QStringLiteral("正在校验安装包完整性…"));
    QString error;
    const QString actual = NodaUpdate::sha256File(path, &error);
    if (actual.isEmpty()) {
        fail(QStringLiteral("读取安装包失败：%1").arg(error));
        return;
    }
    if (actual.compare(_expectedSha, Qt::CaseInsensitive) != 0) {
        QFile::remove(path);
        fail(QStringLiteral("校验失败：安装包已损坏或被篡改，已中止。"));
        return;
    }

    if (_channel == Channel::Deb)
        installDeb(path);
    else
        installAppImage(path);
}

void UpdateInstaller::installDeb(const QString& path) {
    // 二次确认包身份：即使校验和文件被伪造，也不允许装成别的包。
    QProcess probe;
    probe.start(QStringLiteral("dpkg-deb"),
                {QStringLiteral("-f"), path, QStringLiteral("Package")});
    if (!probe.waitForFinished(10000) || probe.exitCode() != 0) {
        fail(QStringLiteral("无法解析 deb 包信息。"));
        return;
    }
    const QString package = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
    if (package != QStringLiteral("noda")) {
        fail(QStringLiteral("安装包包名不是 noda（实际：%1），已拒绝。").arg(package));
        return;
    }

    emit stage(QStringLiteral("正在请求管理员授权并安装…"));

    // 路线 C：优先使用随包安装的受限 polkit 助手；旧版本尚无助手时退化为受控 apt。
    const QString helper = QStringLiteral("/usr/libexec/noda-install-deb");
    QStringList args;
    if (QFileInfo(helper).isExecutable()) {
        args << helper << path;
    } else {
        args << QStringLiteral("/usr/bin/env")
             << QStringLiteral("DEBIAN_FRONTEND=noninteractive")
             << QStringLiteral("/usr/bin/apt-get") << QStringLiteral("install")
             << QStringLiteral("-y")
             << QStringLiteral("-o") << QStringLiteral("Dpkg::Options::=--force-confdef")
             << QStringLiteral("-o") << QStringLiteral("Dpkg::Options::=--force-confold")
             << path;
    }

    auto* proc = new QProcess(this);
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            proc->deleteLater();
            fail(QStringLiteral("无法启动 pkexec，请确认系统已安装 polkit。"));
        }
    });
    connect(proc, &QProcess::finished, this,
            [this, proc](int code, QProcess::ExitStatus status) {
                const QString stderrText =
                    QString::fromUtf8(proc->readAllStandardError()).trimmed();
                proc->deleteLater();
                if (!_running)
                    return;
                if (status == QProcess::NormalExit && code == 0) {
                    _running = false;
                    emit finished(QString());
                } else if (code == 126) {
                    fail(QStringLiteral("已取消管理员授权，未做任何更改。"));
                } else if (code == 127) {
                    fail(QStringLiteral("未找到 pkexec/polkit，请安装 polkit 后重试。"));
                } else {
                    fail(QStringLiteral("安装失败（退出码 %1）%2")
                             .arg(code)
                             .arg(stderrText.isEmpty()
                                      ? QString()
                                      : QStringLiteral("：") + stderrText));
                }
            });
    proc->start(QStringLiteral("pkexec"), args);
}

void UpdateInstaller::installAppImage(const QString& path) {
    const QString target = appImageTargetPath();
    if (target.isEmpty()) {
        fail(QStringLiteral("无法定位当前 AppImage 文件。"));
        return;
    }
    emit stage(QStringLiteral("正在替换 AppImage…"));

    QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    const QString backup = target + QStringLiteral(".old");
    QFile::remove(backup);

    // 同一目录内 rename 才是原子操作（临时目录已按目标目录创建）。
    if (!QFile::rename(target, backup)) {
        fail(QStringLiteral("备份旧 AppImage 失败（可能无写权限）。"));
        return;
    }
    if (!QFile::rename(path, target)) {
        QFile::rename(backup, target); // 回滚
        fail(QStringLiteral("替换新 AppImage 失败，已回滚到旧版本。"));
        return;
    }
    QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                      QFile::ReadGroup | QFile::ExeGroup |
                                      QFile::ReadOther | QFile::ExeOther);
    _running = false;
    emit finished(target);
}

void UpdateInstaller::fail(const QString& reason) {
    if (!_running)
        return;
    _running = false;
    emit failed(reason);
}
