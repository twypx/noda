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
};
