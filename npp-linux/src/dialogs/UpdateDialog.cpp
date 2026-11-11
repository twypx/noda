#include "UpdateDialog.h"
#include "Version.h"
#include "utils/LocalizationManager.h"

#include <QCloseEvent>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {
// 统一走本地化词典：静态串与「模板 + .arg()」都可正确切换中英文。
QString L(const QString& source) {
    return LocalizationManager::instance().translate(source);
}
} // namespace

UpdateDialog::UpdateDialog(QWidget* parent) : QDialog(parent) {
    _currentVersion = QString::fromLatin1(NPP_VERSION_STRING);
    buildUi();

    _checker = new UpdateChecker(this);
    _installer = new UpdateInstaller(this);

    connect(_checker, &UpdateChecker::checking, this, &UpdateDialog::onChecking);
    connect(_checker, &UpdateChecker::upToDate, this, &UpdateDialog::onUpToDate);
    connect(_checker, &UpdateChecker::updateAvailable, this, &UpdateDialog::onUpdateAvailable);
    connect(_checker, &UpdateChecker::failed, this, &UpdateDialog::onCheckFailed);

    connect(_installer, &UpdateInstaller::stage, this, &UpdateDialog::onStage);
    connect(_installer, &UpdateInstaller::progress, this, &UpdateDialog::onProgress);
    connect(_installer, &UpdateInstaller::failed, this, &UpdateDialog::onInstallFailed);
    connect(_installer, &UpdateInstaller::finished, this, &UpdateDialog::onInstallFinished);

    // 让窗口先画出来，再发起网络请求，避免“点了没反应”的观感。
    QTimer::singleShot(0, this, &UpdateDialog::startCheck);
}

void UpdateDialog::buildUi() {
    setWindowTitle(L(QStringLiteral("检查更新")));
    setWindowIcon(QIcon(QStringLiteral(":/app/icons/app_64.png")));
    setModal(true);
    resize(620, 480);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);

    _title = new QLabel(L(QStringLiteral("检查更新")), this);
    QFont titleFont = _title->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    _title->setFont(titleFont);
    layout->addWidget(_title);

    _status = new QLabel(this);
    _status->setWordWrap(true);
    _status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(_status);

    _notes = new QTextBrowser(this);
    _notes->setOpenExternalLinks(true);
    _notes->setMinimumHeight(200);
    layout->addWidget(_notes, 1);

    _progress = new QProgressBar(this);
    _progress->setRange(0, 100);
    _progress->setValue(0);
    _progress->setVisible(false);
    layout->addWidget(_progress);

    auto* buttonRow = new QHBoxLayout();
    _btnRelease = new QPushButton(L(QStringLiteral("打开发布页")), this);
    _btnRestart = new QPushButton(L(QStringLiteral("立即重启")), this);
    _btnInstall = new QPushButton(L(QStringLiteral("立即更新")), this);
    _btnInstall->setDefault(true);
    _btnClose = new QPushButton(L(QStringLiteral("稍后")), this);

    buttonRow->addWidget(_btnRelease);
    buttonRow->addStretch(1);
    buttonRow->addWidget(_btnClose);
    buttonRow->addWidget(_btnRestart);
    buttonRow->addWidget(_btnInstall);
    layout->addLayout(buttonRow);

    connect(_btnInstall, &QPushButton::clicked, this, &UpdateDialog::onInstallClicked);
    connect(_btnRelease, &QPushButton::clicked, this, &UpdateDialog::onOpenReleasePage);
    connect(_btnRestart, &QPushButton::clicked, this, &UpdateDialog::onRestart);
    connect(_btnClose, &QPushButton::clicked, this, &QDialog::reject);

    _btnInstall->setVisible(false);
    _btnRelease->setVisible(false);
    _btnRestart->setVisible(false);
}

void UpdateDialog::reject() {
    // 安装进行中（尤其是 pkexec/apt 阶段）不允许关闭，避免中断造成半装状态。
    if (_installer && _installer->isRunning())
        return;
    QDialog::reject();
}

void UpdateDialog::closeEvent(QCloseEvent* event) {
    if (_installer && _installer->isRunning()) {
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void UpdateDialog::setBusy(bool busy) {
    _btnInstall->setEnabled(!busy);
    _btnRelease->setEnabled(!busy);
    _btnClose->setEnabled(!busy);
}

void UpdateDialog::startCheck() {
    _title->setText(L(QStringLiteral("检查更新")));
    _status->setText(L(QStringLiteral("正在检查新版本…")));
    _notes->clear();
    _progress->setVisible(false);
    _btnInstall->setVisible(false);
    _btnRelease->setVisible(false);
    _btnRestart->setVisible(false);
    _btnClose->setEnabled(true);
    _checker->check(_currentVersion);
}

void UpdateDialog::onChecking() {
    _status->setText(L(QStringLiteral("正在与发布服务器通信…")));
}

void UpdateDialog::onUpToDate(const QString& currentVersion) {
    _title->setText(L(QStringLiteral("已是最新版本")));
    _status->setText(L(QStringLiteral("当前版本 v%1 已是最新。")).arg(currentVersion));
    _notes->clear();
    _progress->setVisible(false);
    _btnInstall->setVisible(false);
    _btnRelease->setVisible(false);
    _btnClose->setEnabled(true);
}

void UpdateDialog::onUpdateAvailable(const UpdateInfo& info) {
    _info = info;
    _channel = UpdateInstaller::detectChannel();

    _title->setText(L(QStringLiteral("发现新版本 v%1")).arg(info.version));
    QString status = L(QStringLiteral("当前版本 v%1 → 最新版本 v%2（来源：%3）"))
                         .arg(_currentVersion, info.version, info.source);

    // 先判断能否自动安装：既要有对应渠道的安装包，也要有校验和（否则拒绝自动装）。
    QString suffix;
    if (_channel == UpdateInstaller::Channel::Deb)
        suffix = QStringLiteral(".deb");
    else if (_channel == UpdateInstaller::Channel::AppImage)
        suffix = QStringLiteral(".AppImage");
    const bool canInstall =
        !suffix.isEmpty() &&
        NodaUpdate::findAssetBySuffix(info.assets, suffix) != nullptr &&
        NodaUpdate::findChecksumAsset(info.assets) != nullptr;

    if (_channel == UpdateInstaller::Channel::Unsupported) {
        status += QLatin1Char('\n') +
                  L(QStringLiteral("当前安装方式不支持自动安装，请使用「打开发布页」手动下载。"));
    } else if (!canInstall) {
        status += QLatin1Char('\n') +
                  L(QStringLiteral("未找到可用于自动安装的安装包，请使用「打开发布页」手动下载。"));
    }
    _status->setText(status);

    const QString notes = info.notes.trimmed();
    _notes->setMarkdown(notes.isEmpty() ? L(QStringLiteral("（无发布说明）")) : notes);

    _btnInstall->setVisible(canInstall);
    _btnInstall->setEnabled(canInstall);
    _btnRelease->setVisible(true);
    _btnRestart->setVisible(false);
    _progress->setVisible(false);
    _btnClose->setEnabled(true);
}

void UpdateDialog::onCheckFailed(const QString& reason) {
    _title->setText(L(QStringLiteral("检查更新失败")));
    _status->setText(L(reason));
    // 检查失败时仍给一个可用的发布页入口，避免用户空手而归。
    _info.releaseUrl = QUrl(QStringLiteral("https://github.com/twypx/noda/releases"));
    _btnInstall->setVisible(false);
    _btnRelease->setVisible(true);
    _btnRestart->setVisible(false);
    _progress->setVisible(false);
    _btnClose->setEnabled(true);
}

void UpdateDialog::onInstallClicked() {
    if (_installer->isRunning())
        return;
    _installDone = false;
    _installedPath.clear();

    setBusy(true);
    _btnInstall->setVisible(true);
    _progress->setVisible(true);
    _progress->setRange(0, 0); // 先走不确定态，拿到总大小后再切百分比
    _status->setText(L(QStringLiteral("准备下载新版本…")));
    _installer->start(_info, _channel);
}

void UpdateDialog::onProgress(qint64 received, qint64 total) {
    if (total > 0) {
        _progress->setRange(0, 100);
        _progress->setValue(static_cast<int>(received * 100 / total));
    } else {
        _progress->setRange(0, 0);
    }
}

void UpdateDialog::onStage(const QString& text) {
    _status->setText(L(text));
    // 校验/授权/安装阶段没有字节进度，切回不确定态。
    if (text.contains(QStringLiteral("校验")) || text.contains(QStringLiteral("授权")) ||
        text.contains(QStringLiteral("安装")) || text.contains(QStringLiteral("替换"))) {
        _progress->setRange(0, 0);
    }
}

void UpdateDialog::onInstallFailed(const QString& reason) {
    _title->setText(L(QStringLiteral("更新失败")));
    _status->setText(L(reason));
    _progress->setVisible(false);
    _btnInstall->setVisible(true);
    _btnInstall->setEnabled(true);
    _btnRelease->setEnabled(true);
    _btnClose->setEnabled(true);
}

void UpdateDialog::onInstallFinished(const QString& newPath) {
    _installDone = true;
    _installedPath = newPath;
    _title->setText(L(QStringLiteral("更新完成")));
    _status->setText(newPath.isEmpty()
                         ? L(QStringLiteral("新版本已安装，请重启应用后生效。"))
                         : L(QStringLiteral("新版本已下载并替换，请重启应用后生效。")));
    _progress->setVisible(false);
    _btnInstall->setVisible(false);
    _btnRelease->setVisible(false);
    _btnRestart->setVisible(true);
    _btnClose->setEnabled(true);
    _btnRestart->setDefault(true);
}

void UpdateDialog::onOpenReleasePage() {
    const QUrl url = _info.releaseUrl.isValid()
                         ? _info.releaseUrl
                         : QUrl(QStringLiteral("https://github.com/twypx/noda/releases"));
    QDesktopServices::openUrl(url);
}

void UpdateDialog::onRestart() {
    QString executable;
    if (_channel == UpdateInstaller::Channel::AppImage && !_installedPath.isEmpty())
        executable = _installedPath;
    else
        executable = QCoreApplication::applicationFilePath();
    UpdateInstaller::restartApplication(executable);
}
