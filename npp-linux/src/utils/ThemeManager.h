#pragma once

#include <QString>

class ThemeManager {
public:
    static void applyTheme(bool isDark);
    static QString getStyleSheet(bool isDark);
};
