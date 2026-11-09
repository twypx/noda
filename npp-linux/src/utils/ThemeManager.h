#pragma once

#include <QColor>
#include <QString>

// 目录树需要自绘缩进区底色，颜色必须和 QSS 里的 ::item:hover / ::item:selected 完全一致，
// 否则会出现「文字那一截变了色、缩进区没变」的断层。两边共用同一组常量。
struct FolderTreeColors {
    QColor hover;
    QColor selected;
};

class ThemeManager {
public:
    static void applyTheme(bool isDark);
    static QString getStyleSheet(bool isDark);
    // 当前主题下的目录树行底色；applyTheme() 之前默认按浅色处理。
    static FolderTreeColors folderTreeColors();
};
