#pragma once
#include <cstddef>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct LexerStyle {
    int styleID{0};
    std::string name;       // stylers.model.xml 中 WordsStyle 的 name（供配置器列槽位）
    int fgColor{-1};
    int bgColor{-1};
    bool bold{false};
    bool italic{false};
    std::string fontName;   // 空 = 沿用全局编辑器字体
    int fontSize{0};        // 0  = 沿用全局编辑器字号
};

struct LanguageDefinition {
    std::string name;
    std::vector<std::string> extensions;
    std::unordered_map<std::string, std::string> rawKeywords; // key: "instre1", "type1", etc.
    std::string commentLine;
    std::string commentStart;
    std::string commentEnd;
};

struct GlobalStyles {
    int defaultFg{0x000000};
    int defaultBg{0xFFFFFF};
    std::string defaultFont{"Courier New"};
    int defaultFontSize{10};
    int lineMarginFg{0x808080};
    int lineMarginBg{0xE4E4E4};
    int currentLineBg{0xFFE8E8}; // BGR
    int selectedBg{0xC0C0C0};
    int selectedFg{-1};
    int caretColor{0xFF0080};   // BGR
    int foldMarginFg{0xFFFFFF};
    int foldMarginBg{0xE9E9E9};
};

// 两个 model.xml 的探测结果；searched 记录全部候选路径供失败诊断逐条列出。
struct ConfigPaths {
    std::string langs;
    std::string stylers;
    std::vector<std::string> searched;
    bool complete() const { return !langs.empty() && !stylers.empty(); }
};

// errors 是致命错误（高亮不可用）；warnings 是可降级的单属性问题。
struct ConfigLoadReport {
    bool ok{false};
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    std::size_t languageCount{0};
    std::size_t styleCount{0};
};

// 上游 官方主题文件（PowerEditor/installer/themes/*.xml）的描述。
struct ThemeInfo {
    std::string name;   // 不含 .xml 的显示名
    std::string path;   // 完整路径
};

class StyleManager {
public:
    static StyleManager& instance() {
        static StyleManager s_instance;
        return s_instance;
    }

    // 按 applicationDirPath() 相对推导两个 XML，不依赖编译期绝对路径。
    static ConfigPaths locateConfigFiles(const std::string& appDir);

    // 两个 XML 都必须解析成功且结构非空才算成功；重载会清空上一次的状态。
    ConfigLoadReport loadConfigs(const std::string& langsPath, const std::string& stylersPath);
    const ConfigLoadReport& lastReport() const { return _lastReport; }

    // 面向用户的诊断文本：缺哪个文件、找过哪些路径、有什么后果。
    static std::string buildDiagnostic(const ConfigPaths& paths, const ConfigLoadReport& report);

    std::string detectLanguageByExt(const std::string& ext) const;
    std::string getLexerName(const std::string& langName) const;
    // 把旧版显示名（C++、Shell）或语言 id 统一折算成 langs.model.xml 的 id；
    // 不认识的值返回 "normal"，避免把无效语言名传给词法器。
    std::string resolveLanguageId(const std::string& nameOrId) const;
    const std::vector<LexerStyle>* getStyles(const std::string& langName) const;
    const LanguageDefinition* getLanguage(const std::string& langName) const;
    const GlobalStyles& getGlobalStyles() const { return _globalStyles; }

    // 供“语言样式配置器”回写：可修改地取出某语言样式表（不存在返回 nullptr）。
    std::vector<LexerStyle>* mutableStyles(const std::string& langName);

    // 覆盖/新增指定 styleID 的前景、背景、粗体、斜体（颜色为 BGR，-1 表示不改动）。
    void overrideStyle(const std::string& langName, int styleID, int fg, int bg, bool bold, bool italic);

    // 覆盖指定 styleID 的字体族/字号（空串 / 0 表示该字段不覆盖）。
    void overrideFont(const std::string& langName, int styleID,
                      const std::string& fontName, int fontSize);

    // 已加载的全部语言名（供样式配置器列表使用）。
    std::vector<std::string> languageNames() const;

    // 扫描 <appDir>/themes、<prefix>/share/noda/themes 等位置，
    // 返回按名称排序的官方主题列表；"Default"（内置 stylers）不在其中。
    static std::vector<ThemeInfo> locateThemes(const std::string& appDir);
    // 主题文件是安装期静态资源；仅在安装/卸载主题或测试时清空内存缓存。
    static void invalidateThemeCache();

    // 应用一个主题 XML：只覆盖其中出现的 LexerType，其余语言回退到 stylers.model.xml。
    bool applyThemeFile(const std::string& path, std::string* error = nullptr);
    // 恢复内置 stylers.model.xml 的样式。
    void resetTheme();
    bool hasCustomTheme() const { return !_themePath.empty(); }
    // 当前主题是否真的定义了该语言的样式（深色模式下决定背景是否跟主题走）。
    bool hasThemeStyle(const std::string& langName) const;

private:
    StyleManager() = default;

    std::unordered_map<std::string, LanguageDefinition> _languages; // langName -> def
    std::unordered_map<std::string, std::string> _extToLang;        // ext -> langName
    std::unordered_map<std::string, std::vector<LexerStyle>> _styles; // langName -> styles
    // stylers.model.xml 的原始样式；切换/恢复主题时作为兜底，避免丢失语言。
    std::unordered_map<std::string, std::vector<LexerStyle>> _baseStyles;
    std::unordered_set<std::string> _themedLanguages;
    std::string _themePath;
    GlobalStyles _globalStyles;
    ConfigLoadReport _lastReport;
};
