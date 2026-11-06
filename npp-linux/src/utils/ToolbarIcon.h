#pragma once

#include <QIcon>
#include <QString>

// 工具栏动作图标：PNG 由 tools/gen_toolbar_icons.py 从 resources/icons/toolbar/src/*.svg
// （Lucide 形态，ISC 许可）离线生成并经 resources/toolbar.qrc 内嵌，
// 因此运行时不需要 Qt6Svg，也不依赖任何外部图标主题。
//
// name 为图标基名（如 "save"、"zoom-in"），darkTheme 决定取深色还是浅色前景的一套。
QIcon toolBarIcon(const QString& name, bool darkTheme);
