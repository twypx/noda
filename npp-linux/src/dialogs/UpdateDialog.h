#pragma once

#include <QDialog>
#include <QString>

#include "utils/UpdateChecker.h"
#include "utils/UpdateInstaller.h"

class QLabel;
class QPushButton;
class QProgressBar;
class QTextBrowser;
class QCloseEvent;

// 用户触发的更新窗口：检查 → 展示发布说明 → 下载/校验/提权安装 → 提示重启。
// 不做任何后台静默行为。
class UpdateDialog : public QDialog {
    Q_OBJECT
public:
    explicit UpdateDialog(QWidget* parent = nullptr);
    // 安装进行中禁止关闭：中断 dpkg/apt 会留下半装状态。
    void reject() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void startCheck();
    void onChecking();
    void onUpToDate(const QString& currentVersion);
    void onUpdateAvailable(const UpdateInfo& info);
    void onCheckFailed(const QString& reason);
    void onInstallClicked();
    void onProgress(qint64 received, qint64 total);
    void onStage(const QString& text);
    void onInstallFailed(const QString& reason);
    void onInstallFinished(const QString& newPath);
    void onOpenReleasePage();
    void onRestart();

private:
    void buildUi();
    void setBusy(bool busy);

    UpdateChecker* _checker{nullptr};
    UpdateInstaller* _installer{nullptr};
    UpdateInfo _info;
    UpdateInstaller::Channel _channel{UpdateInstaller::Channel::Unsupported};
    QString _currentVersion;
    QString _installedPath;
    bool _installDone{false};

    QLabel* _title{nullptr};
    QLabel* _status{nullptr};
    QTextBrowser* _notes{nullptr};
    QProgressBar* _progress{nullptr};
    QPushButton* _btnInstall{nullptr};
    QPushButton* _btnRelease{nullptr};
    QPushButton* _btnRestart{nullptr};
    QPushButton* _btnClose{nullptr};
};
