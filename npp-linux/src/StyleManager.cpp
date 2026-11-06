#include "StyleManager.h"
#include "pugixml.hpp"
#include <sstream>
#include <algorithm>
#include <iostream>

static int parseHexColor(const char* hexStr) {
    if (!hexStr || strlen(hexStr) != 6) return -1;
    char* end = nullptr;
    long val = strtol(hexStr, &end, 16);
    if (end && *end == '\0') {
        // 转换成 Scintilla 需要的 BGR 格式
        int r = (val >> 16) & 0xFF;
        int g = (val >> 8) & 0xFF;
        int b = val & 0xFF;
        return (b << 16) | (g << 8) | r;
    }
    return -1;
}

static int keywordNameToSlot(const std::string& name) {
    if (name == "instre1") return 0;
    if (name == "instre2") return 1;
    if (name == "type1") return 2;
    if (name == "type2") return 3;
    if (name == "type3") return 4;
    if (name == "type4") return 5;
    if (name == "type5") return 6;
    if (name == "type6") return 7;
    return 0; // 兜底
}

bool StyleManager::loadConfigs(const std::string& langsPath, const std::string& stylersPath) {
    pugi::xml_document docLangs;
    if (docLangs.load_file(langsPath.c_str())) {
        pugi::xml_node root = docLangs.child("NotepadPlus").child("Languages");
        for (pugi::xml_node langNode : root.children("Language")) {
            LanguageDefinition lang;
            lang.name = langNode.attribute("name").as_string();
            lang.commentLine = langNode.attribute("commentLine").as_string();
            lang.commentStart = langNode.attribute("commentStart").as_string();
            lang.commentEnd = langNode.attribute("commentEnd").as_string();
            std::string exts = langNode.attribute("ext").as_string();

            std::istringstream iss(exts);
            std::string ext;
            while (iss >> ext) {
                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                lang.extensions.push_back(ext);
                _extToLang[ext] = lang.name;
            }

            for (pugi::xml_node kwNode : langNode.children("Keywords")) {
                std::string kwName = kwNode.attribute("name").as_string();
                std::string kws = kwNode.text().as_string();
                if (!kws.empty() && !kwName.empty()) {
                    lang.rawKeywords[kwName] = kws;
                }
            }

            _languages[lang.name] = std::move(lang);
        }
    }

    pugi::xml_document docStylers;
    if (docStylers.load_file(stylersPath.c_str())) {
        // 1. 解析全局部件样式 GlobalStyles
        pugi::xml_node globalRoot = docStylers.child("NotepadPlus").child("GlobalStyles");
        for (pugi::xml_node widgetNode : globalRoot.children("WidgetStyle")) {
            std::string wName = widgetNode.attribute("name").as_string();
            if (wName == "Default Style") {
                _globalStyles.defaultFg = parseHexColor(widgetNode.attribute("fgColor").as_string());
                _globalStyles.defaultBg = parseHexColor(widgetNode.attribute("bgColor").as_string());
                _globalStyles.defaultFont = widgetNode.attribute("fontName").as_string("Monospace");
                _globalStyles.defaultFontSize = widgetNode.attribute("fontSize").as_int(10);
            } else if (wName == "Line number margin") {
                _globalStyles.lineMarginFg = parseHexColor(widgetNode.attribute("fgColor").as_string());
                _globalStyles.lineMarginBg = parseHexColor(widgetNode.attribute("bgColor").as_string());
            } else if (wName == "Current line background colour") {
                _globalStyles.currentLineBg = parseHexColor(widgetNode.attribute("bgColor").as_string());
            } else if (wName == "Selected text colour") {
                _globalStyles.selectedBg = parseHexColor(widgetNode.attribute("bgColor").as_string());
                _globalStyles.selectedFg = parseHexColor(widgetNode.attribute("fgColor").as_string());
            } else if (wName == "Caret colour") {
                _globalStyles.caretColor = parseHexColor(widgetNode.attribute("fgColor").as_string());
            } else if (wName == "Fold margin") {
                _globalStyles.foldMarginFg = parseHexColor(widgetNode.attribute("fgColor").as_string());
                _globalStyles.foldMarginBg = parseHexColor(widgetNode.attribute("bgColor").as_string());
            }
        }

        // 2. 解析语言词法样式 LexerStyles
        pugi::xml_node root = docStylers.child("NotepadPlus").child("LexerStyles");
        for (pugi::xml_node lexerNode : root.children("LexerType")) {
            std::string langName = lexerNode.attribute("name").as_string();
            std::vector<LexerStyle> styles;

            for (pugi::xml_node styleNode : lexerNode.children("WordsStyle")) {
                LexerStyle s;
                s.styleID = styleNode.attribute("styleID").as_int();
                s.fgColor = parseHexColor(styleNode.attribute("fgColor").as_string());
                s.bgColor = parseHexColor(styleNode.attribute("bgColor").as_string());
                int fontStyle = styleNode.attribute("fontStyle").as_int(0);
                s.bold = (fontStyle & 1) != 0;
                s.italic = (fontStyle & 2) != 0;
                styles.push_back(s);
            }

            _styles[langName] = std::move(styles);
        }
    }

    std::cout << "[Notepad++ Linux] 配置载入成功: " 
              << _languages.size() << " 种语言规则, " 
              << _styles.size() << " 套高亮主题样式。" << std::endl;

    return !_languages.empty();
}

std::string StyleManager::detectLanguageByExt(const std::string& ext) const {
    std::string lowerExt = ext;
    std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(), ::tolower);
    auto it = _extToLang.find(lowerExt);
    if (it != _extToLang.end()) {
        return it->second;
    }
    // 常见默认兜底
    if (lowerExt == "md" || lowerExt == "markdown" || lowerExt == "mdown" || lowerExt == "mkd") return "markdown";
    if (lowerExt == "html" || lowerExt == "htm") return "html";
    if (lowerExt == "xml") return "xml";
    if (lowerExt == "cpp" || lowerExt == "cxx" || lowerExt == "cc" || lowerExt == "c" || lowerExt == "h" || lowerExt == "hpp") return "cpp";
    if (lowerExt == "java") return "java";
    if (lowerExt == "js" || lowerExt == "jsx" || lowerExt == "ts" || lowerExt == "tsx") return "javascript";
    if (lowerExt == "json") return "json";
    if (lowerExt == "py") return "python";
    if (lowerExt == "sh" || lowerExt == "bash") return "bash";
    if (lowerExt == "css") return "css";
    if (lowerExt == "sql") return "sql";
    if (lowerExt == "yaml" || lowerExt == "yml") return "yaml";
    return "normal";
}

std::string StyleManager::getLexerName(const std::string& langName) const {
    static const std::unordered_map<std::string, std::string> s_lexerMap = {
        {"html", "hypertext"},
        {"asp", "hypertext"},
        {"jsp", "hypertext"},
        {"xml", "xml"},
        {"php", "phpscript"},
        {"c", "cpp"},
        {"cpp", "cpp"},
        {"cs", "cpp"},
        {"java", "cpp"},
        {"javascript", "cpp"},
        {"javascript.js", "cpp"},
        {"rc", "cpp"},
        {"actionscript", "cpp"},
        {"python", "python"},
        {"bash", "bash"},
        {"makefile", "makefile"},
        {"batch", "batch"},
        {"ini", "props"},
        {"props", "props"},
        {"sql", "sql"},
        {"css", "css"},
        {"lua", "lua"},
        {"diff", "diff"},
        {"json", "json"},
        {"yaml", "yaml"},
        {"rust", "rust"},
        {"cmake", "cmake"},
        {"ruby", "ruby"},
        {"perl", "perl"},
        {"markdown", "markdown"},
        {"normal", "null"}
    };

    auto it = s_lexerMap.find(langName);
    if (it != s_lexerMap.end()) {
        return it->second;
    }
    return langName;
}

const std::vector<LexerStyle>* StyleManager::getStyles(const std::string& langName) const {
    auto it = _styles.find(langName);
    if (it != _styles.end()) {
        return &it->second;
    }
    return nullptr;
}

const LanguageDefinition* StyleManager::getLanguage(const std::string& langName) const {
    auto it = _languages.find(langName);
    if (it != _languages.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<LexerStyle>* StyleManager::mutableStyles(const std::string& langName) {
    auto it = _styles.find(langName);
    if (it != _styles.end()) {
        return &it->second;
    }
    return nullptr;
}

void StyleManager::overrideStyle(const std::string& langName, int styleID, int fg, int bg, bool bold, bool italic) {
    auto it = _styles.find(langName);
    if (it == _styles.end()) return;
    auto& vec = it->second;
    for (auto& s : vec) {
        if (s.styleID == styleID) {
            if (fg != -1) s.fgColor = fg;
            if (bg != -1) s.bgColor = bg;
            s.bold = bold;
            s.italic = italic;
            return;
        }
    }
    LexerStyle s;
    s.styleID = styleID;
    s.fgColor = fg;
    s.bgColor = bg;
    s.bold = bold;
    s.italic = italic;
    vec.push_back(s);
}

std::vector<std::string> StyleManager::languageNames() const {
    std::vector<std::string> v;
    v.reserve(_languages.size());
    for (const auto& entry : _languages) {
        v.push_back(entry.first);
    }
    std::sort(v.begin(), v.end());
    return v;
}
