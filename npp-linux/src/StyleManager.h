#pragma once
#include <string>
#include <unordered_map>
#include <vector>

struct LexerStyle {
    int styleID{0};
    int fgColor{-1};
    int bgColor{-1};
    bool bold{false};
    bool italic{false};
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

class StyleManager {
public:
    static StyleManager& instance() {
        static StyleManager s_instance;
        return s_instance;
    }

    bool loadConfigs(const std::string& langsPath, const std::string& stylersPath);
    std::string detectLanguageByExt(const std::string& ext) const;
    std::string getLexerName(const std::string& langName) const;
    const std::vector<LexerStyle>* getStyles(const std::string& langName) const;
    const LanguageDefinition* getLanguage(const std::string& langName) const;
    const GlobalStyles& getGlobalStyles() const { return _globalStyles; }

    // 供“语言样式配置器”回写：可修改地取出某语言样式表（不存在返回 nullptr）。
    std::vector<LexerStyle>* mutableStyles(const std::string& langName);

    // 覆盖/新增指定 styleID 的前景、背景、粗体、斜体（颜色为 BGR，-1 表示不改动）。
    void overrideStyle(const std::string& langName, int styleID, int fg, int bg, bool bold, bool italic);

    // 已加载的全部语言名（供样式配置器列表使用）。
    std::vector<std::string> languageNames() const;

private:
    StyleManager() = default;

    std::unordered_map<std::string, LanguageDefinition> _languages; // langName -> def
    std::unordered_map<std::string, std::string> _extToLang;        // ext -> langName
    std::unordered_map<std::string, std::vector<LexerStyle>> _styles; // langName -> styles
    GlobalStyles _globalStyles;
};
