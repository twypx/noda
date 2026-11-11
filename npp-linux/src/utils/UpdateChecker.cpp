#include "UpdateChecker.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace {
// 更新源集中在此，改仓库只需改这一处。
constexpr const char* kGithubLatest =
    "https://api.github.com/repos/twypx/noda/releases/latest";
constexpr const char* kGiteeLatest =
    "https://gitee.com/api/v5/repos/twypx/notepad-plus-plus-linux/releases/latest";
constexpr int kRequestTimeoutMs = 15000;
} // namespace

// ---------------------------------------------------------------------------
// 纯函数
// ---------------------------------------------------------------------------
QVersionNumber NodaUpdate::parseVersion(const QString& text) {
    QString s = text.trimmed();
    while (!s.isEmpty() && (s.at(0) == QLatin1Char('v') || s.at(0) == QLatin1Char('V')))
        s.remove(0, 1);
    return QVersionNumber::fromString(s);
}

bool NodaUpdate::isNewer(const QString& current, const QString& remote) {
    const QVersionNumber cur = parseVersion(current);
    const QVersionNumber rem = parseVersion(remote);
    if (rem.isNull())
        return false;
    if (cur.isNull())
        return true;
    return QVersionNumber::compare(rem, cur) > 0;
}

QString NodaUpdate::sha256Hex(const QByteArray& data) {
    return QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha256).toHex());
}

QString NodaUpdate::sha256File(const QString& path, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        if (error)
            *error = QStringLiteral("读取文件失败");
        return {};
    }
    return QString::fromLatin1(hash.result().toHex());
}

QHash<QString, QString> NodaUpdate::parseChecksums(const QByteArray& data) {
    QHash<QString, QString> result;
    static const QRegularExpression hexRe(QStringLiteral("^[0-9a-fA-F]{64}$"));
    const QList<QByteArray> lines = data.split('\n');
    for (const QByteArray& raw : lines) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const int sep = line.indexOf(' ');
        if (sep <= 0)
            continue;
        const QString hash = QString::fromLatin1(line.left(sep)).trimmed().toLower();
        if (!hexRe.match(hash).hasMatch())
            continue; // 只接受 64 位十六进制，避免把注释/垃圾行当校验和
        QString name = QString::fromUtf8(line.mid(sep + 1)).trimmed();
        if (name.startsWith(QLatin1Char('*')))
            name.remove(0, 1); // "hash *file" 二进制模式标记
        if (name.startsWith(QStringLiteral("./")))
            name.remove(0, 2);
        if (!name.isEmpty())
            result.insert(name, hash);
    }
    return result;
}

const UpdateAsset* NodaUpdate::findAssetBySuffix(const QVector<UpdateAsset>& assets,
                                                 const QString& suffix) {
    for (const UpdateAsset& asset : assets) {
        if (asset.name.endsWith(suffix, Qt::CaseInsensitive))
            return &asset;
    }
    return nullptr;
}

const UpdateAsset* NodaUpdate::findChecksumAsset(const QVector<UpdateAsset>& assets) {
    for (const UpdateAsset& asset : assets) {
        if (asset.name.contains(QStringLiteral("SHA256SUM"), Qt::CaseInsensitive))
            return &asset;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// UpdateChecker
// ---------------------------------------------------------------------------
UpdateChecker::UpdateChecker(QObject* parent)
    : QObject(parent), _nam(new QNetworkAccessManager(this)) {
    qRegisterMetaType<UpdateInfo>("UpdateInfo");
}

void UpdateChecker::check(const QString& currentVersion) {
    if (_busy)
        return;
    _busy = true;
    _triedGitee = false;
    _current = currentVersion;
    emit checking();
    queryGitHub();
}

void UpdateChecker::queryGitHub() {
    issueRequest(QUrl(QString::fromLatin1(kGithubLatest)), QStringLiteral("GitHub"));
}

void UpdateChecker::queryGitee() {
    issueRequest(QUrl(QString::fromLatin1(kGiteeLatest)), QStringLiteral("Gitee"));
}

void UpdateChecker::issueRequest(const QUrl& url, const QString& source) {
    QNetworkRequest request(url);
    // GitHub 要求 UA；Accept 让它走 v3 JSON 稳定格式。
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Noda-Updater/1.0"));
    request.setRawHeader("Accept", "application/vnd.github+json");
    // 资产下载会 302 到 objects.githubusercontent.com，必须允许安全重定向。
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kRequestTimeoutMs);

    QNetworkReply* reply = _nam->get(request);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, source]() { handleReply(reply, source); });
}

void UpdateChecker::handleReply(QNetworkReply* reply, const QString& source) {
    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok = reply->error() == QNetworkReply::NoError && status < 400;
    reply->deleteLater();

    if (!ok) {
        // GitHub 限流(403)/无 release(404)/网络失败，都回退到 Gitee 再试一次。
        failOrFallback(source, reply->errorString());
        return;
    }
    parseAndEmit(body, source);
}

void UpdateChecker::parseAndEmit(const QByteArray& data, const QString& source) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        failOrFallback(source, QStringLiteral("发布信息解析失败"));
        return;
    }

    const QJsonObject root = doc.object();
    const QString tag = root.value(QStringLiteral("tag_name")).toString().trimmed();
    if (tag.isEmpty()) {
        failOrFallback(source, QStringLiteral("发布信息缺少版本号"));
        return;
    }

    _busy = false;
    if (!NodaUpdate::isNewer(_current, tag)) {
        emit upToDate(_current);
        return;
    }

    UpdateInfo info;
    info.tag = tag;
    info.version = tag;
    if (info.version.startsWith(QLatin1Char('v')) || info.version.startsWith(QLatin1Char('V')))
        info.version.remove(0, 1);
    info.notes = root.value(QStringLiteral("body")).toString();
    info.source = source;

    const QString htmlUrl = root.value(QStringLiteral("html_url")).toString();
    info.releaseUrl = htmlUrl.isEmpty() ? releasePageUrl(tag, source) : QUrl(htmlUrl);

    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue& value : assets) {
        const QJsonObject obj = value.toObject();
        UpdateAsset asset;
        asset.name = obj.value(QStringLiteral("name")).toString();
        asset.url = QUrl(obj.value(QStringLiteral("browser_download_url")).toString());
        asset.size = static_cast<qint64>(obj.value(QStringLiteral("size")).toDouble());
        if (!asset.name.isEmpty() && asset.url.isValid())
            info.assets.append(asset);
    }

    emit updateAvailable(info);
}

void UpdateChecker::failOrFallback(const QString& source, const QString& reason) {
    if (source == QLatin1String("GitHub") && !_triedGitee) {
        _triedGitee = true;
        queryGitee();
        return;
    }
    _busy = false;
    emit failed(reason);
}

QUrl UpdateChecker::releasePageUrl(const QString& tag, const QString& source) const {
    if (source == QLatin1String("GitHub"))
        return QUrl(QStringLiteral("https://github.com/twypx/noda/releases/tag/") + tag);
    return QUrl(QStringLiteral("https://gitee.com/twypx/notepad-plus-plus-linux/releases/tag/") + tag);
}
