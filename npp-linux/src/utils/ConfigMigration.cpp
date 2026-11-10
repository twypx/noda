#include "ConfigMigration.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1String>

namespace ConfigMigration {

bool migrateLegacy(const QString& configHome) {
    if (configHome.isEmpty()) return false;

    const QDir legacy(QDir(configHome).filePath(QStringLiteral("Notepad++")));
    if (!legacy.exists()) return false;

    const QDir target(QDir(configHome).filePath(QStringLiteral("Noda")));
    const QString targetConf = target.filePath(QStringLiteral("Noda.conf"));
    // 已经迁移过 / 用户已有新配置：绝不覆盖。
    if (QFileInfo::exists(targetConf)) return false;
    if (!target.exists() && !QDir().mkpath(target.path())) return false;

    bool copied = false;
    const QFileInfoList entries = legacy.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        QString destination = target.filePath(entry.fileName());
        if (entry.fileName() == QLatin1String("Notepad++.conf")) {
            destination = targetConf;
        }
        if (!QFile::exists(destination) && QFile::copy(entry.absoluteFilePath(), destination)) {
            copied = true;
        }
    }
    return copied;
}

} // namespace ConfigMigration
