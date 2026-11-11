#pragma once

#include <QObject>
#include <QUrl>
#include <QVector>
#include <QHash>
#include <QByteArray>
#include <QVersionNumber>

class QNetworkAccessManager;
class QNetworkReply;

// 一个发布资产（安装包 / 校验和 / 其它附件）。
struct UpdateAsset {
    QString name;
    QUrl url;
    qint64 size{0};
};

// 一次“发现新版本”的完整信息，交给下载/安装器使用。
struct UpdateInfo {
    QString tag;        // 原始 tag，形如 v1.0.24
    QString version;    // 去掉前缀的版本，形如 1.0.24
    QString notes;      // 发布说明（Markdown / 纯文本）
    QUrl releaseUrl;    // 发布页
    QString source;     // 固定为 "GitHub"
    QVector<UpdateAsset> assets;
};

Q_DECLARE_METATYPE(UpdateAsset)
Q_DECLARE_METATYPE(UpdateInfo)

// 与网络无关的纯函数：便于单元测试，也避免把业务逻辑塞进 QObject。
namespace NodaUpdate {

// 解析版本号：容忍前导 v/V 与后缀（1.2.3-beta 取 1.2.3）。非法输入返回 null。
QVersionNumber parseVersion(const QString& text);

// remote 是否比 current 更新；任一非法时保守返回 false（绝不把非法版本判成更新）。
// 所有“是否可升级”的判断都走这里，因此天然拒绝降级与同版本。
bool isNewer(const QString& current, const QString& remote);

// sha256sum 文本 -> {文件名: 小写十六进制哈希}。
// 兼容 "hash  name" / "hash *name" / "./name" 等格式，非法行忽略。
QHash<QString, QString> parseChecksums(const QByteArray& data);

QString sha256Hex(const QByteArray& data);
// 流式读取文件计算 SHA256，避免把几十 MB 的包整个读进内存。
QString sha256File(const QString& path, QString* error = nullptr);

// 按扩展名找资产（大小写不敏感，如 ".deb" / ".AppImage"）。
const UpdateAsset* findAssetBySuffix(const QVector<UpdateAsset>& assets, const QString& suffix);

// 找校验和资产：名字里含 SHA256SUM 即可（兼容 SHA256SUMS / SHA256SUMS.txt）。
const UpdateAsset* findChecksumAsset(const QVector<UpdateAsset>& assets);

} // namespace NodaUpdate

// 异步查询 GitHub 最新 release，并与当前版本比较。
// 全程用户触发，不做任何后台静默行为；更新源固定为 GitHub。
class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(QObject* parent = nullptr);

    // 开始检查；若正在检查则忽略重复调用。
    void check(const QString& currentVersion);
    bool isBusy() const { return _busy; }

signals:
    void checking();
    void upToDate(const QString& currentVersion);
    void updateAvailable(const UpdateInfo& info);
    void failed(const QString& reason);

private:
    void issueRequest(const QUrl& url);
    void handleReply(QNetworkReply* reply);
    void parseAndEmit(const QByteArray& data);
    QUrl releasePageUrl(const QString& tag) const;

    QNetworkAccessManager* _nam{nullptr};
    bool _busy{false};
    QString _current;
};
