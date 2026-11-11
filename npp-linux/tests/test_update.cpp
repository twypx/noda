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
    const QByteArray data =
        (hashA + QStringLiteral("  noda_1.0.24-1_amd64.deb\n") + hashB +
         QStringLiteral(" *./Noda-1.0.24-x86_64.AppImage\n") +
         QStringLiteral("# 注释行应被忽略\n") + QStringLiteral("abc  garbage.txt\n"))
            .toUtf8();

    const QHash<QString, QString> sums = NodaUpdate::parseChecksums(data);
    QCOMPARE(sums.value(QStringLiteral("noda_1.0.24-1_amd64.deb")), hashA);
    // 去掉 "*" 与 "./" 前缀，且哈希统一小写。
    QCOMPARE(sums.value(QStringLiteral("Noda-1.0.24-x86_64.AppImage")), hashB.toLower());
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
