#pragma once

#include <QString>

namespace ConfigMigration {

// 产品由旧名改为 Noda 后，QSettings 落点从 <configHome>/Notepad++/Notepad++.conf
// 变为 <configHome>/Noda/Noda.conf。启动时做一次性迁移，避免用户设置/会话丢失。
// - configHome 传入 QStandardPaths::GenericConfigLocation（或测试用临时目录）。
// - 新配置已存在时不覆盖，保证可重复调用（幂等）。
// - 返回本次是否实际复制了文件。
bool migrateLegacy(const QString& configHome);

} // namespace ConfigMigration
