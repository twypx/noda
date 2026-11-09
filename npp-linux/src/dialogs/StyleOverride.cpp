#include "StyleOverride.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSettings>
#include <QStringList>

namespace {

const char* kGroup = "styleOverride";

std::optional<StyleOverride> parseLegacy(const QString& langId, int styleId,
                                         const QString& value) {
    const QStringList parts = value.split(QLatin1Char(';'));
    if (parts.size() < 2 || langId.isEmpty()) return std::nullopt;
    bool fgOk = false;
    bool bgOk = false;
    StyleOverride o;
    o.langId = langId;
    o.styleId = styleId;
    o.fg = parts[0].toInt(&fgOk);
    o.bg = parts[1].toInt(&bgOk);
    if (!fgOk || !bgOk) return std::nullopt;
    o.bold = parts.size() >= 3 && parts[2].toInt() != 0;
    o.italic = parts.size() >= 4 && parts[3].toInt() != 0;
    return o;
}

} // namespace

QString StyleOverride::settingsKey(const QString& langId, int styleId) {
    return langId + QLatin1Char('/') + QString::number(styleId);
}

QString StyleOverride::serialize() const {
    QJsonObject obj;
    obj.insert(QStringLiteral("fg"), fg);
    obj.insert(QStringLiteral("bg"), bg);
    obj.insert(QStringLiteral("bold"), bold);
    obj.insert(QStringLiteral("italic"), italic);
    obj.insert(QStringLiteral("fontName"), fontName);
    obj.insert(QStringLiteral("fontSize"), fontSize);
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

std::optional<StyleOverride> StyleOverride::deserialize(const QString& key, const QString& value) {
    const QStringList segments = key.split(QLatin1Char('/'));
    QString langId;
    int styleId = 0;
    if (segments.size() == 1) {
        langId = segments[0];           // 旧键：<langId>
    } else if (segments.size() == 2) {
        langId = segments[0];
        bool ok = false;
        styleId = segments[1].toInt(&ok);
        if (!ok) return std::nullopt;
    } else {
        return std::nullopt;
    }
    if (langId.isEmpty() || value.isEmpty()) return std::nullopt;

    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(value.toUtf8(), &error);
    if (error.error == QJsonParseError::NoError && doc.isObject()) {
        const QJsonObject obj = doc.object();
        if (!obj.contains(QStringLiteral("fg")) || !obj.contains(QStringLiteral("bg")))
            return std::nullopt;
        StyleOverride o;
        o.langId = langId;
        o.styleId = styleId;
        o.fg = obj.value(QStringLiteral("fg")).toInt(-1);
        o.bg = obj.value(QStringLiteral("bg")).toInt(-1);
        o.bold = obj.value(QStringLiteral("bold")).toBool();
        o.italic = obj.value(QStringLiteral("italic")).toBool();
        o.fontName = obj.value(QStringLiteral("fontName")).toString();
        o.fontSize = obj.value(QStringLiteral("fontSize")).toInt(0);
        return o;
    }
    return parseLegacy(langId, styleId, value);
}

QList<StyleOverride> StyleOverride::loadAll() {
    QList<StyleOverride> result;
    QSettings settings;
    settings.beginGroup(QLatin1String(kGroup));
    const QStringList keys = settings.allKeys();
    for (const QString& key : keys) {
        auto parsed = deserialize(key, settings.value(key).toString());
        if (parsed) result.append(*parsed);
    }
    settings.endGroup();
    return result;
}
