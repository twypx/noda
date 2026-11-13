#pragma once

#include <QObject>
#include <QtTest>

class TestUpdate : public QObject {
    Q_OBJECT
private slots:
    void versionParsing();
    void versionComparison();
    void checksumParsing();
    void assetSelection();
    void channelDetection();
    void sha256OfDataAndFile();

    // 更新源按界面语言选择：中文→GitHub，英文→Gitee。
    void sourceSelection();
    // Gitee 返回数组、资产用 download_url，且能正确解析成更新信息。
    void giteeResponseParsing();
    // 回归：GitHub 返回对象、资产用 browser_download_url 的行为保持不变。
    void githubResponseParsingUnchanged();
};
