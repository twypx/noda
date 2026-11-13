#include "UpdateChecker.h"
#include "utils/LocalizationManager.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>

namespace {

constexpr int kRequestTimeoutMs = 15000;

// 两个更新源的端点与解析细节集中在此，改仓库只动这两处。
NodaUpdate::UpdateSourceConfig githubConfig() {
    return {NodaUpdate::UpdateSource::GitHub,
            QStringLiteral("https://api.github.com/repos/twypx/noda/releases/latest"),
            QStringLiteral("https://api.github.com/repos/twypx/noda/releases"),
            QStringLiteral("https://github.com/twypx/noda/releases"),
            QStringLiteral("browser_download_url"),
            QStringLiteral("GitHub")};
}

NodaUpdate::UpdateSourceConfig giteeConfig() {
    return {NodaUpdate::UpdateSource::Gitee,
            {}, // Gitee 无 /latest 端点，走 releases 列表
            QStringLiteral("https://gitee.com/api/v5/repos/twypx/notepad-plus-plus-linux/releases"),
            QStringLiteral("https://gitee.com/twypx/notepad-plus-plus-linux/releases"),
            QStringLiteral("download_url"),
            QStringLiteral("Gitee")};
}

// 解析单个 release 对象；非更新/缺字段分别返回对应的结果码。
NodaUpdate::ReleaseParseResult parseRelease(const QJsonObject& root,
                                            const QString& currentVersion,
                                            const QString& assetUrlKey, UpdateInfo& out) {
    const QString tag = root.value(QStringLiteral("tag_name")).toString().trimmed();
    if (tag.isEmpty())
        return NodaUpdate::ReleaseParseResult::Invalid;
    if (!NodaUpdate::isNewer(currentVersion, tag))
        return NodaUpdate::ReleaseParseResult::NotAnUpdate;

    out.tag = tag;
    out.version = tag;
    if (out.version.startsWith(QLatin1Char('v')) || out.version.startsWith(QLatin1Char('V')))
        out.version.remove(0, 1);
    out.notes = root.value(QStringLiteral("body")).toString();
    out.source.clear();
    out.releaseUrl.clear();
    out.assets.clear();

    const QJsonArray assets = root.value(QStringLiteral("assets")).toArray();
    for (const QJsonValue& value : assets) {
        const QJsonObject obj = value.toObject();
        UpdateAsset asset;
        asset.name = obj.value(QStringLiteral("name")).toString();
        asset.url = QUrl(obj.value(assetUrlKey).toString());
        asset.size = static_cast<qint64>(obj.value(QStringLiteral("size")).toDouble());
        if (!asset.name.isEmpty() && asset.url.isValid())
            out.assets.append(asset);
    }
    return NodaUpdate::ReleaseParseResult::Ok;
}

} // namespace

namespace NodaUpdate {

UpdateSourceConfig selectUpdateSource(const QString& language) {
    // 中文界面用 GitHub；英文界面用 Gitee（需求：zh_CN→github，en_US→gitee）。
    if (language == QStringLiteral("en_US"))
        return giteeConfig();
    return githubConfig();
}

ReleaseParseResult parseUpdateResponse(const QByteArray& data, const UpdateSourceConfig& src,
                                       const QString& currentVersion, UpdateInfo& out) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return ReleaseParseResult::Invalid;

    QJsonObject root;
    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        if (arr.isEmpty())
            return ReleaseParseResult::Invalid;
        root = arr.first().toObject(); // Gitee 列表按时间倒序，第一条即最新
    } else if (doc.isObject()) {
        root = doc.object();
    } else {
        return ReleaseParseResult::Invalid;
    }

    const ReleaseParseResult r = parseRelease(root, currentVersion, src.assetUrlKey, out);
    if (r != ReleaseParseResult::Ok)
        return r;

    out.source = src.label;
    const QString htmlUrl = root.value(QStringLiteral("html_url")).toString();
    out.releaseUrl = htmlUrl.isEmpty()
                        ? QUrl(src.releasePageBase + QLatin1Char('/') + QStringLiteral("tag") +
                               QLatin1Char('/') + out.tag)
                        : QUrl(htmlUrl);
    return ReleaseParseResult::Ok;
}

} // namespace NodaUpdate

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
    // 严格大于才算更新：等于/更旧都返回 false，天然拒绝降级。
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
        // sha256sum 可能写成 "../dist/foo" / "dist/foo" / "/abs/foo"；
        // 统一取 basename，才能与 Release 资产名（纯文件名）对上。
        name = QFileInfo(name).fileName();
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
    _current = currentVersion;
    _source = NodaUpdate::selectUpdateSource(LocalizationManager::instance().language());
    emit checking();
    const QUrl url = (_source.kind == NodaUpdate::UpdateSource::GitHub)
                        ? QUrl(_source.latestUrl)
                        : QUrl(_source.releasesUrl);
    issueRequest(url);
}

void UpdateChecker::issueRequest(const QUrl& url) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Noda-Updater/1.0"));
    // 仅 GitHub 需要该 Accept 头走 v3 稳定 JSON；Gitee 用标准 JSON，不能带此头。
    if (_source.kind == NodaUpdate::UpdateSource::GitHub)
        request.setRawHeader("Accept", "application/vnd.github+json");
    // 资产下载会 302 到 objects.githubusercontent.com，必须允许安全重定向。
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(kRequestTimeoutMs);

    QNetworkReply* reply = _nam->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { handleReply(reply); });
}

void UpdateChecker::handleReply(QNetworkReply* reply) {
    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok = reply->error() == QNetworkReply::NoError && status < 400;
    const QString error = reply->errorString();
    reply->deleteLater();

    if (!ok) {
        _busy = false;
        // 限流(403)/无 release(404)/网络失败：如实上报，不做静默降级。
        emit failed(error);
        return;
    }
    parseAndEmit(body, _source);
}

void UpdateChecker::parseAndEmit(const QByteArray& data, const NodaUpdate::UpdateSourceConfig& src) {
    UpdateInfo info;
    const NodaUpdate::ReleaseParseResult r =
        NodaUpdate::parseUpdateResponse(data, src, _current, info);
    _busy = false;
    switch (r) {
    case NodaUpdate::ReleaseParseResult::Ok:
        emit updateAvailable(info);
        break;
    case NodaUpdate::ReleaseParseResult::NotAnUpdate:
        emit upToDate(_current);
        break;
    case NodaUpdate::ReleaseParseResult::Invalid:
        emit failed(QStringLiteral("发布信息解析失败"));
        break;
    }
}
