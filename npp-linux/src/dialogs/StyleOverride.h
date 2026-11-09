#pragma once

#include <QList>
#include <QString>
#include <optional>

// 一条样式覆盖记录。与 QWidget 解耦，便于独立回归测试。
//
// 持久化在 QSettings 的 "styleOverride" 组：
//   新格式  key = <langId>/<styleId>，value = JSON（含字体族/字号）
//   旧格式  key = <langId>，          value = "fg;bg;bold;italic"（等价 styleId 0）
// deserialize() 同时认两种，升级前保存的配置不会失效。
struct StyleOverride {
    QString langId;      // "global" 表示全局默认样式
    int styleId{0};
    int fg{-1};          // BGR，同 Scintilla/StyleManager
    int bg{-1};
    bool bold{false};
    bool italic{false};
    QString fontName;    // 空 = 不覆盖字体族
    int fontSize{0};     // 0  = 不覆盖字号

    QString serialize() const;
    static QString settingsKey(const QString& langId, int styleId);

    // key 是组内相对键。无效输入返回 nullopt。
    static std::optional<StyleOverride> deserialize(const QString& key, const QString& value);

    // 读出全部有效覆盖（含旧格式），供 MainWindow 回灌。
    static QList<StyleOverride> loadAll();
};
