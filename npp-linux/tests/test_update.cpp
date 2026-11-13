#include "test_update.h"

#include "utils/UpdateChecker.h"
#include "utils/UpdateInstaller.h"

#include <QTemporaryFile>

void TestUpdate::versionParsing() {
    QVERIFY(NodaUpdate::parseVersion(QStringLiteral("v1.0.24")) == QVersionNumber(1, 0, 24));
    QVERIFY(NodaUpdate::parseVersion(QStringLiteral("1.0.24")) == QVersionNumber(1, 0, 24));
    QVERIFY(NodaUpdate::parseVersion(QStringLiteral("V2.3.4-beta")) == QVersionNumber(2, 3, 4));
    QVERIFY(NodaUpdate::parseVersion(QString()).isNull());
    QVERIFY(NodaUpdate::parseVersion(QStringLiteral("abc")).isNull());
}

void TestUpdate::versionComparison() {
    QVERIFY(NodaUpdate::isNewer(QStringLiteral("1.0.23"), QStringLiteral("v1.0.24")));
    QVERIFY(!NodaUpdate::isNewer(QStringLiteral("v1.0.24"), QStringLiteral("1.0.24")));
    QVERIFY(!NodaUpdate::isNewer(QStringLiteral("1.0.24"), QStringLiteral("1.0.23")));
    QVERIFY(NodaUpdate::isNewer(QStringLiteral("1.0.24"), QStringLiteral("1.0.24.1")));
    // 非法远端版本绝不能判成“有更新”。
    QVERIFY(!NodaUpdate::isNewer(QStringLiteral("1.0.24"), QStringLiteral("garbage")));
    QVERIFY(!NodaUpdate::isNewer(QString(), QStringLiteral("garbage")));
}

void TestUpdate::checksumParsing() {
    const QString hashA(64, QLatin1Char('a'));
    const QString hashB(64, QLatin1Char('B'));
    const QString hashC(64, QLatin1Char('c'));
    const QByteArray data =
        // 真实场景：workflow 里 `sha256sum ../dist/*.deb` 会写成带路径的名字。
        (hashA + QStringLiteral("  ../dist/noda_1.0.24-1_amd64.deb\n") + hashB +
         QStringLiteral(" *./Noda-1.0.24-x86_64.AppImage\n") + hashC +
         QStringLiteral("  /tmp/build/dist/noda_extra.deb\n") +
         QStringLiteral("# 注释行应被忽略\n") + QStringLiteral("abc  garbage.txt\n"))
            .toUtf8();

    const QHash<QString, QString> sums = NodaUpdate::parseChecksums(data);
    // 带 "../dist/" 前缀的条目也必须归一化为纯文件名后可按资产名查到。
    QCOMPARE(sums.value(QStringLiteral("noda_1.0.24-1_amd64.deb")), hashA);
    // 去掉 "*" 与 "./" 前缀，且哈希统一小写。
    QCOMPARE(sums.value(QStringLiteral("Noda-1.0.24-x86_64.AppImage")), hashB.toLower());
    // 绝对路径同样归一化为 basename。
    QCOMPARE(sums.value(QStringLiteral("noda_extra.deb")), hashC);
    // 非 64 位十六进制的行必须忽略。
    QVERIFY(!sums.contains(QStringLiteral("garbage.txt")));
}

void TestUpdate::assetSelection() {
    QVector<UpdateAsset> assets;
    assets.append(UpdateAsset{QStringLiteral("Noda-1.0.24-x86_64.AppImage"),
                              QUrl(QStringLiteral("https://example.com/a")), 100});
    assets.append(UpdateAsset{QStringLiteral("noda_1.0.24-1_amd64.deb"),
                              QUrl(QStringLiteral("https://example.com/d")), 50});
    assets.append(UpdateAsset{QStringLiteral("SHA256SUMS.txt"),
                              QUrl(QStringLiteral("https://example.com/s")), 1});

    const UpdateAsset* deb = NodaUpdate::findAssetBySuffix(assets, QStringLiteral(".deb"));
    QVERIFY(deb != nullptr);
    QCOMPARE(deb->name, QStringLiteral("noda_1.0.24-1_amd64.deb"));

    const UpdateAsset* appImage =
        NodaUpdate::findAssetBySuffix(assets, QStringLiteral(".AppImage"));
    QVERIFY(appImage != nullptr);
    QVERIFY(appImage->name.endsWith(QStringLiteral(".AppImage")));

    QVERIFY(NodaUpdate::findChecksumAsset(assets) != nullptr);
    QVERIFY(NodaUpdate::findAssetBySuffix(assets, QStringLiteral(".rpm")) == nullptr);
    QVERIFY(NodaUpdate::findChecksumAsset(QVector<UpdateAsset>{}) == nullptr);
}

void TestUpdate::channelDetection() {
    using Channel = UpdateInstaller::Channel;
    QVERIFY(UpdateInstaller::detectChannelFor(QString(), false) == Channel::Unsupported);
    QVERIFY(UpdateInstaller::detectChannelFor(QString(), true) == Channel::Deb);
    QVERIFY(UpdateInstaller::detectChannelFor(QStringLiteral("/home/u/Noda.AppImage"), false) ==
            Channel::AppImage);
    // AppImage 环境变量存在时优先于 deb 判定。
    QVERIFY(UpdateInstaller::detectChannelFor(QStringLiteral("/x/Noda.AppImage"), true) ==
            Channel::AppImage);

    qputenv("APPIMAGE", "/tmp/Noda-1.0.24-x86_64.AppImage");
    QCOMPARE(UpdateInstaller::appImageTargetPath(),
             QStringLiteral("/tmp/Noda-1.0.24-x86_64.AppImage"));
    qunsetenv("APPIMAGE");
    QVERIFY(UpdateInstaller::appImageTargetPath().isEmpty());
}

void TestUpdate::sha256OfDataAndFile() {
    const QString expected =
        QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    QCOMPARE(NodaUpdate::sha256Hex(QByteArray("abc")), expected);

    QTemporaryFile tmp;
    QVERIFY(tmp.open());
    QCOMPARE(tmp.write("abc"), static_cast<qint64>(3));
    QVERIFY(tmp.flush());

    QString error;
    QCOMPARE(NodaUpdate::sha256File(tmp.fileName(), &error), expected);
    QVERIFY(error.isEmpty());

    QVERIFY(NodaUpdate::sha256File(QStringLiteral("/nonexistent/noda.deb"), &error).isEmpty());
    QVERIFY(!error.isEmpty());
}

void TestUpdate::sourceSelection() {
    // 中文界面 → GitHub
    const auto zh = NodaUpdate::selectUpdateSource(QStringLiteral("zh_CN"));
    QCOMPARE(zh.kind, NodaUpdate::UpdateSource::GitHub);
    QCOMPARE(zh.label, QStringLiteral("GitHub"));
    QCOMPARE(zh.assetUrlKey, QStringLiteral("browser_download_url"));
    QVERIFY(!zh.latestUrl.isEmpty());

    // 英文界面 → Gitee（需求：en_US→gitee）
    const auto en = NodaUpdate::selectUpdateSource(QStringLiteral("en_US"));
    QCOMPARE(en.kind, NodaUpdate::UpdateSource::Gitee);
    QCOMPARE(en.label, QStringLiteral("Gitee"));
    QCOMPARE(en.assetUrlKey, QStringLiteral("download_url"));
    QVERIFY(en.latestUrl.isEmpty());
    QVERIFY(!en.releasesUrl.isEmpty());

    // 其它（含默认空串）→ GitHub
    QCOMPARE(NodaUpdate::selectUpdateSource(QString()).kind,
             NodaUpdate::UpdateSource::GitHub);
    QCOMPARE(NodaUpdate::selectUpdateSource(QStringLiteral("zh_TW")).kind,
             NodaUpdate::UpdateSource::GitHub);
}

void TestUpdate::giteeResponseParsing() {
    // 构造一条 Gitee 风格的发布：数组包裹，资产下载链接字段为 download_url。
    QJsonObject asset;
    asset[QStringLiteral("name")] = QStringLiteral("noda_1.0.99-1_amd64.deb");
    asset[QStringLiteral("download_url")] = QStringLiteral("https://gitee.com/x/noda.deb");
    asset[QStringLiteral("size")] = 12345;

    QJsonObject release;
    release[QStringLiteral("tag_name")] = QStringLiteral("v1.0.99");
    release[QStringLiteral("body")] = QStringLiteral("release notes");
    release[QStringLiteral("html_url")] =
        QStringLiteral("https://gitee.com/twypx/notepad-plus-plus-linux/releases/tag/v1.0.99");
    release[QStringLiteral("assets")] = QJsonArray{asset};

    const QJsonDocument doc(QJsonArray{release});
    const auto src = NodaUpdate::selectUpdateSource(QStringLiteral("en_US"));

    UpdateInfo info;
    const auto r =
        NodaUpdate::parseUpdateResponse(doc.toJson(), src, QStringLiteral("1.0.0"), info);
    QCOMPARE(r, NodaUpdate::ReleaseParseResult::Ok);
    QCOMPARE(info.source, QStringLiteral("Gitee"));
    QCOMPARE(info.version, QStringLiteral("1.0.99"));
    QCOMPARE(info.assets.size(), 1);
    QCOMPARE(info.assets.first().name, QStringLiteral("noda_1.0.99-1_amd64.deb"));
    QCOMPARE(info.assets.first().url.toString(), QStringLiteral("https://gitee.com/x/noda.deb"));
    QCOMPARE(info.assets.first().size, qint64(12345));

    // 非更新（远端更旧/相同）应判为 NotAnUpdate，而非误报更新。
    UpdateInfo stale;
    const auto r2 =
        NodaUpdate::parseUpdateResponse(doc.toJson(), src, QStringLiteral("1.1.0"), stale);
    QCOMPARE(r2, NodaUpdate::ReleaseParseResult::NotAnUpdate);
}

void TestUpdate::githubResponseParsingUnchanged() {
    // GitHub 风格：单对象，资产用 browser_download_url。
    QJsonObject asset;
    asset[QStringLiteral("name")] = QStringLiteral("noda_1.0.99-1_amd64.deb");
    asset[QStringLiteral("browser_download_url")] = QStringLiteral("https://github.com/x/noda.deb");
    asset[QStringLiteral("size")] = 999;

    QJsonObject release;
    release[QStringLiteral("tag_name")] = QStringLiteral("v1.0.99");
    release[QStringLiteral("body")] = QStringLiteral("notes");
    release[QStringLiteral("html_url")] = QStringLiteral("https://github.com/twypx/noda/releases/tag/v1.0.99");
    release[QStringLiteral("assets")] = QJsonArray{asset};

    const QJsonDocument doc(release);
    const auto src = NodaUpdate::selectUpdateSource(QStringLiteral("zh_CN"));

    UpdateInfo info;
    const auto r =
        NodaUpdate::parseUpdateResponse(doc.toJson(), src, QStringLiteral("1.0.0"), info);
    QCOMPARE(r, NodaUpdate::ReleaseParseResult::Ok);
    QCOMPARE(info.source, QStringLiteral("GitHub"));
    QCOMPARE(info.assets.first().url.toString(), QStringLiteral("https://github.com/x/noda.deb"));
}
