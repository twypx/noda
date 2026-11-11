#pragma once

#include <QObject>

class TestPreferencesDialog : public QObject {
    Q_OBJECT
private slots:
    void treeStructureAndPageMapping();
    void loadAndSaveAllCategories();
    void mainWindowAppliesExtendedPreferences();

    // 存储值未知时不能被首项静默覆盖。
    void unknownStoredValuesArePreserved();
    // “系统默认”字体必须映射回空字符串。
    void systemDefaultFontMapsToEmpty();
    // 官方主题文件可被发现并真实覆盖 LexerStyles，Default 可恢复。
    void officialThemesApplyAndReset();
    // 默认高亮语言真正作用到新建文档。
    void defaultLanguageAppliedToNewDocument();
    // 选中词高亮真的写入/清除指示器。
    void smartHighlightTogglesIndicator();
    // 标签栏四个开关真实生效。
    void tabPreferencesApply();
    // applyPreferences 只应用，不写盘。
    void applyPreferencesDoesNotWriteSettings();
    // 词补全按上游默认算法在输入后弹出候选。
    void wordCompletionTriggers();
    // 首选项切换官方主题后，编辑器真实拿到主题配色。
    void mainWindowAppliesOfficialTheme();
    // 上游 nativeLang 中英文切换与切回中文。
    void interfaceLanguageSwitchesFromNativeLangXml();
    // 主界面字体同时进入应用字体与样式表，切换主题后仍保留。
    void interfaceFontsAffectApplicationAndStyleSheet();
    // 括号高亮开关真正改变匹配状态，并配有可见样式。
    void braceHighlightFollowsPreference();
    // 首选项里不再出现上游关系、实现说明、英文括注或不支持的占位项。
    void preferencesUiIsCleanAndHasNoPlaceholders();
    // 外观页主题色：预设色板 + 颜色选择器 + 自定义色保留。
    void accentColorPresetsAndCustomPicker();
    void accentColorCustomValueIsPreserved();
    // 主题色真正驱动 QSS 与 QPalette（深浅两套都不能残留紫色）。
    void accentColorDrivesStyleSheetAndPalette();
    // 主题色从 QSettings 启动恢复，非法值回退默认。
    void accentColorLoadsFromSettings();
    // 默认主题色必须逐字节还原改造前的紫色令牌，视觉零回归。
    void defaultAccentKeepsOriginalPalette();
    // 目录树（首选项左侧 + 文件树）选中/hover 底色必须跟随主题色，而非写死紫色。
    void treeSelectionFollowsAccentColor();
};
