#include "StyleManager.h"
#include "pugixml.hpp"
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <mutex>

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

namespace {

std::string parseFailure(const char* what, const std::string& path,
                         const pugi::xml_parse_result& res) {
    return std::string(what) + " 读取/解析失败（" + path + "）：" + res.description()
         + "，偏移 " + std::to_string(res.offset);
}

// stylers.model.xml 与官方主题 XML 的 LexerStyles 结构完全一致，共用一份解析。
void parseLexerStyles(const pugi::xml_node& lexerRoot,
                      std::unordered_map<std::string, std::vector<LexerStyle>>& out) {
    for (pugi::xml_node lexerNode : lexerRoot.children("LexerType")) {
        const std::string langName = lexerNode.attribute("name").as_string();
        std::vector<LexerStyle> styles;
        for (pugi::xml_node styleNode : lexerNode.children("WordsStyle")) {
            LexerStyle s;
            s.styleID = styleNode.attribute("styleID").as_int();
            s.name = styleNode.attribute("name").as_string();
            s.fgColor = parseHexColor(styleNode.attribute("fgColor").as_string());
            s.bgColor = parseHexColor(styleNode.attribute("bgColor").as_string());
            s.fontName = styleNode.attribute("fontName").as_string();
            s.fontSize = styleNode.attribute("fontSize").as_int(0);
            const int fontStyle = styleNode.attribute("fontStyle").as_int(0);
            s.bold = (fontStyle & 1) != 0;
            s.italic = (fontStyle & 2) != 0;
            styles.push_back(std::move(s));
        }
        if (!langName.empty()) out[langName] = std::move(styles);
    }
}

} // namespace

ConfigPaths StyleManager::locateConfigFiles(const std::string& appDir) {
    ConfigPaths paths;
    const std::vector<std::string> langs = {
        appDir + "/langs.model.xml",
        appDir + "/../share/noda/langs.model.xml",
        appDir + "/../../PowerEditor/src/langs.model.xml",
        appDir + "/../PowerEditor/src/langs.model.xml",
        "./PowerEditor/src/langs.model.xml",
        "./langs.model.xml"
    };
    const std::vector<std::string> stylers = {
        appDir + "/stylers.model.xml",
        appDir + "/../share/noda/stylers.model.xml",
        appDir + "/../../PowerEditor/src/stylers.model.xml",
        appDir + "/../PowerEditor/src/stylers.model.xml",
        "./PowerEditor/src/stylers.model.xml",
        "./stylers.model.xml"
    };
    // 词法规范化：诊断文本里不该出现 "bin/../share/..." 这种绕弯路径。
    auto probe = [&paths](const std::vector<std::string>& candidates, std::string& out) {
        for (const auto& raw : candidates) {
            const std::filesystem::path normalized = std::filesystem::path(raw).lexically_normal();
            const std::string candidate = normalized.string();
            paths.searched.push_back(candidate);
            std::error_code ec;
            if (out.empty() && std::filesystem::is_regular_file(normalized, ec) && !ec)
                out = candidate;
        }
    };
    probe(langs, paths.langs);
    probe(stylers, paths.stylers);
    return paths;
}

std::string StyleManager::buildDiagnostic(const ConfigPaths& paths,
                                          const ConfigLoadReport& report) {
    std::ostringstream os;
    os << "语法高亮配置载入失败；文本编辑、查找与保存不受影响，但语言菜单与语法高亮不可用。\n\n";
    os << "langs.model.xml: " << (paths.langs.empty() ? "未找到" : paths.langs) << "\n";
    os << "stylers.model.xml: " << (paths.stylers.empty() ? "未找到" : paths.stylers) << "\n";
    if (!report.errors.empty()) {
        os << "\n错误：\n";
        for (const auto& e : report.errors) os << "  - " << e << "\n";
    }
    if (!report.warnings.empty()) {
        os << "\n警告：\n";
        for (const auto& w : report.warnings) os << "  - " << w << "\n";
    }
    if (!paths.searched.empty()) {
        os << "\n已搜索路径：\n";
        for (const auto& s : paths.searched) os << "  " << s << "\n";
    }
    os << "\n完整安装时这两个 XML 位于 <prefix>/share/noda/，"
          "源码树中位于 PowerEditor/src/。";
    return os.str();
}

ConfigLoadReport StyleManager::loadConfigs(const std::string& langsPath,
                                           const std::string& stylersPath) {
    ConfigLoadReport report;
    // 重载必须替换而非累积，否则换配置后旧语言/旧样式会残留。
    _languages.clear();
    _extToLang.clear();
    _styles.clear();
    _globalStyles = GlobalStyles{};

    pugi::xml_document docLangs;
    const pugi::xml_parse_result langRes = docLangs.load_file(langsPath.c_str());
    if (!langRes) {
        report.errors.push_back(parseFailure("langs.model.xml", langsPath, langRes));
    } else {
        pugi::xml_node root = docLangs.child("NotepadPlus").child("Languages");
        if (!root) {
            report.errors.push_back("langs.model.xml 缺少 NotepadPlus/Languages 节点");
        } else {
            for (pugi::xml_node langNode : root.children("Language")) {
                LanguageDefinition lang;
                lang.name = langNode.attribute("name").as_string();
                lang.commentLine = langNode.attribute("commentLine").as_string();
                lang.commentStart = langNode.attribute("commentStart").as_string();
                lang.commentEnd = langNode.attribute("commentEnd").as_string();
                const std::string exts = langNode.attribute("ext").as_string();

                std::istringstream iss(exts);
                std::string ext;
                while (iss >> ext) {
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    lang.extensions.push_back(ext);
                    _extToLang[ext] = lang.name;
                }
                for (pugi::xml_node kwNode : langNode.children("Keywords")) {
                    const std::string kwName = kwNode.attribute("name").as_string();
                    const std::string kws = kwNode.text().as_string();
                    if (!kws.empty() && !kwName.empty()) lang.rawKeywords[kwName] = kws;
                }
                if (!lang.name.empty()) _languages[lang.name] = std::move(lang);
            }
            if (_languages.empty())
                report.errors.push_back("langs.model.xml 未定义任何语言（Languages 为空）");
        }
    }

    pugi::xml_document docStylers;
    const pugi::xml_parse_result styleRes = docStylers.load_file(stylersPath.c_str());
    if (!styleRes) {
        report.errors.push_back(parseFailure("stylers.model.xml", stylersPath, styleRes));
    } else {
        pugi::xml_node globalRoot = docStylers.child("NotepadPlus").child("GlobalStyles");
        pugi::xml_node lexerRoot = docStylers.child("NotepadPlus").child("LexerStyles");
        if (!globalRoot)
            report.warnings.push_back("stylers.model.xml 缺少 GlobalStyles 节点，使用内置外观");
        if (!lexerRoot)
            report.errors.push_back("stylers.model.xml 缺少 NotepadPlus/LexerStyles 节点");

        // 颜色解析失败时保留成员默认值并留告警；旧实现会把 -1 直接写进 GlobalStyles。
        auto applyColor = [&report](const pugi::xml_node& node, const char* attr,
                                   int& target, const char* where) {
            const char* raw = node.attribute(attr).as_string();
            if (!raw || !*raw) return;
            const int value = parseHexColor(raw);
            if (value < 0) {
                report.warnings.push_back(std::string(where) + "." + attr
                                         + " 颜色值「" + raw + "」非法，保留默认值");
                return;
            }
            target = value;
        };

        if (globalRoot) {
            for (pugi::xml_node widgetNode : globalRoot.children("WidgetStyle")) {
                const std::string wName = widgetNode.attribute("name").as_string();
                if (wName == "Default Style") {
                    applyColor(widgetNode, "fgColor", _globalStyles.defaultFg, "Default Style");
                    applyColor(widgetNode, "bgColor", _globalStyles.defaultBg, "Default Style");
                    _globalStyles.defaultFont = widgetNode.attribute("fontName").as_string("Monospace");
                    _globalStyles.defaultFontSize = widgetNode.attribute("fontSize").as_int(10);
                } else if (wName == "Line number margin") {
                    applyColor(widgetNode, "fgColor", _globalStyles.lineMarginFg, wName.c_str());
                    applyColor(widgetNode, "bgColor", _globalStyles.lineMarginBg, wName.c_str());
                } else if (wName == "Current line background colour") {
                    applyColor(widgetNode, "bgColor", _globalStyles.currentLineBg, wName.c_str());
                } else if (wName == "Selected text colour") {
                    applyColor(widgetNode, "bgColor", _globalStyles.selectedBg, wName.c_str());
                    applyColor(widgetNode, "fgColor", _globalStyles.selectedFg, wName.c_str());
                } else if (wName == "Caret colour") {
                    applyColor(widgetNode, "fgColor", _globalStyles.caretColor, wName.c_str());
                } else if (wName == "Fold margin") {
                    applyColor(widgetNode, "fgColor", _globalStyles.foldMarginFg, wName.c_str());
                    applyColor(widgetNode, "bgColor", _globalStyles.foldMarginBg, wName.c_str());
                }
            }
        }

        if (lexerRoot) parseLexerStyles(lexerRoot, _styles);
        if (_styles.empty())
            report.errors.push_back("stylers.model.xml 未定义任何 LexerType 样式");
        // 保存内置样式作为主题兜底；重新载入配置时主题回到 Default。
        _baseStyles = _styles;
        _themePath.clear();
        _themedLanguages.clear();
    }

    report.languageCount = _languages.size();
    report.styleCount = _styles.size();
    report.ok = report.errors.empty() && report.languageCount > 0 && report.styleCount > 0;

    if (report.ok) {
        std::cout << "[Noda] 配置载入成功: "
                  << report.languageCount << " 种语言规则, "
                  << report.styleCount << " 套高亮主题样式。" << std::endl;
    } else {
        std::cerr << "[Noda] 配置载入失败：" << report.errors.size()
                  << " 项错误（详见启动提示）。" << std::endl;
    }
    _lastReport = report;
    return report;
}

std::string StyleManager::resolveLanguageId(const std::string& nameOrId) const {
    if (nameOrId.empty() || nameOrId == "normal" || nameOrId == "Normal Text")
        return "normal";
    if (_languages.count(nameOrId)) return nameOrId;

    static const std::unordered_map<std::string, std::string> alias = {
        {"C++", "cpp"}, {"C#", "cs"}, {"JavaScript", "javascript"},
        {"TypeScript", "typescript"}, {"Shell", "bash"}, {"HTML", "html"},
        {"XML", "xml"}, {"SQL", "sql"}, {"Python", "python"}, {"Java", "java"},
        {"Go", "go"}, {"Rust", "rust"}, {"JSON", "json"}, {"CSS", "css"},
        {"PHP", "php"}, {"YAML", "yaml"}, {"Markdown", "markdown"},
    };
    const auto it = alias.find(nameOrId);
    if (it != alias.end() && _languages.count(it->second)) return it->second;
    return "normal";
}

namespace {
// 主题 XML 是安装期静态资源，运行期不会增删。按搜索根缓存扫描结果，
// 避免每次打开首选项 / 切换主题都做 5 次 directory_iterator 磁盘遍历。
std::mutex g_themeCacheMutex;
std::map<std::string, std::vector<ThemeInfo>> g_themeCache;
} // namespace

void StyleManager::invalidateThemeCache() {
    std::lock_guard<std::mutex> lock(g_themeCacheMutex);
    g_themeCache.clear();
}

std::vector<ThemeInfo> StyleManager::locateThemes(const std::string& appDir) {
    {
        std::lock_guard<std::mutex> lock(g_themeCacheMutex);
        const auto hit = g_themeCache.find(appDir);
        if (hit != g_themeCache.end()) return hit->second;
    }

    const std::vector<std::string> dirs = {
        appDir + "/themes",
        appDir + "/../share/noda/themes",
        // 构建目录在 npp-linux/build，源码树主题在 npp-linux/themes。
        appDir + "/../themes",
        appDir + "/../../npp-linux/themes",
        "./themes",
    };

    std::map<std::string, std::string> byName; // 同名主题以先命中的目录为准
    for (const auto& rawDir : dirs) {
        const std::filesystem::path dir =
            std::filesystem::path(rawDir).lexically_normal();
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec) || ec) continue;

        for (std::filesystem::directory_iterator it(dir, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file(ec) || ec) continue;
            if (it->path().extension() != ".xml") continue;
            const std::string name = it->path().stem().string();
            if (!name.empty()) byName.emplace(name, it->path().string());
        }
    }

    std::vector<ThemeInfo> themes;
    themes.reserve(byName.size());
    for (auto& entry : byName) themes.push_back({entry.first, entry.second});
    {
        std::lock_guard<std::mutex> lock(g_themeCacheMutex);
        g_themeCache.emplace(appDir, themes);
    }
    return themes;
}

bool StyleManager::applyThemeFile(const std::string& path, std::string* error) {
    pugi::xml_document doc;
    const pugi::xml_parse_result res = doc.load_file(path.c_str());
    if (!res) {
        if (error) *error = parseFailure("主题", path, res);
        return false;
    }

    const pugi::xml_node lexerRoot = doc.child("NotepadPlus").child("LexerStyles");
    if (!lexerRoot) {
        if (error) *error = "主题文件缺少 NotepadPlus/LexerStyles 节点：" + path;
        return false;
    }

    std::unordered_map<std::string, std::vector<LexerStyle>> themed;
    parseLexerStyles(lexerRoot, themed);
    if (themed.empty()) {
        if (error) *error = "主题文件未定义任何 LexerType 样式：" + path;
        return false;
    }

    // 主题只覆盖它声明的语言，其余回退到内置 stylers，避免切换主题后一半语言没高亮。
    _styles = _baseStyles;
    _themedLanguages.clear();
    for (auto& entry : themed) {
        _themedLanguages.insert(entry.first);
        _styles[entry.first] = std::move(entry.second);
    }
    _themePath = path;
    return true;
}

void StyleManager::resetTheme() {
    _styles = _baseStyles;
    _themePath.clear();
    _themedLanguages.clear();
}

bool StyleManager::hasThemeStyle(const std::string& langName) const {
    return _themedLanguages.count(langName) != 0;
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
    if (lowerExt == "js" || lowerExt == "jsx") return "javascript";
    // .ts / .tsx 属于 TypeScript（langs.model.xml 的 typescript ext），不能再兜底成 JavaScript。
    if (lowerExt == "ts" || lowerExt == "tsx") return "typescript";
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
        // 上游 ScintillaEditView.cpp:191 把 TypeScript 登记为 cpp 词法器；
        // Lexilla 没有名为 typescript 的模块，原样传入 CreateLexer 会静默得到 nullptr。
        {"typescript", "cpp"},
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

void StyleManager::overrideFont(const std::string& langName, int styleID,
                                const std::string& fontName, int fontSize) {
    auto it = _styles.find(langName);
    if (it == _styles.end()) return;
    for (auto& s : it->second) {
        if (s.styleID == styleID) {
            if (!fontName.empty()) s.fontName = fontName;
            if (fontSize > 0) s.fontSize = fontSize;
            return;
        }
    }
    LexerStyle s;
    s.styleID = styleID;
    if (!fontName.empty()) s.fontName = fontName;
    if (fontSize > 0) s.fontSize = fontSize;
    it->second.push_back(s);
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
