#include "EditorView.h"
#include "StyleManager.h"
#include "utils/EncodingDetector.h"
#include "utils/EncodingConverter.h"
#include "ILexer.h"
#include "Lexilla.h"
#include <QFile>
#include <QSaveFile>
#include <QFileInfo>
#include <QByteArray>
#include <QApplication>
#include <QClipboard>
#include <QDebug>
#include <QStringList>
#include <QElapsedTimer>
#include <QSet>
#include <QTimer>
#include <QMouseEvent>
#include <QDesktopServices>
#include <QUrl>
#include <QStringList>
#include <algorithm>
#include <vector>
#include <cctype>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

EditorView::EditorView(QWidget* parent)
    : ScintillaEditBase(parent)
    , _buffer(std::make_shared<DocumentBuffer>())
{
    initEditorDefaults();

    connect(this, &ScintillaEditBase::savePointChanged, this, [this](bool dirty) {
        _buffer->setModified(dirty);
        if (dirty) _buffer->setBackupDirty(true);
        emit documentModified(dirty);
    });

    connect(this, &ScintillaEditBase::notifyChange, this, [this]() {
        _buffer->setBackupDirty(true);
    });

    // 选中词高亮防抖：拖动选择时每次 updateUi 都全文档搜索会明显拖慢大文件。
    _hlTimer = new QTimer(this);
    _hlTimer->setSingleShot(true);
    _hlTimer->setInterval(120);
    connect(_hlTimer, &QTimer::timeout, this, &EditorView::updateSmartHighlight);

    connect(this, &ScintillaEditBase::updateUi, this, &EditorView::onUpdateUi);

    // 自动补全：与上游 AutoCompletion::update 一样，在字符输入后按当前词前缀触发。
    connect(this, &ScintillaEditBase::charAdded, this, [this](int ch) {
        updateAutoCompletion(ch);
    });

    connect(this, &ScintillaEditBase::marginClicked, this, [this](Scintilla::Position pos, Scintilla::KeyMod, int margin) {
        if (margin == 1) {
            auto line = send(SCI_LINEFROMPOSITION, pos);
            const int MARK_BOOKMARK = 24;
            if (send(SCI_MARKERGET, line) & (1 << MARK_BOOKMARK)) {
                send(SCI_MARKERDELETE, line, MARK_BOOKMARK);
            } else {
                send(SCI_MARKERADD, line, MARK_BOOKMARK);
            }
        } else if (margin == 2) {
            auto line = send(SCI_LINEFROMPOSITION, pos);
            send(SCI_TOGGLEFOLD, line);
        }
    });
}

static inline int scRgb(int r, int g, int b) {
    return (b << 16) | (g << 8) | r;
}

// 8-12 已被「格式标记」占用，选中词高亮用 7、括号高亮用 6。
static constexpr int kBraceHighlightIndicator = 6;
static constexpr int kSmartHighlightIndicator = 7;
// URL 可点击链接指示器（容器指示器，避开 6/7 高亮与 8-12 格式标记）。
static constexpr int kUrlIndicator = 13;
// 单次最多标记的匹配数，避免在超长文件里把整个文档都涂满。
static constexpr int kMaxSmartHighlightMatches = 1000;
// 选词高亮只覆盖「当前视口 ±N 行」：屏幕之外的数 MB 内容涂了也看不见。
// 缓冲带让小幅滚动不必重算，滚出带外再由 updateUi 的滚动通知刷新。
static constexpr sptr_t kHighlightContextLines = 200;
// 单次重算的时间预算，超时立即熔断，绝不让选词拖住 UI 线程。
static constexpr qint64 kHighlightBudgetMs = 5;

// ---------------- URL 识别（移植自 上游 的 isUrl 系列，去掉 Win32 InternetCrackUrl） ----------------
// 全部基于 QString/QChar 操作，scheme 一旦确认即视为有效链接，边界处理与原版保持一致。
namespace {

bool isUrlSchemeStartChar(QChar c) {
    return c.isUpper() || c.isLower();
}

// scheme 之前允许出现的分隔符（非字母、非数字、非下划线）。
bool isUrlSchemeDelimiter(QChar c) {
    return !(c.isDigit() || c.isUpper() || c.isLower() || c == '_');
}

// URL 普通文本字符：排除空白（含 Unicode 空白）、以及若干在查询/结构里具有特殊含义的符号。
bool isUrlTextChar(QChar c) {
    if (c.isNull() || c.unicode() <= ' ') return false;
    switch (c.unicode()) {
        case 0x00A0: case 0x2002: case 0x2003: case 0x3000: case 0x2004:
        case 0x2005: case 0x2006: case 0x2007: case 0x2008: case 0x2009:
        case 0x200A: case 0x200B: case 0x202F: case 0x205F: case 0xFEFF:
            return false;
    }
    switch (c.unicode()) {
        case '"': case '#': case '<': case '>': case '{': case '}':
        case '?': case 0x007F:
            return false;
    }
    return true;
}

bool isUrlQueryDelimiter(QChar c) {
    return c == '&' || c == '+' || c == '=' || c == ';';
}

bool isUrlSchemeSupported(const QString& url, int remainingLength) {
    static const QStringList schemes = {
        QStringLiteral("ftp://"), QStringLiteral("http://"), QStringLiteral("https://"),
        QStringLiteral("mailto:"), QStringLiteral("file://")};
    for (const QString& s : schemes) {
        if (s.length() <= remainingLength && url.startsWith(s, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

// 在 text[start..] 寻找 URL 起点；找到时 distance=起点偏移、schemeLength=scheme 长度。
bool scanToUrlStart(const QString& text, int textLen, int start, int* distance, int* schemeLength) {
    int p = start;
    int p0 = 0;
    enum { sUnknown, sScheme } st = sUnknown;
    while (p < textLen) {
        const QChar ch = text.at(p);
        if (st == sUnknown) {
            if (isUrlSchemeStartChar(ch) && (p == 0 || isUrlSchemeDelimiter(text.at(p - 1)))) {
                p0 = p;
                st = sScheme;
            }
        } else { // sScheme
            if (ch == ':' && isUrlSchemeSupported(text.mid(p0), textLen - p0)) {
                *distance = p0 - start;
                *schemeLength = p - p0 + 1;
                return true;
            }
            if (!isUrlSchemeStartChar(ch)) st = sUnknown;
        }
        ++p;
    }
    *schemeLength = 0;
    *distance = p - start;
    return false;
}

// 从 URL 主体起点（跳过 scheme）扫描到 URL 末尾，distance=主体+尾部的长度。
void scanToUrlEnd(const QString& text, int textLen, int start, int* distance) {
    int p = start;
    QChar q;
    enum { sHostAndPath, sQuery, sQueryAfterDelimiter, sQueryQuotes, sQueryAfterQuotes, sFragment } st = sHostAndPath;
    while (p < textLen) {
        const QChar ch = text.at(p);
        switch (st) {
            case sHostAndPath:
                if (ch == '?') st = sQuery;
                else if (ch == '#') st = sFragment;
                else if (!isUrlTextChar(ch)) { *distance = p - start; return; }
                break;
            case sQuery:
                if (ch == '#') st = sFragment;
                else if (isUrlQueryDelimiter(ch)) st = sQueryAfterDelimiter;
                else if (!isUrlTextChar(ch)) { *distance = p - start; return; }
                break;
            case sQueryAfterDelimiter:
                if (ch == '\'' || ch == '"' || ch == '`') { q = ch; st = sQueryQuotes; }
                else if (ch == '(') { q = ')'; st = sQueryQuotes; }
                else if (ch == '[') { q = ']'; st = sQueryQuotes; }
                else if (ch == '{') { q = '}'; st = sQueryQuotes; }
                else if (isUrlTextChar(ch)) st = sQuery;
                else { *distance = p - start; return; }
                break;
            case sQueryQuotes:
                if (ch < ' ') { *distance = p - start; return; }
                if (ch == q) st = sQueryAfterQuotes;
                break;
            case sQueryAfterQuotes:
                if (isUrlQueryDelimiter(ch)) st = sQueryAfterDelimiter;
                else { *distance = p - start; return; }
                break;
            case sFragment:
                if (ch != '?' && !isUrlTextChar(ch)) { *distance = p - start; return; }
                break;
        }
        ++p;
    }
    *distance = p - start;
}

void removeUnwantedTrailingCharFromEnclosedUrl(int start, const QString& text, int* length) {
    if (start > 0 && text.at(start - 1) == '\'' && text.at(start + *length - 1) == '\'') *length -= 1;
    if (start > 0 && text.at(start - 1) == '`' && text.at(start + *length - 1) == '`') *length -= 1;
}

bool removeUnwantedTrailingCharFromUrl(const QString& text, int* length) {
    int l = *length - 1;
    if (l <= 0) return false;
    static const QString singleChars = QStringLiteral(".,:;?!#");
    for (const QChar sc : singleChars) {
        if (text.at(l) == sc) { *length = l; return true; }
    }
    static const QString closing = QStringLiteral(")]");
    static const QString opening = QStringLiteral("([");
    for (int i = 0; i < closing.length(); ++i) {
        if (text.at(l) == closing.at(i)) {
            int count = 0;
            for (int j = l - 1; j >= 0; --j) {
                if (text.at(j) == closing.at(i)) count++;
                if (text.at(j) == opening.at(i)) {
                    if (count > 0) count--;
                    else return false;
                }
            }
            if (count != 0) return false;
            *length = l;
            return true;
        }
    }
    return false;
}

// text[start..] 是否以合法 URL 开头；成功时 segmentLen=URL 长度，否则为到下一候选的偏移。
bool isUrl(const QString& text, int textLen, int start, int* segmentLen) {
    int dist = 0, schemeLen = 0;
    if (scanToUrlStart(text, textLen, start, &dist, &schemeLen)) {
        if (dist) {
            *segmentLen = dist;
            return false;
        }
        int len = 0;
        scanToUrlEnd(text, textLen, start + schemeLen, &len);
        if (len) {
            len += schemeLen;
            // 原版用 Win32 InternetCrackUrl 校验；本移植版改为结构校验（scheme 已确认即视为有效）。
            removeUnwantedTrailingCharFromEnclosedUrl(start, text, &len);
            while (removeUnwantedTrailingCharFromUrl(text.mid(start), &len)) {}
            *segmentLen = len;
            return true;
        }
        len = 1;
        const int lMax = textLen - start;
        while (isUrlSchemeStartChar(text.at(start + len)) && len < lMax) len++;
        *segmentLen = len;
        return false;
    }
    *segmentLen = dist;
    return false;
}

// 返回 text 中所有 URL 的 [起始, 长度]（以 QString 字符索引计）。
QVector<QPair<int, int>> findUrls(const QString& text) {
    QVector<QPair<int, int>> out;
    const int textLen = text.length();
    int start = 0;
    while (start < textLen) {
        int segLen = 0;
        const bool r = isUrl(text, textLen, start, &segLen);
        if (segLen <= 0) break;
        if (r) out.append({start, segLen});
        start += segLen;
        if (start >= textLen) break;
    }
    return out;
}

} // namespace

void EditorView::setupMarkdownStyles(bool isDark) {
    int fs = (_fontSize > 0) ? _fontSize : 10;
    if (isDark) {
        // 深色模式 Markdown 配色 (Obsidian Dark 调色盘)
        send(SCI_STYLESETFORE, 0, scRgb(218, 218, 218)); // DEFAULT
        // H1 ~ H6 (分层大标题与加粗)
        send(SCI_STYLESETFORE, 6, scRgb(168, 130, 255)); send(SCI_STYLESETBOLD, 6, 1); send(SCI_STYLESETSIZE, 6, fs + 3);
        send(SCI_STYLESETFORE, 7, scRgb(2, 122, 255));   send(SCI_STYLESETBOLD, 7, 1); send(SCI_STYLESETSIZE, 7, fs + 2);
        send(SCI_STYLESETFORE, 8, scRgb(83, 223, 221));  send(SCI_STYLESETBOLD, 8, 1); send(SCI_STYLESETSIZE, 8, fs + 1);
        send(SCI_STYLESETFORE, 9, scRgb(68, 207, 110));  send(SCI_STYLESETBOLD, 9, 1);
        send(SCI_STYLESETFORE, 10, scRgb(233, 151, 63)); send(SCI_STYLESETBOLD, 10, 1);
        send(SCI_STYLESETFORE, 11, scRgb(233, 151, 63)); send(SCI_STYLESETBOLD, 11, 1);
        // 粗体 & 斜体
        send(SCI_STYLESETFORE, 2, scRgb(255, 255, 255)); send(SCI_STYLESETBOLD, 2, 1);
        send(SCI_STYLESETFORE, 3, scRgb(255, 255, 255)); send(SCI_STYLESETBOLD, 3, 1);
        send(SCI_STYLESETFORE, 4, scRgb(200, 200, 200)); send(SCI_STYLESETITALIC, 4, 1);
        send(SCI_STYLESETFORE, 5, scRgb(200, 200, 200)); send(SCI_STYLESETITALIC, 5, 1);
        // 代码块与行内代码
        send(SCI_STYLESETFORE, 19, scRgb(233, 151, 63)); send(SCI_STYLESETBACK, 19, scRgb(38, 38, 38));
        send(SCI_STYLESETFORE, 20, scRgb(233, 151, 63)); send(SCI_STYLESETBACK, 20, scRgb(38, 38, 38));
        send(SCI_STYLESETFORE, 21, scRgb(233, 151, 63)); send(SCI_STYLESETBACK, 21, scRgb(34, 34, 34));
        // 链接
        send(SCI_STYLESETFORE, 18, scRgb(168, 130, 255)); send(SCI_STYLESETUNDERLINE, 18, 1);
        // 引用
        send(SCI_STYLESETFORE, 15, scRgb(150, 150, 150)); send(SCI_STYLESETITALIC, 15, 1);
        // 列表
        send(SCI_STYLESETFORE, 13, scRgb(168, 130, 255));
        send(SCI_STYLESETFORE, 14, scRgb(168, 130, 255));
        // 分割线
        send(SCI_STYLESETFORE, 17, scRgb(80, 80, 80));
        // YAML Frontmatter (22, 23, 24)
        send(SCI_STYLESETFORE, 22, scRgb(102, 102, 102));
        send(SCI_STYLESETFORE, 23, scRgb(150, 150, 150));
        send(SCI_STYLESETFORE, 24, scRgb(168, 130, 255));
    } else {
        // 浅色模式 Markdown 配色 (Obsidian Light 调色盘)
        send(SCI_STYLESETFORE, 0, scRgb(34, 34, 34)); // DEFAULT
        // H1 ~ H6
        send(SCI_STYLESETFORE, 6, scRgb(120, 82, 238)); send(SCI_STYLESETBOLD, 6, 1); send(SCI_STYLESETSIZE, 6, fs + 3);
        send(SCI_STYLESETFORE, 7, scRgb(8, 109, 221));  send(SCI_STYLESETBOLD, 7, 1); send(SCI_STYLESETSIZE, 7, fs + 2);
        send(SCI_STYLESETFORE, 8, scRgb(0, 191, 188));  send(SCI_STYLESETBOLD, 8, 1); send(SCI_STYLESETSIZE, 8, fs + 1);
        send(SCI_STYLESETFORE, 9, scRgb(8, 185, 78));   send(SCI_STYLESETBOLD, 9, 1);
        send(SCI_STYLESETFORE, 10, scRgb(236, 117, 0)); send(SCI_STYLESETBOLD, 10, 1);
        send(SCI_STYLESETFORE, 11, scRgb(236, 117, 0)); send(SCI_STYLESETBOLD, 11, 1);
        // 粗体 & 斜体
        send(SCI_STYLESETFORE, 2, scRgb(34, 34, 34)); send(SCI_STYLESETBOLD, 2, 1);
        send(SCI_STYLESETFORE, 3, scRgb(34, 34, 34)); send(SCI_STYLESETBOLD, 3, 1);
        send(SCI_STYLESETFORE, 4, scRgb(92, 92, 92)); send(SCI_STYLESETITALIC, 4, 1);
        send(SCI_STYLESETFORE, 5, scRgb(92, 92, 92)); send(SCI_STYLESETITALIC, 5, 1);
        // 代码块与行内代码
        send(SCI_STYLESETFORE, 19, scRgb(213, 57, 132)); send(SCI_STYLESETBACK, 19, scRgb(242, 239, 252));
        send(SCI_STYLESETFORE, 20, scRgb(213, 57, 132)); send(SCI_STYLESETBACK, 20, scRgb(242, 239, 252));
        send(SCI_STYLESETFORE, 21, scRgb(213, 57, 132)); send(SCI_STYLESETBACK, 21, scRgb(246, 246, 246));
        // 链接
        send(SCI_STYLESETFORE, 18, scRgb(120, 82, 238)); send(SCI_STYLESETUNDERLINE, 18, 1);
        // 引用
        send(SCI_STYLESETFORE, 15, scRgb(92, 92, 92)); send(SCI_STYLESETITALIC, 15, 1);
        // 列表
        send(SCI_STYLESETFORE, 13, scRgb(120, 82, 238));
        send(SCI_STYLESETFORE, 14, scRgb(120, 82, 238));
        // 分割线
        send(SCI_STYLESETFORE, 17, scRgb(218, 218, 218));
        // YAML Frontmatter (22, 23, 24)
        send(SCI_STYLESETFORE, 22, scRgb(171, 171, 171));
        send(SCI_STYLESETFORE, 23, scRgb(92, 92, 92));
        send(SCI_STYLESETFORE, 24, scRgb(120, 82, 238));
    }
}

void EditorView::setShowLineNumbers(bool show) {
    _showLineNumbers = show;
    updateLineNumberMarginWidth(true);
}

void EditorView::updateLineNumberMarginWidth(bool force) {
    if (!_showLineNumbers) {
        send(SCI_SETMARGINWIDTHN, 0, 0);
        return;
    }

    auto lineCount = send(SCI_GETLINECOUNT);
    int digits = 1;
    auto temp = lineCount;
    while (temp >= 10) {
        temp /= 10;
        digits++;
    }
    digits = std::max(digits, 4);

    if (!force && digits == _lastLineDigits) {
        return;
    }
    _lastLineDigits = digits;

    std::string testStr(digits, '9');
    auto textWidth = send(SCI_TEXTWIDTH, STYLE_LINENUMBER, reinterpret_cast<sptr_t>(testStr.c_str()));
    // 左右各 4px 安全呼吸空间，使 6位、7位等高位数字绝不被裁切
    auto finalWidth = textWidth + 8;

    send(SCI_SETMARGINWIDTHN, 0, finalWidth);
}

void EditorView::updateEditorChromeStyles() {
    std::string fn = _fontName.isEmpty() ? "Monospace" : _fontName.toUtf8().toStdString();
    int fs = (_fontSize > 0) ? _fontSize : 10;
    sends(SCI_STYLESETFONT, STYLE_LINENUMBER, fn.c_str());
    send(SCI_STYLESETSIZE, STYLE_LINENUMBER, fs);

    if (_isDarkMode) {
        send(SCI_STYLESETBACK, STYLE_DEFAULT, scRgb(28, 28, 28));
        send(SCI_STYLESETFORE, STYLE_DEFAULT, scRgb(218, 218, 218));
        send(SCI_SETCARETLINEBACK, scRgb(37, 34, 48));
        send(SCI_SETCARETFORE, scRgb(168, 130, 255));
        send(SCI_SETSELBACK, 1, scRgb(50, 40, 75));
        send(SCI_STYLESETBACK, STYLE_LINENUMBER, scRgb(35, 35, 35));
        send(SCI_STYLESETFORE, STYLE_LINENUMBER, scRgb(102, 102, 102));
        send(SCI_SETFOLDMARGINCOLOUR, 1, scRgb(40, 40, 40));
        send(SCI_SETFOLDMARGINHICOLOUR, 1, scRgb(40, 40, 40));
        send(SCI_SETMARGINBACKN, 1, scRgb(35, 35, 35));
        send(SCI_STYLESETFORE, STYLE_BRACELIGHT, scRgb(255, 214, 120));
        send(SCI_STYLESETBACK, STYLE_BRACELIGHT, scRgb(78, 64, 38));
        send(SCI_STYLESETBOLD, STYLE_BRACELIGHT, 1);
        send(SCI_STYLESETFORE, STYLE_BRACEBAD, scRgb(255, 110, 110));
        send(SCI_STYLESETBACK, STYLE_BRACEBAD, scRgb(90, 40, 40));
    } else {
        const auto& gs = StyleManager::instance().getGlobalStyles();
        send(SCI_STYLESETBACK, STYLE_DEFAULT, scRgb(255, 255, 255));
        send(SCI_STYLESETFORE, STYLE_DEFAULT, scRgb(34, 34, 34));
        send(SCI_SETCARETLINEBACK, gs.currentLineBg != -1 ? gs.currentLineBg : scRgb(246, 244, 254));
        send(SCI_SETCARETFORE, gs.caretColor != -1 ? gs.caretColor : scRgb(120, 82, 238));
        send(SCI_SETSELBACK, 1, gs.selectedBg != -1 ? gs.selectedBg : scRgb(237, 233, 254));
        if (gs.selectedFg != -1) {
            send(SCI_SETSELFORE, 1, gs.selectedFg);
        }
        send(SCI_STYLESETBACK, STYLE_LINENUMBER, gs.lineMarginBg != -1 ? gs.lineMarginBg : scRgb(250, 250, 250));
        send(SCI_STYLESETFORE, STYLE_LINENUMBER, gs.lineMarginFg != -1 ? gs.lineMarginFg : scRgb(171, 171, 171));
        send(SCI_SETFOLDMARGINCOLOUR, 1, gs.foldMarginBg != -1 ? gs.foldMarginBg : scRgb(246, 246, 246));
        send(SCI_SETFOLDMARGINHICOLOUR, 1, gs.foldMarginBg != -1 ? gs.foldMarginBg : scRgb(246, 246, 246));
        send(SCI_SETMARGINBACKN, 1, gs.lineMarginBg != -1 ? gs.lineMarginBg : scRgb(250, 250, 250));
        send(SCI_STYLESETFORE, STYLE_BRACELIGHT, scRgb(70, 50, 0));
        send(SCI_STYLESETBACK, STYLE_BRACELIGHT, scRgb(255, 232, 163));
        send(SCI_STYLESETBOLD, STYLE_BRACELIGHT, 1);
        send(SCI_STYLESETFORE, STYLE_BRACEBAD, scRgb(160, 30, 30));
        send(SCI_STYLESETBACK, STYLE_BRACEBAD, scRgb(255, 220, 220));
    }
}

void EditorView::initEditorDefaults() {
    // 1. 行号列 (Margin 0: 宽度自适应位数)
    send(SCI_SETMARGINTYPEN, 0, SC_MARGIN_NUMBER);
    send(SCI_SETMARGINSENSITIVEN, 0, 0);

    // 2. 书签标记列 (Margin 1: 仅用于书签，平时空白，按 Ctrl+F2 或点击显示蓝色圆圈)
    const int MARK_BOOKMARK = 24;
    send(SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
    send(SCI_SETMARGINWIDTHN, 1, 14);
    send(SCI_SETMARGINMASKN, 1, (1 << MARK_BOOKMARK));
    send(SCI_SETMARGINSENSITIVEN, 1, 1);

    // 定义书签外观 (上游 经典蓝色圆圈标记)
    send(SCI_MARKERDEFINE, MARK_BOOKMARK, SC_MARK_CIRCLE);
    send(SCI_MARKERSETFORE, MARK_BOOKMARK, scRgb(0, 0, 180));
    send(SCI_MARKERSETBACK, MARK_BOOKMARK, scRgb(0, 165, 255));

    // 3. 代码折叠列 (Margin 2: 上游 经典方块树 Box Tree 风格)
    send(SCI_SETMARGINTYPEN, 2, SC_MARGIN_SYMBOL);
    send(SCI_SETMARGINWIDTHN, 2, 14);
    send(SCI_SETMARGINMASKN, 2, SC_MASK_FOLDERS);
    send(SCI_SETMARGINSENSITIVEN, 2, 1);

    // 定义折叠标记外观 (Box Tree)
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPEN, SC_MARK_BOXMINUS);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDER, SC_MARK_BOXPLUS);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERSUB, SC_MARK_VLINE);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERTAIL, SC_MARK_LCORNER);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEREND, SC_MARK_BOXPLUSCONNECTED);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDEROPENMID, SC_MARK_BOXMINUSCONNECTED);
    send(SCI_MARKERDEFINE, SC_MARKNUM_FOLDERMIDTAIL, SC_MARK_TCORNER);

    for (int m = 25; m <= 31; ++m) {
        send(SCI_MARKERSETFORE, m, scRgb(255, 255, 255));
        send(SCI_MARKERSETBACK, m, scRgb(128, 128, 128));
        send(SCI_MARKERSETBACKSELECTED, m, scRgb(255, 0, 0));
    }

    send(SCI_MARKERENABLEHIGHLIGHT, 1);
    send(SCI_SETAUTOMATICFOLD, SC_AUTOMATICFOLD_CLICK | SC_AUTOMATICFOLD_SHOW);
    send(SCI_SETFOLDFLAGS, SC_FOLDFLAG_LINEAFTER_CONTRACTED);

    // 3. 开启多光标编辑与列块选区 (Multi-Selection & Column Mode)
    send(SCI_SETMULTIPLESELECTION, 1);
    send(SCI_SETADDITIONALSELECTIONTYPING, 1);
    send(SCI_SETADDITIONALCARETSBLINK, 1);
    send(SCI_SETADDITIONALCARETSVISIBLE, 1);
    send(SCI_SETVIRTUALSPACEOPTIONS, SCVS_RECTANGULARSELECTION);

    // 4. 基础字符编码与排版
    send(SCI_SETCODEPAGE, SC_CP_UTF8);
    send(SCI_SETWRAPMODE, SC_WRAP_NONE);
    send(SCI_SETIDLESTYLING, SC_IDLESTYLING_TOVISIBLE);

    send(SCI_SETTABWIDTH, 4);
    send(SCI_SETUSETABS, 0);
    send(SCI_SETINDENT, 4);
    send(SCI_SETCARETLINEVISIBLE, 1);

    // 5. 默认字体
    std::string fn = _fontName.isEmpty() ? "Monospace" : _fontName.toUtf8().toStdString();
    int fs = (_fontSize > 0) ? _fontSize : 10;
    sends(SCI_STYLESETFONT, STYLE_DEFAULT, fn.c_str());
    send(SCI_STYLESETSIZE, STYLE_DEFAULT, fs);
    send(SCI_STYLECLEARALL);
    for (int i = 0; i <= 127; ++i) {
        sends(SCI_STYLESETFONT, i, fn.c_str());
        send(SCI_STYLESETSIZE, i, fs);
    }

    // 6. 高亮匹配指示器（首选项「高亮匹配」使用；8-12 已被格式标记占用）
    auto configureIndicator = [this](int indic, int r, int g, int b) {
        send(SCI_INDICSETSTYLE, indic, INDIC_ROUNDBOX);
        send(SCI_INDICSETFORE, indic, scRgb(r, g, b));
        send(SCI_INDICSETALPHA, indic, 90);
        send(SCI_INDICSETOUTLINEALPHA, indic, 140);
        // 指示器值为 0 时仍被视为“无标记”，显式置 1。
        send(SCI_SETINDICATORCURRENT, indic);
        send(SCI_SETINDICATORVALUE, 1);
    };
    configureIndicator(kBraceHighlightIndicator, 120, 82, 238);
    configureIndicator(kSmartHighlightIndicator, 255, 165, 0);

    // URL 可点击链接指示器：INDIC_PLAIN 画下划线；VALUEFORE 标志让指示器值作为链接文字色；
    // 悬停改用 EXPLORERLINK 并高亮前景，体验与原版一致。
    send(SCI_INDICSETSTYLE, kUrlIndicator, INDIC_PLAIN);
    send(SCI_INDICSETFORE, kUrlIndicator, scRgb(0, 0, 255));       // 链接蓝
    send(SCI_INDICSETALPHA, kUrlIndicator, 70);
    send(SCI_INDICSETFLAGS, kUrlIndicator, SC_INDICFLAG_VALUEFORE);
    send(SCI_INDICSETHOVERSTYLE, kUrlIndicator, INDIC_EXPLORERLINK);
    send(SCI_INDICSETHOVERFORE, kUrlIndicator, scRgb(0, 0, 255));

    // 用指示器绘制匹配括号，避免 STYLE_BRACELIGHT 在深色主题下不可见。
    send(SCI_BRACEHIGHLIGHTINDICATOR, 1, kBraceHighlightIndicator);

    updateEditorChromeStyles();
    updateLineNumberMarginWidth(true);
}

bool EditorView::loadFromFile(const QString& filePath, const std::string& forcedEnc) {
    // 使用 mmap 只读映射，避免整文件一次性堆分配（超大文件峰值内存更友好）。
    int fd = ::open(filePath.toLocal8Bit().constData(), O_RDONLY);
    if (fd < 0) {
        return false;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        ::close(fd);
        return false;
    }
    size_t len = static_cast<size_t>(st.st_size);

    const char* base = nullptr;
    bool mapped = false;
    std::vector<char> heap; // mmap 失败或空文件时的回退缓冲
    if (len > 0) {
        void* m = ::mmap(nullptr, len, PROT_READ, MAP_PRIVATE, fd, 0);
        if (m != MAP_FAILED) {
            base = static_cast<const char*>(m);
            mapped = true;
        }
    }
    if (!mapped) {
        heap.resize(len);
        ssize_t rd = 0;
        size_t off = 0;
        while (off < len) {
            rd = ::read(fd, heap.data() + off, len - off);
            if (rd <= 0) break;
            off += static_cast<size_t>(rd);
        }
        len = off;
        base = heap.data();
    }
    ::close(fd);

    std::string_view raw(base, len);

    // 1. 编码：优先使用调用方指定（“设定编码”重载），否则 uchardet + BOM 自动探测。
    std::string enc = forcedEnc.empty() ? EncodingDetector::detect(raw) : forcedEnc;
    _buffer->setFilePath(filePath.toStdString());
    _buffer->setEncoding(enc);
    _buffer->setBinary(Encoding::looksBinary(base, len, enc));

    // 2. 换行风格（在原始字节上粗略判定；UTF-16 的 EOL 由解码后统一规整）。
    LineEnding le = DocumentBuffer::detectLineEnding(raw);
    _buffer->setLineEnding(le);
    int eolMode = (le == LineEnding::CRLF) ? SC_EOL_CRLF : (le == LineEnding::CR) ? SC_EOL_CR : SC_EOL_LF;
    send(SCI_SETEOLMODE, eolMode);

    // 3. 剥离 BOM，并把磁盘字节按 enc 真正转码为 UTF-8 后分块写入 Scintilla。
    //    这一步修复了历史上 GBK/UTF-16 文件被当作 UTF-8 直接塞入而乱码的问题。
    //    载入期间关闭撤销收集并预分配缓冲，避免膨胀撤销栈与频繁 realloc。
    size_t bom = Encoding::bomLength(base, len);
    send(SCI_SETUNDOCOLLECTION, 0);
    send(SCI_CLEARALL);
    if (len > bom) {
        send(SCI_ALLOCATE, static_cast<uptr_t>(len - bom));
    }
    Encoding::decodeToUtf8Chunks(base + bom, len - bom, enc,
        [this](const char* d, size_t n) {
            if (n) sends(SCI_ADDTEXT, n, d);
        });

    // 解码后的 UTF-8 重新判定并规整换行（UTF-16 源文件的 LF/CR 只有在解码后才可见）。
    // 仅取头部最多 4MB 样本判定，消除大文件多余的全文内存拷贝。
    if (Encoding::isUtf16(enc)) {
        const size_t docLen = static_cast<size_t>(send(SCI_GETLENGTH));
        const size_t probeLen = std::min<size_t>(docLen, 4u << 20);
        std::string sample;
        sample.resize(probeLen);
        send(SCI_GETTEXT, probeLen + 1, reinterpret_cast<sptr_t>(sample.data()));
        LineEnding decodedLe = DocumentBuffer::detectLineEnding(sample);
        _buffer->setLineEnding(decodedLe);
        int dmode = (decodedLe == LineEnding::CRLF) ? SC_EOL_CRLF : (decodedLe == LineEnding::CR) ? SC_EOL_CR : SC_EOL_LF;
        send(SCI_SETEOLMODE, dmode);
    }
    // 经典 Mac(CR) 统一规整为内部一致的 CR EOL，保证保存往返一致。
    if (_buffer->getLineEnding() == LineEnding::CR) {
        send(SCI_CONVERTEOLS, SC_EOL_CR);
    }

    send(SCI_SETUNDOCOLLECTION, 1);
    send(SCI_EMPTYUNDOBUFFER);

    if (mapped) {
        ::munmap(const_cast<char*>(base), static_cast<size_t>(st.st_size));
    }

    send(SCI_SETSAVEPOINT);
    send(SCI_GOTOPOS, 0);
    updateLineNumberMarginWidth();

    QFileInfo fi(filePath);
    std::string ext = fi.suffix().toStdString();
    std::string lang = StyleManager::instance().detectLanguageByExt(ext);
    applyLanguage(lang);

    return true;
}

bool EditorView::reloadWithEncoding(const std::string& enc) {
    QString path = QString::fromStdString(_buffer->getFilePath());
    if (path.isEmpty()) return false;
    return loadFromFile(path, enc);
}

void EditorView::applyDefaultDocFormat(LineEnding le, const std::string& enc) {
    int eol = (le == LineEnding::CRLF) ? SC_EOL_CRLF : (le == LineEnding::CR) ? SC_EOL_CR : SC_EOL_LF;
    send(SCI_SETEOLMODE, eol);
    _buffer->setLineEnding(le);
    if (!enc.empty()) _buffer->setEncoding(enc);
}

std::string EditorView::currentTextUtf8() const {
    auto len = send(SCI_GETLENGTH);
    if (len <= 0) return std::string();
    std::string buf;
    buf.resize(static_cast<size_t>(len) + 1);
    send(SCI_GETTEXT, len + 1, reinterpret_cast<sptr_t>(buf.data()));
    buf.resize(static_cast<size_t>(len)); // 去掉结尾 NUL
    return buf;
}

std::string EditorView::selectedTextUtf8() const {
    auto* self = const_cast<EditorView*>(this);
    const sptr_t start = self->send(SCI_GETSELECTIONSTART);
    const sptr_t end = self->send(SCI_GETSELECTIONEND);
    if (end <= start) return currentTextUtf8();
    const sptr_t textLength = self->send(SCI_GETSELTEXT, 0, 0);
    if (textLength <= 0) return std::string();
    std::string selected(static_cast<size_t>(textLength) + 1, '\0');
    self->send(SCI_GETSELTEXT, static_cast<uptr_t>(textLength + 1),
               reinterpret_cast<sptr_t>(selected.data()));
    selected.resize(static_cast<size_t>(textLength));
    return selected;
}

std::string_view EditorView::currentTextView() const {
    auto* self = const_cast<EditorView*>(this);
    const sptr_t len = self->send(SCI_GETLENGTH);
    if (len <= 0) return std::string_view();
    const char* ptr = reinterpret_cast<const char*>(
        self->send(SCI_GETRANGEPOINTER, 0, static_cast<sptr_t>(len)));
    if (!ptr) return std::string_view();
    return std::string_view(ptr, static_cast<size_t>(len));
}

void EditorView::setTextFromUtf8(const std::string& utf8) {
    // 恢复出来的内容属于“未保存的修改”，不能进入撤销栈：否则一次 Ctrl+Z 就回到空文档，
    // 看起来像内容全丢了。清栈后把 savepoint 停在当前内容上，
    // 由调用方显式把缓冲区标成已修改（对应上游 buf->setDirty(true)）。
    send(SCI_SETUNDOCOLLECTION, 0);
    sends(SCI_SETTEXT, 0, utf8.c_str());
    send(SCI_SETUNDOCOLLECTION, 1);
    send(SCI_EMPTYUNDOBUFFER);
    send(SCI_SETSAVEPOINT);
    updateLineNumberMarginWidth();
}

void EditorView::restoreViewState(int caretPos, int selStart, int selEnd,
                                  int firstDocLine, int xOffset) {
    const sptr_t len = send(SCI_GETLENGTH);
    // session.xml 里的位置来自上一次运行，文件可能已被外部改小，必须先夹到合法区间。
    auto clampPos = [len](int pos) -> sptr_t {
        if (pos < 0) return -1;
        return (pos > len) ? len : static_cast<sptr_t>(pos);
    };

    const sptr_t caret = clampPos(caretPos);
    const sptr_t selFrom = clampPos(selStart);
    const sptr_t selTo = clampPos(selEnd);

    // 关键：用 SETANCHOR/SETCURRENTPOS 而不是 GOTOPOS/SETSEL。
    // GOTOPOS 以及 SCI_SCROLLCARET 都会“把光标带进视野”，从而重算横向/纵向滚动位置，
    // 把下面刚还原的 xOffset 和 firstVisibleLine 覆盖掉 —— 表现就是重开程序后
    // 横向滚动条停在中间，一行只看得见后半截（上游 restoreCurrentPosPreStep 同样避开它们）。
    if (selFrom >= 0 && selTo >= 0 && selFrom != selTo) {
        send(SCI_SETANCHOR, static_cast<uptr_t>(selFrom));
        send(SCI_SETCURRENTPOS, selTo);
    } else {
        const sptr_t pos = caret < 0 ? 0 : caret;
        send(SCI_SETANCHOR, static_cast<uptr_t>(pos));
        send(SCI_SETCURRENTPOS, pos);
    }
    send(SCI_CANCEL); // 撤掉尚未执行的滚动/重绘

    // 横向偏移只在关闭自动换行时有意义：开了换行就没有水平滚动条，
    // 这时强行 SETXOFFSET 会在切回不换行时留下一个莫名其妙的偏移。
    if (send(SCI_GETWRAPMODE) == SC_WRAP_NONE) {
        send(SCI_SETXOFFSET, static_cast<uptr_t>(qMax(0, xOffset)));
    }
    send(SCI_CHOOSECARETX);

    // session 里存的是“文档行号”，折行开启时要换算成可见行号才能定位到正确的屏幕行。
    if (firstDocLine > 0) {
        const auto visible = send(SCI_VISIBLEFROMDOCLINE, static_cast<uptr_t>(firstDocLine));
        send(SCI_SETFIRSTVISIBLELINE, static_cast<uptr_t>(visible));
    }
}

// 格式化只动空白、非空白字符序列不变：统计 before 中 pos 之前的非空白字符数，
// 再在 after 里定位到“同样多个非空白字符之后”的字节处，即可把光标/视口的逻辑落点映射回去。
static sptr_t mapPosByNonWhitespace(const std::string& before, sptr_t pos, const std::string& after) {
    if (pos <= 0) return 0;
    size_t target = 0;
    const size_t limit = qMin(static_cast<size_t>(pos), before.size());
    for (size_t i = 0; i < limit; ++i)
        if (!std::isspace(static_cast<unsigned char>(before[i]))) ++target;
    size_t count = 0;
    for (size_t j = 0; j < after.size(); ++j) {
        if (count == target) return static_cast<sptr_t>(j);
        if (!std::isspace(static_cast<unsigned char>(after[j]))) ++count;
    }
    return static_cast<sptr_t>(after.size());
}

void EditorView::applyFormattedText(const std::string& formattedUtf8) {
    const std::string before = currentTextUtf8();
    if (before == formattedUtf8) return;

    const sptr_t caret = send(SCI_GETCURRENTPOS);
    const sptr_t firstLine = send(SCI_GETFIRSTVISIBLELINE);
    const sptr_t firstLinePos = send(SCI_POSITIONFROMLINE, static_cast<uptr_t>(firstLine));
    const int xOffset = static_cast<int>(send(SCI_GETXOFFSET));
    const bool wrapped = send(SCI_GETWRAPMODE) != SC_WRAP_NONE;

    const sptr_t newCaret = mapPosByNonWhitespace(before, caret, formattedUtf8);
    const sptr_t newFirstPos = mapPosByNonWhitespace(before, firstLinePos, formattedUtf8);

    // 整篇替换包在一个撤销动作里：一次 Ctrl+Z 即可回到格式化前。
    send(SCI_BEGINUNDOACTION);
    sends(SCI_SETTEXT, 0, formattedUtf8.c_str());
    send(SCI_ENDUNDOACTION);

    // 同 restoreViewState：用 SETANCHOR/SETCURRENTPOS + CANCEL，避开 GOTOPOS/SCROLLCARET
    // 顺带重算滚动，把下面刚还原的视口再次覆盖掉。
    send(SCI_SETANCHOR, static_cast<uptr_t>(newCaret));
    send(SCI_SETCURRENTPOS, newCaret);
    send(SCI_CANCEL);

    if (!wrapped && xOffset > 0) send(SCI_SETXOFFSET, static_cast<uptr_t>(xOffset));
    const sptr_t newFirstLine = send(SCI_LINEFROMPOSITION, static_cast<uptr_t>(newFirstPos));
    send(SCI_SETFIRSTVISIBLELINE, static_cast<uptr_t>(newFirstLine));

    updateLineNumberMarginWidth();
}

QByteArray EditorView::documentBytes() const {
    const sptr_t len = const_cast<EditorView*>(this)->send(SCI_GETLENGTH);
    if (len <= 0) return QByteArray();
    const char* ptr = reinterpret_cast<const char*>(
        const_cast<EditorView*>(this)->send(SCI_GETRANGEPOINTER, 0, len));
    if (ptr) {
        return QByteArray(ptr, static_cast<int>(len));
    }
    QByteArray buf;
    buf.resize(static_cast<int>(len));
    const_cast<EditorView*>(this)->sends(SCI_GETTEXT, len + 1, buf.data());
    return buf;
}

bool EditorView::writeEncodedFile(const QString& path, const std::string& utf8,
                                  const std::string& enc) {
    _lastSaveError.clear();

    // QSaveFile 先写同目录临时文件、commit() 时原子替换。转码/写盘失败都会丢掉临时文件，
    // 既不会截断已存在的原文件，也不会留下半个文件 —— 这是「不丢数据」的底线。
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        _lastSaveError = QStringLiteral("无法写入 %1：%2").arg(path, file.errorString());
        return false;
    }

    const std::string bom = Encoding::bomForSave(enc);
    if (!bom.empty()
        && file.write(bom.data(), static_cast<qint64>(bom.size())) != static_cast<qint64>(bom.size())) {
        _lastSaveError = QStringLiteral("写入 BOM 失败：%1").arg(file.errorString());
        file.cancelWriting();
        return false;
    }

    bool writeOk = true;
    const bool convOk = Encoding::encodeFromUtf8Chunks(utf8.data(), utf8.size(), enc,
        [&file, &writeOk](const char* d, std::size_t n) {
            if (!writeOk) return;
            writeOk = file.write(d, static_cast<qint64>(n)) == static_cast<qint64>(n);
        });
    if (!convOk) {
        _lastSaveError = QStringLiteral("不支持的编码「%1」，未写入任何内容")
                             .arg(QString::fromStdString(enc));
        file.cancelWriting();
        return false;
    }
    if (!writeOk) {
        _lastSaveError = QStringLiteral("写盘失败：%1").arg(file.errorString());
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        _lastSaveError = QStringLiteral("提交文件失败：%1").arg(file.errorString());
        return false;
    }
    return true;
}

bool EditorView::saveToFile(const QString& savePath) {
    const QString targetPath = savePath.isEmpty()
        ? QString::fromStdString(_buffer->getFilePath()) : savePath;
    if (targetPath.isEmpty()) {
        _lastSaveError = QStringLiteral("未指定保存路径");
        return false;
    }

    const std::string enc = _buffer->getEncoding();
    if (!writeEncodedFile(targetPath, currentTextUtf8(), enc)) {
        // 失败时绝不 SETSAVEPOINT，文档保持「未保存」，用户可换编码重试。
        return false;
    }

    send(SCI_SETSAVEPOINT);
    _buffer->setFilePath(targetPath.toStdString());
    _buffer->setModified(false);
    _buffer->setBackupDirty(false);
    emit documentModified(false);
    return true;
}

bool EditorView::saveCopyToFile(const QString& filePath) {
    if (filePath.isEmpty()) {
        _lastSaveError = QStringLiteral("未指定拷贝目标路径");
        return false;
    }
    // 与保存共用事务写盘链路（BOM + 真实转码），但绝不触碰 filePath / savepoint /
    // modified / 文件监控；非 UTF-8 文档得到的才是正确编码的副本。
    return writeEncodedFile(filePath, currentTextUtf8(), _buffer->getEncoding());
}

void EditorView::applyLanguage(const std::string& langName) {
    _currentLang = langName;
    std::string lexerName = StyleManager::instance().getLexerName(langName);

    // 1. 重置默认样式并清除所有旧样式定义
    std::string fn = _fontName.isEmpty() ? "Monospace" : _fontName.toUtf8().toStdString();
    int fs = (_fontSize > 0) ? _fontSize : 10;
    sends(SCI_STYLESETFONT, STYLE_DEFAULT, fn.c_str());
    send(SCI_STYLESETSIZE, STYLE_DEFAULT, fs);
    send(SCI_STYLECLEARALL);

    // 2. 挂载或卸载词法分析器
    if (lexerName != "null") {
        void* pLexer = CreateLexer(lexerName.c_str());
        if (!pLexer) {
            // CreateLexer 只按名字精确匹配 Lexilla 目录。传 nullptr 给 SCI_SETILEXER 会
            // 静默退化成「识别出语言但全文不着色」，正是 .ts 之前的症状，必须显式诊断。
            qWarning("EditorView: 未找到词法分析器 '%s'（语言 '%s'），该文档将按纯文本显示",
                     lexerName.c_str(), langName.c_str());
            send(SCI_SETILEXER, 0, 0);
        } else {
            send(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(pLexer));
        }
    } else {
        send(SCI_SETILEXER, 0, 0);
    }

    // 3. 应用全局部件外观（行号、选区、折叠列等，严格跟随当前深浅色模式）
    updateEditorChromeStyles();
    updateLineNumberMarginWidth(true);

    // 4. 清除现有关键字槽位残留
    for (int k = 0; k <= 8; ++k) {
        sends(SCI_SETKEYWORDS, k, "");
    }

    // 5. 统一应用当前字体和字号到所有语法高亮样式槽位 (0 ~ 127)
    for (int i = 0; i <= 127; ++i) {
        sends(SCI_STYLESETFONT, i, fn.c_str());
        send(SCI_STYLESETSIZE, i, fs);
    }

    // 6. 应用对应语言的主题高亮颜色
    if (langName == "markdown") {
        setupMarkdownStyles(_isDarkMode);
    } else {
        applyThemeStyles(langName);
    }

    const auto* langDef = StyleManager::instance().getLanguage(langName);
    if (langDef) {
        if (lexerName == "cpp") {
            // 对齐 上游 Cpp/Java/JavaScript 词法器关键字规范
            // slot 0: instre1 (指令关键字: if, else, return, new, import, package...)
            if (langDef->rawKeywords.count("instre1")) {
                sends(SCI_SETKEYWORDS, 0, langDef->rawKeywords.at("instre1").c_str());
            }
            // slot 1: type1 (类型关键字: public, private, protected, class, interface, void, int, boolean...)
            if (langDef->rawKeywords.count("type1")) {
                sends(SCI_SETKEYWORDS, 1, langDef->rawKeywords.at("type1").c_str());
            }
            // slot 2: type2 (Doxygen / Javadoc 关键字)
            if (langDef->rawKeywords.count("type2")) {
                sends(SCI_SETKEYWORDS, 2, langDef->rawKeywords.at("type2").c_str());
            }
            // slot 3: instre2 (全局类 / 指令)
            if (langDef->rawKeywords.count("instre2")) {
                sends(SCI_SETKEYWORDS, 3, langDef->rawKeywords.at("instre2").c_str());
            }
        } else if (lexerName == "hypertext") {
            // HTML 词法器关键字规范:
            // slot 0: HTML tag 关键字 (instre1: a, div, span, title, script, body...)
            if (langDef->rawKeywords.count("instre1")) {
                sends(SCI_SETKEYWORDS, 0, langDef->rawKeywords.at("instre1").c_str());
            }
            // slot 1: JavaScript 关键字 (嵌入在 HTML 中的 JS)
            const auto* jsLangDef = StyleManager::instance().getLanguage("javascript");
            if (jsLangDef && jsLangDef->rawKeywords.count("instre1")) {
                sends(SCI_SETKEYWORDS, 1, jsLangDef->rawKeywords.at("instre1").c_str());
            }
            // slot 5: SGML / DOCTYPE (instre2)
            if (langDef->rawKeywords.count("instre2")) {
                sends(SCI_SETKEYWORDS, 5, langDef->rawKeywords.at("instre2").c_str());
            }
        } else {
            // 通用语言槽位映射
            if (langDef->rawKeywords.count("instre1")) sends(SCI_SETKEYWORDS, 0, langDef->rawKeywords.at("instre1").c_str());
            if (langDef->rawKeywords.count("instre2")) sends(SCI_SETKEYWORDS, 1, langDef->rawKeywords.at("instre2").c_str());
            if (langDef->rawKeywords.count("type1"))   sends(SCI_SETKEYWORDS, 2, langDef->rawKeywords.at("type1").c_str());
            if (langDef->rawKeywords.count("type2"))   sends(SCI_SETKEYWORDS, 3, langDef->rawKeywords.at("type2").c_str());
        }
    }

    if (lexerName == "hypertext") {
        // 内嵌 JS 样式必须同时注入 (stylers.model.xml 中的 javascript embedded 样式)
        applyThemeStyles("javascript");
    }

    // 词法器属性是文档级的，而 TypeScript 与 C/C++/Java 共用 cpp 词法器：每次切换语言
    // 先复位成 LexCPP 默认值（track=1 / backquoted=0），避免上一种语言的设置泄漏。
    auto setLexerProperty = [this](const char* key, const char* value) {
        send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>(key),
             reinterpret_cast<sptr_t>(value));
    };
    setLexerProperty("lexer.cpp.track.preprocessor", "1");
    setLexerProperty("lexer.cpp.backquoted.strings", "0");
    if (langName == "typescript") {
        // 对齐上游 ScintillaEditView::setTypeScriptLexer()：TS 的 # 是私有字段而非预处理
        // 指令，必须关掉跟踪；反引号模板字符串 `...` 也要开启才会被识别成字符串。
        setLexerProperty("lexer.cpp.track.preprocessor", "0");
        setLexerProperty("lexer.cpp.backquoted.strings", "1");
    }

    // 启用代码折叠支持
    send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold"), reinterpret_cast<sptr_t>("1"));
    send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold.html"), reinterpret_cast<sptr_t>("1"));
    send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold.hypertext.comment"), reinterpret_cast<sptr_t>("1"));
    send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold.comment"), reinterpret_cast<sptr_t>("1"));

    // 7. 核心：重置整篇文档已有字符的样式字节，并重新染色
    // 大文件（> 2MB）避免同步 SCI_COLOURISE(0, -1) 阻塞主线程，依靠 SC_IDLESTYLING_TOVISIBLE 自动视口/空闲着色
    send(SCI_CLEARDOCUMENTSTYLE);
    const auto docLen = send(SCI_GETLENGTH);
    if (docLen > 2 * 1024 * 1024) {
        send(SCI_COLOURISE, 0, 65536);
    } else {
        send(SCI_COLOURISE, 0, -1);
    }
    emit languageChanged(QString::fromStdString(langName));
}

void EditorView::setEditorFont(const QString& fontName, int fontSize) {
    if (!fontName.isEmpty()) {
        _fontName = fontName;
    }
    if (fontSize > 0) {
        _fontSize = fontSize;
    }

    std::string fn = _fontName.toUtf8().toStdString();
    sends(SCI_STYLESETFONT, STYLE_DEFAULT, fn.c_str());
    send(SCI_STYLESETSIZE, STYLE_DEFAULT, _fontSize);

    for (int i = 0; i <= 127; ++i) {
        sends(SCI_STYLESETFONT, i, fn.c_str());
        send(SCI_STYLESETSIZE, i, _fontSize);
    }

    if (_currentLang == "markdown") {
        setupMarkdownStyles(_isDarkMode);
    }

    updateLineNumberMarginWidth(true);
}

void EditorView::applyThemeStyles(const std::string& langName) {
    const auto* styles = StyleManager::instance().getStyles(langName);
    if (!styles) {
        styles = StyleManager::instance().getStyles("normal");
    }
    if (!styles) return;

    // 官方主题明确给了背景色时，即使处于深色模式也照主题绘制；否则深色模式会用
    // 深色默认背景覆盖掉 Monokai 等主题的 #272822。
    const bool themed = StyleManager::instance().hasThemeStyle(langName);
    for (const auto& s : *styles) {
        if (s.fgColor != -1) {
            if (_isDarkMode && !themed && s.fgColor == 0x000000) {
                send(SCI_STYLESETFORE, s.styleID, scRgb(212, 212, 212));
            } else {
                send(SCI_STYLESETFORE, s.styleID, s.fgColor);
            }
        }
        if ((!_isDarkMode || themed) && s.bgColor != -1) {
            send(SCI_STYLESETBACK, s.styleID, s.bgColor);
        }
        send(SCI_STYLESETBOLD, s.styleID, s.bold ? 1 : 0);
        send(SCI_STYLESETITALIC, s.styleID, s.italic ? 1 : 0);
        // 样式配置器可按槽位单独指定字体族/字号；空值表示沿用全局编辑器字体。
        // 这里排在 applyLanguage()/setEditorFont() 之后执行，才不会被全局字体重置冲掉。
        if (!s.fontName.empty())
            sends(SCI_STYLESETFONT, s.styleID, s.fontName.c_str());
        if (s.fontSize > 0)
            send(SCI_STYLESETSIZE, s.styleID, s.fontSize);
    }

    if (_isDarkMode) {
        // 主题覆盖了当前语言时，默认背景交给主题样式，不再强行刷成窗口深色，
        // 否则主题的 #272822 会被 #1C1C1C 覆盖。
        if (!themed) {
            send(SCI_STYLESETBACK, STYLE_DEFAULT, scRgb(28, 28, 28));
            send(SCI_STYLESETFORE, STYLE_DEFAULT, scRgb(218, 218, 218));
        }
        send(SCI_SETCARETLINEBACK, scRgb(37, 34, 48));
        send(SCI_SETCARETFORE, scRgb(168, 130, 255));
    } else {
        send(SCI_STYLESETBACK, STYLE_DEFAULT, scRgb(255, 255, 255));
        send(SCI_STYLESETFORE, STYLE_DEFAULT, scRgb(34, 34, 34));
        send(SCI_SETCARETLINEBACK, scRgb(246, 244, 254));
        send(SCI_SETCARETFORE, scRgb(120, 82, 238));
    }
}

void EditorView::setDarkMode(bool dark) {
    _isDarkMode = dark;
    updateEditorChromeStyles();
    updateLineNumberMarginWidth(true);

    if (_currentLang == "markdown") {
        setupMarkdownStyles(dark);
        send(SCI_COLOURISE, 0, -1);
    } else {
        applyThemeStyles(_currentLang);
    }
}

void EditorView::duplicateCurrentLine() {
    send(SCI_LINEDUPLICATE);
}

void EditorView::cutCurrentLine() {
    send(SCI_LINECUT);
}

void EditorView::moveLineUp() {
    send(SCI_MOVESELECTEDLINESUP);
}

void EditorView::moveLineDown() {
    send(SCI_MOVESELECTEDLINESDOWN);
}

void EditorView::goToLine(int line) {
    if (line < 1) line = 1;
    auto pos = send(SCI_POSITIONFROMLINE, line - 1);
    send(SCI_GOTOPOS, pos);
    send(SCI_SCROLLCARET);
}

// ---------------------------------------------------------------------------
// 列选模式（矩形选区）与列编辑器
// 语义照搬 上游：列选区在 Scintilla 内部是“每行一个 selection”，
// 列编辑器逐行把选区区间换成要写入的内容，写完再把选区拉回到新内容上，
// 这样连续按几次 Alt+C 可以接着上次的列位置继续填。
// ---------------------------------------------------------------------------

namespace {

// 数字在指定进制下占几位（用于对齐宽度）。
int digitsInBase(qint64 number, int base) {
    if (number < 0) number = -number;
    int digits = 1;
    while (number >= base) {
        number /= base;
        ++digits;
    }
    return digits;
}

QString formatNumberInBase(qint64 number, int base, bool hexUpper, int width, ColumnPad pad) {
    QString s = QString::number(number, base); // Qt 内建支持 2..36 进制，字母默认小写
    if (hexUpper && base == 16) s = s.toUpper();
    const int padLen = width - s.size();
    if (padLen <= 0) return s;
    switch (pad) {
    case ColumnPad::zeros:  return QString(padLen, QLatin1Char('0')) + s; // 前置补零，右对齐
    case ColumnPad::spaces: return QString(padLen, QLatin1Char(' ')) + s; // 前置补空格，右对齐
    case ColumnPad::none:   return s + QString(padLen, QLatin1Char(' ')); // 不补齐：数字后补空格凑满宽度
    }
    return s;
}

} // namespace

void EditorView::beginOrEndSelect(bool columnMode) {
    const sptr_t cur = send(SCI_GETCURRENTPOS);
    if (_beginSelectPos < 0) {
        // 第一次调用：把当前光标位置记为列块的起点，选区仍保持为空。
        _beginSelectPos = cur;
        return;
    }
    // 第二次调用：先切选取模式，再把锚点拉回起点，光标留在现在的位置，
    // Scintilla 会自动把这两点之间的矩形展开成每行一个选区。
    send(SCI_CHANGESELECTIONMODE, columnMode ? SC_SEL_RECTANGLE : SC_SEL_STREAM);
    if (columnMode) {
        send(SCI_SETANCHOR, _beginSelectPos);
    } else {
        send(SCI_SETSEL, _beginSelectPos, cur);
    }
    _beginSelectPos = -1;
}

QVector<EditorView::ColumnLine> EditorView::collectColumnLines() const {
    QVector<ColumnLine> lines;
    // 列选区至少跨两行才会被展开成多个 selection，单个 selection 不是列块。
    const sptr_t nbSel = send(SCI_GETSELECTIONS);
    if (nbSel <= 1) return lines;

    const bool rect = send(SCI_SELECTIONISRECTANGLE) != 0;
    lines.reserve(static_cast<int>(nbSel));
    for (sptr_t i = 0; i < nbSel; ++i) {
        const sptr_t anchor = send(SCI_GETSELECTIONNANCHOR, i);
        const sptr_t caret = send(SCI_GETSELECTIONNCARET, i);
        const sptr_t vsAnchor = send(SCI_GETSELECTIONNANCHORVIRTUALSPACE, i);
        const sptr_t vsCaret = send(SCI_GETSELECTIONNCARETVIRTUALSPACE, i);

        ColumnLine cl;
        cl.order = static_cast<int>(i);
        cl.virtualAnchor = vsAnchor;
        cl.virtualCaret = vsCaret;
        if (anchor == caret && rect) {
            // 宽度为 0 的列块（只在某一列放了个光标），方向靠虚拟空格大小判断。
            cl.selLpos = anchor;
            cl.selRpos = caret;
            cl.l2r = vsAnchor < vsCaret;
        } else if (anchor > caret) {
            cl.selLpos = caret;
            cl.selRpos = anchor;
            cl.l2r = false;
        } else {
            cl.selLpos = anchor;
            cl.selRpos = caret;
        }
        lines.push_back(cl);
    }
    return lines;
}

void EditorView::applyColumnPayload(QVector<ColumnLine>& lines, const ColumnPayload& payload) const {
    // 改写一行会让后面所有行的字节位置整体偏移，所以用 totalDiff 累计修正。
    sptr_t totalDiff = 0;
    for (int i = 0; i < lines.size(); ++i) {
        ColumnLine& cl = lines[i];
        if (!cl.isValid()) continue;

        const QByteArray utf8 = payload(i).toUtf8();
        const sptr_t diff = utf8.size() - (cl.selRpos - cl.selLpos);

        cl.selLpos += totalDiff;
        cl.selRpos += totalDiff;

        const bool hasVirtualSpc = cl.virtualAnchor > 0;
        if (hasVirtualSpc) {
            // 选区伸到了行尾之外（只有虚拟空格没有实字符），先按行尾对齐补成真空格，
            // 否则 target 区间会超出行尾而无法替换。
            // 性能优化：直接构造指定长度的空格串一次性插入，消除原逐字符循环调用的开销
            if (cl.virtualCaret > 0) {
                std::string spc(static_cast<size_t>(cl.virtualCaret), ' ');
                sends(SCI_INSERTTEXT, cl.selLpos, spc.c_str());
            }
            cl.selLpos += cl.virtualAnchor;
            cl.selRpos += cl.virtualCaret;
        }

        send(SCI_SETTARGETRANGE, cl.selLpos, cl.selRpos);
        sends(SCI_REPLACETARGET, utf8.size(), utf8.constData());

        if (hasVirtualSpc) {
            totalDiff += cl.virtualAnchor + utf8.size();
            cl.virtualAnchor = 0;
            cl.virtualCaret = 0; // 已经补成真实空格，还原选区时不再需要虚拟空间
        } else {
            totalDiff += diff;
        }
        cl.selRpos += diff;
    }
}

void EditorView::restoreColumnSelections(const QVector<ColumnLine>& lines) const {
    for (const ColumnLine& cl : lines) {
        if (!cl.isValid()) continue;
        const sptr_t start = cl.l2r ? cl.selLpos : cl.selRpos;
        const sptr_t end = cl.l2r ? cl.selRpos : cl.selLpos;
        send(SCI_SETSELECTIONNSTART, cl.order, start);
        send(SCI_SETSELECTIONNEND, cl.order, end);
        if (cl.virtualAnchor) send(SCI_SETSELECTIONNANCHORVIRTUALSPACE, cl.order, cl.virtualAnchor);
        if (cl.virtualCaret) send(SCI_SETSELECTIONNCARETVIRTUALSPACE, cl.order, cl.virtualCaret);
    }

    // 虚拟空格被补成真实空格后，那一行各区间的位置整体右移，Scintilla 有时会把
    // 多行选区收回成一个（主选区的起止一动就会重置附加选区）。收回去的后果是
    // 下一次再开列编辑器时会走“写到文档结尾”的退化路径，很危险，
    // 所以这里用矩形两个角点把它重新建起来。
    // 角点不能取 lines.front()/back()：lines 是按 selection 索引排的，而 Scintilla 的
    // 索引 0 是主选区所在行，从下往上拖动时它并不是最上面一行，首尾会颠倒。
    if (lines.size() > 1 && send(SCI_GETSELECTIONS) != static_cast<sptr_t>(lines.size())) {
        const ColumnLine* topLeft = &lines.front();
        const ColumnLine* bottomRight = &lines.front();
        for (const ColumnLine& cl : lines) {
            if (!cl.isValid()) continue;
            if (cl.selLpos < topLeft->selLpos) topLeft = &cl;
            if (cl.selRpos > bottomRight->selRpos) bottomRight = &cl;
        }
        send(SCI_SETRECTANGULARSELECTIONANCHOR, topLeft->selLpos);
        send(SCI_SETRECTANGULARSELECTIONANCHORVIRTUALSPACE, topLeft->virtualAnchor);
        send(SCI_SETRECTANGULARSELECTIONCARET, bottomRight->selRpos);
        send(SCI_SETRECTANGULARSELECTIONCARETVIRTUALSPACE, bottomRight->virtualCaret);
    }
}

void EditorView::writeColumnPayloadToDocEnd(const ColumnPayload& payload) const {
    const sptr_t cursorPos = send(SCI_GETCURRENTPOS);
    const sptr_t cursorCol = send(SCI_GETCOLUMN, cursorPos);
    const sptr_t cursorLine = send(SCI_LINEFROMPOSITION, cursorPos);
    const sptr_t lastLine = send(SCI_LINEFROMPOSITION, send(SCI_GETLENGTH));

    for (sptr_t line = cursorLine; line <= lastLine; ++line) {
        const sptr_t lineBegin = send(SCI_POSITIONFROMLINE, line);
        const sptr_t lineEnd = send(SCI_GETLINEENDPOSITION, line);
        const sptr_t lineLen = lineEnd - lineBegin;

        QByteArray text(lineLen + 3, '\0');
        const sptr_t copied = send(SCI_GETLINE, line, reinterpret_cast<sptr_t>(text.data()));
        text.truncate(qMin<sptr_t>(copied, lineLen)); // 去掉 GETLINE 附带的 EOL 与结尾 NUL

        const QByteArray ins = payload(static_cast<int>(line - cursorLine)).toUtf8();
        const sptr_t lineEndCol = send(SCI_GETCOLUMN, lineEnd);
        if (lineEndCol < cursorCol) {
            // 该行比目标列短：先补空格对齐到目标列，再接上要写的内容。
            text += QByteArray(static_cast<int>(cursorCol - lineEndCol), ' ') + ins;
        } else {
            // FINDCOLUMN 返回的是字节位置（而 cursorCol 是按字符计的列），
            // 所以一直在字节层面拼接，CJK 文本也不会错位。
            sptr_t rel = send(SCI_FINDCOLUMN, line, cursorCol) - lineBegin;
            rel = qMin(rel, static_cast<sptr_t>(text.size()));
            text.insert(static_cast<int>(rel), ins);
        }
        send(SCI_SETTARGETRANGE, lineBegin, lineEnd);
        sends(SCI_REPLACETARGET, text.size(), text.constData());
    }
}

void EditorView::columnInsertText(const QString& text) {
    // 文本模式：列选区里每一行都写入同一段内容（整段覆盖所选的列宽）。
    send(SCI_BEGINUNDOACTION);
    auto lines = collectColumnLines();
    if (!lines.isEmpty()) {
        // 从前往后改写才不会让字节位置失效；还原选区时要按原序号回填。
        std::sort(lines.begin(), lines.end(),
                  [](const ColumnLine& a, const ColumnLine& b) { return a.selLpos < b.selLpos; });
        applyColumnPayload(lines, [&](int) { return text; });
        std::sort(lines.begin(), lines.end(),
                  [](const ColumnLine& a, const ColumnLine& b) { return a.order < b.order; });
        restoreColumnSelections(lines);
    } else {
        writeColumnPayloadToDocEnd([&](int) { return text; });
    }
    send(SCI_ENDUNDOACTION);
}

void EditorView::columnInsertNumber(qint64 initial, qint64 increment, int repeatTimes, int base,
                                   bool hexUpper, ColumnPad pad) {
    // repeat 至少为 1，否则下面生成序列的循环永不调数（同 上游 的保护）。
    repeatTimes = qMax(1, repeatTimes);

    // 先取列选区：有列块时“一行一个选区”，没有时退化为“光标行到文档结尾”，
    // 两种情况下行数算法不同，必须先定下来才能算出需要多少个数字。
    auto lines = collectColumnLines();
    const sptr_t lastLine = send(SCI_LINEFROMPOSITION, send(SCI_GETLENGTH));
    const sptr_t cursorLine = send(SCI_LINEFROMPOSITION, send(SCI_GETCURRENTPOS));
    const int lineCount = lines.isEmpty()
        ? static_cast<int>(lastLine - cursorLine + 1)
        : lines.size();
    if (lineCount <= 0) return;

    // 同一个数字连续占 repeatTimes 行，再按 increment 递增。
    QVector<qint64> numbers;
    numbers.reserve(lineCount);
    qint64 cur = initial;
    while (numbers.size() < lineCount) {
        for (int i = 0; i < repeatTimes; ++i) {
            numbers.append(cur);
            if (numbers.size() >= lineCount) break;
        }
        cur += increment;
    }

    // 对齐宽度取首尾两个数字位数的较大值，列编辑器才能把整列排齐。
    const int width = qMax(digitsInBase(initial, base), digitsInBase(numbers.back(), base));

    send(SCI_BEGINUNDOACTION);
    const auto payload = [&](int idx) {
        const int clamped = qBound(0, idx, static_cast<int>(numbers.size()) - 1);
        return formatNumberInBase(numbers.at(clamped), base, hexUpper, width, pad);
    };
    if (!lines.isEmpty()) {
        // 从前往后改写才不会让字节位置失效；还原选区时要按原序号回填。
        std::sort(lines.begin(), lines.end(),
                  [](const ColumnLine& a, const ColumnLine& b) { return a.selLpos < b.selLpos; });
        applyColumnPayload(lines, payload);
        std::sort(lines.begin(), lines.end(),
                  [](const ColumnLine& a, const ColumnLine& b) { return a.order < b.order; });
        restoreColumnSelections(lines);
    } else {
        writeColumnPayloadToDocEnd(payload);
    }
    send(SCI_ENDUNDOACTION);
}

void EditorView::convertLineEndings(LineEnding targetLe) {
    int eol = (targetLe == LineEnding::CRLF) ? SC_EOL_CRLF
              : (targetLe == LineEnding::CR) ? SC_EOL_CR
              : SC_EOL_LF;
    send(SCI_CONVERTEOLS, eol);
    send(SCI_SETEOLMODE, eol);
    _buffer->setLineEnding(targetLe);
    emit lineEndingChanged(targetLe);
}

void EditorView::toggleBookmark() {
    const int MARK_BOOKMARK = 24;
    auto pos = send(SCI_GETCURRENTPOS);
    auto line = send(SCI_LINEFROMPOSITION, pos);
    auto mask = send(SCI_MARKERGET, line);

    if (mask & (1 << MARK_BOOKMARK)) {
        send(SCI_MARKERDELETE, line, MARK_BOOKMARK);
    } else {
        send(SCI_MARKERADD, line, MARK_BOOKMARK);
    }
}

void EditorView::nextBookmark() {
    const int MARK_BOOKMARK = 24;
    auto pos = send(SCI_GETCURRENTPOS);
    auto line = send(SCI_LINEFROMPOSITION, pos);
    auto nextLine = send(SCI_MARKERNEXT, line + 1, 1 << MARK_BOOKMARK);

    if (nextLine == -1) {
        // 环回从文档开头查找
        nextLine = send(SCI_MARKERNEXT, 0, 1 << MARK_BOOKMARK);
    }

    if (nextLine != -1) {
        goToLine(static_cast<int>(nextLine + 1));
    }
}

void EditorView::prevBookmark() {
    const int MARK_BOOKMARK = 24;
    auto pos = send(SCI_GETCURRENTPOS);
    auto line = send(SCI_LINEFROMPOSITION, pos);
    auto prevLine = send(SCI_MARKERPREVIOUS, line - 1, 1 << MARK_BOOKMARK);

    if (prevLine == -1) {
        // 环回从文档末尾查找
        auto totalLines = send(SCI_GETLINECOUNT);
        prevLine = send(SCI_MARKERPREVIOUS, totalLines - 1, 1 << MARK_BOOKMARK);
    }

    if (prevLine != -1) {
        goToLine(static_cast<int>(prevLine + 1));
    }
}

void EditorView::clearAllBookmarks() {
    const int MARK_BOOKMARK = 24;
    send(SCI_MARKERDELETEALL, MARK_BOOKMARK);
}

void EditorView::applyGlobalStyleOverride(int fg, int bg) {
    // 样式配置器“Global Styles / Default Style”的前/背景色覆盖（BGR，-1 不覆盖）。
    // 仅影响非语法高亮的默认正文，保留已应用的语言高亮颜色。
    if (bg != -1) send(SCI_STYLESETBACK, STYLE_DEFAULT, bg);
    if (fg != -1) send(SCI_STYLESETFORE, STYLE_DEFAULT, fg);
    send(SCI_COLOURISE, 0, -1);
}

void EditorView::onUpdateUi(Scintilla::Update updated) {
    auto currentLines = send(SCI_GETLINECOUNT);
    if (currentLines != _lastLineCount) {
        _lastLineCount = currentLines;
        updateLineNumberMarginWidth();
    }
    const int updatedBits = static_cast<int>(updated);
    if (updatedBits & static_cast<int>(Scintilla::Update::Selection)) {
        auto pos = send(SCI_GETCURRENTPOS);
        auto line = send(SCI_LINEFROMPOSITION, pos);
        auto col = send(SCI_GETCOLUMN, pos);
        emit cursorPositionChanged(static_cast<int>(line + 1), static_cast<int>(col + 1));

        updateBraceHighlight();
        // 选中词高亮防抖：拖动选区时不至于每像素全文档搜索。
        if (_hlWords) {
            _hlTimer->start();
        } else {
            updateSmartHighlight();
        }
    }

    // 视口优先高亮：滚出已标记的 ±200 行缓冲带后重算一次，保证新视口里的匹配仍然可见。
    // 与选区共用同一个 120ms 防抖定时器，连续滚动只会在停下来的那一帧算一次。
    if (_hlWords && !_hlQuery.isEmpty()
        && (updatedBits & (static_cast<int>(Scintilla::Update::VScroll)
                         | static_cast<int>(Scintilla::Update::HScroll)))) {
        _hlTimer->start();
    }

    // URL 可点击链接：内容变更 / 选区 / 滚动后刷新可见区域内的 URL 下划线。
    // 只扫可见区域，代价极小，无需防抖。
    if (updatedBits & (static_cast<int>(Scintilla::Update::Content)
                     | static_cast<int>(Scintilla::Update::Selection)
                     | static_cast<int>(Scintilla::Update::VScroll)
                     | static_cast<int>(Scintilla::Update::HScroll))) {
        updateUrlHighlighting();
    }
}

void EditorView::setHighlightPrefs(bool matchingWords, bool matchingBraces,
                                   bool matchCase, bool wholeWord) {
    if (_hlWords == matchingWords && _hlBraces == matchingBraces
        && _hlMatchCase == matchCase && _hlWholeWord == wholeWord) {
        return;
    }
    _hlWords = matchingWords;
    _hlBraces = matchingBraces;
    _hlMatchCase = matchCase;
    _hlWholeWord = wholeWord;
    _hlQuery.clear();
    _hlFlags = -1;
    refreshHighlighting();
}

void EditorView::refreshHighlighting() {
    if (_hlTimer) _hlTimer->stop();
    updateBraceHighlight();
    updateSmartHighlight();
}

void EditorView::updateBraceHighlight() {
    auto clearBrace = [this]() {
        _braceStart = -1;
        _braceEnd = -1;
        send(SCI_BRACEHIGHLIGHT, -1, -1);
    };

    if (!_hlBraces) {
        clearBrace();
        return;
    }

    auto isBrace = [](int ch) {
        return ch == '(' || ch == ')' || ch == '[' || ch == ']'
            || ch == '{' || ch == '}';
    };

    sptr_t position = send(SCI_GETCURRENTPOS);
    int ch = static_cast<int>(send(SCI_GETCHARAT, position));
    if (!isBrace(ch) && position > 0) {
        --position;
        ch = static_cast<int>(send(SCI_GETCHARAT, position));
    }
    if (!isBrace(ch)) {
        clearBrace();
        return;
    }

    const sptr_t match = send(SCI_BRACEMATCH, position, 0);
    if (match < 0) {
        clearBrace();
        return;
    }
    _braceStart = static_cast<int>(position);
    _braceEnd = static_cast<int>(match);
    send(SCI_BRACEHIGHLIGHT, position, match);
}

void EditorView::updateSmartHighlight() {
    const sptr_t length = send(SCI_GETLENGTH);
    auto clearIndicator = [this, length]() {
        // 指示器是稀疏区间，整段清一次是 O(log n)，不随文件长度线性增长。
        // 保留全文清除可避免文档编辑后留下错位的旧标记。
        send(SCI_SETINDICATORCURRENT, kSmartHighlightIndicator);
        send(SCI_INDICATORCLEARRANGE, 0, length);
        _hlMarkedStart = 0;
        _hlMarkedEnd = 0;
        _hlQuery.clear();
        _hlFlags = -1;
    };

    if (!_hlWords) {
        clearIndicator();
        return;
    }

    const sptr_t start = send(SCI_GETSELECTIONSTART);
    const sptr_t end = send(SCI_GETSELECTIONEND);
    if (end <= start || end - start > 128) {
        clearIndicator();
        return;
    }

    // SCI_GETSELTEXT 在 lParam==0 时返回的是“不含结尾 NUL”的字符数，
    // 因此缓冲区要多留 1 字节，再按实际长度截断。
    const sptr_t textLength = send(SCI_GETSELTEXT, 0, 0);
    if (textLength <= 0) {
        clearIndicator();
        return;
    }
    std::string selected(static_cast<size_t>(textLength) + 1, '\0');
    send(SCI_GETSELTEXT, static_cast<uptr_t>(textLength + 1),
         reinterpret_cast<sptr_t>(selected.data()));
    selected.resize(static_cast<size_t>(textLength));

    for (unsigned char ch : selected) {
        if (std::isspace(ch)) {
            clearIndicator();
            return;
        }
    }

    int flags = 0;
    if (_hlMatchCase) flags |= SCFIND_MATCHCASE;
    if (_hlWholeWord) flags |= SCFIND_WHOLEWORD;

    const QString query = QString::fromUtf8(selected.data(), static_cast<int>(selected.size()));

    // 只扫描「当前视口 ±200 行」。display line 先折算成 doc line 并夹到合法范围，
    // 折叠、空文档、视口尚未布局时都不能让范围退化成全文扫描。
    const sptr_t lineCount = qMax<sptr_t>(1, send(SCI_GETLINECOUNT));
    auto clampLine = [lineCount](sptr_t line) {
        if (line < 0) return lineCount - 1;
        return qMin<sptr_t>(line, lineCount - 1);
    };
    const sptr_t firstVisible = qMax<sptr_t>(0, send(SCI_GETFIRSTVISIBLELINE));
    const sptr_t linesOnScreen = qMax<sptr_t>(1, send(SCI_LINESONSCREEN));
    const sptr_t topLine = clampLine(send(SCI_DOCLINEFROMVISIBLE, firstVisible));
    const sptr_t bottomLine = clampLine(send(SCI_DOCLINEFROMVISIBLE,
                                            firstVisible + linesOnScreen));
    const sptr_t rangeStart = send(SCI_POSITIONFROMLINE,
                                   qMax<sptr_t>(0, topLine - kHighlightContextLines));
    const sptr_t rangeEnd = qMin<sptr_t>(length, send(SCI_GETLINEENDPOSITION,
                                   clampLine(bottomLine + kHighlightContextLines)));

    // 同一查询且新视口仍落在已标记窗口内：缓冲带吸收小幅滚动，不重复扫描。
    if (query == _hlQuery && flags == _hlFlags
        && rangeStart >= _hlMarkedStart && rangeEnd <= _hlMarkedEnd) return;

    clearIndicator();
    if (rangeEnd <= rangeStart) return;
    _hlQuery = query;
    _hlFlags = flags;

    const QByteArray needle(selected.data(), static_cast<int>(selected.size()));
    send(SCI_SETSEARCHFLAGS, flags);
    send(SCI_SETINDICATORCURRENT, kSmartHighlightIndicator);

    QElapsedTimer budget;
    budget.start();
    sptr_t searchFrom = rangeStart;
    int matches = 0;
    while (searchFrom <= rangeEnd && matches < kMaxSmartHighlightMatches
           && budget.elapsed() < kHighlightBudgetMs) {
        send(SCI_SETTARGETSTART, searchFrom);
        send(SCI_SETTARGETEND, rangeEnd);
        const sptr_t found = sends(SCI_SEARCHINTARGET, needle.size(), needle.constData());
        if (found < 0) break;
        const sptr_t matchStart = send(SCI_GETTARGETSTART);
        const sptr_t matchEnd = send(SCI_GETTARGETEND);
        if (matchEnd <= matchStart) break;
        send(SCI_INDICATORFILLRANGE, matchStart, matchEnd - matchStart);
        searchFrom = matchEnd;
        ++matches;
    }
    _hlMarkedStart = rangeStart;
    _hlMarkedEnd = rangeEnd;
}

void EditorView::setAutoCompletion(bool enabled, int minChars, int type, bool ignoreNumbers) {
    _autoCompletion = enabled;
    _autoCompletionMinChars = std::max(1, minChars);
    _autoCompletionType = (type == 1) ? 1 : 0;
    _autoCompletionIgnoreNumbers = ignoreNumbers;
    if (!enabled) send(SCI_AUTOCCANCEL);
}

void EditorView::updateAutoCompletion(int ch) {
    if (!_autoCompletion) return;
    if (send(SCI_AUTOCACTIVE) != 0) return;

    const unsigned char byte = static_cast<unsigned char>(ch);
    if (!(std::isalnum(byte) || ch == '_' || byte >= 0x80)) return;

    // 超大文档里每次按键做正则全文档扫描会明显卡顿，直接跳过补全（远大于常规源码文件）。
    const sptr_t docLength = send(SCI_GETLENGTH);
    if (docLength > 4 * 1024 * 1024) return;

    const sptr_t currentPos = send(SCI_GETCURRENTPOS);
    const sptr_t startPos = send(SCI_WORDSTARTPOSITION, currentPos, 1);
    if (currentPos <= startPos) return;

    const sptr_t prefixLength = currentPos - startPos;
    if (prefixLength < _autoCompletionMinChars || prefixLength >= 64) return;

    const char* prefixPtr = reinterpret_cast<const char*>(
        send(SCI_GETRANGEPOINTER, startPos, prefixLength));
    if (!prefixPtr) return;
    const std::string prefix(prefixPtr, static_cast<size_t>(prefixLength));

    bool allDigits = true;
    for (unsigned char c : prefix) {
        if (!std::isdigit(c)) { allDigits = false; break; }
    }
    if (_autoCompletionIgnoreNumbers && allDigits) return;

    const sptr_t endPos = send(SCI_WORDENDPOSITION, currentPos, 1);
    QString currentWord;
    if (endPos > startPos) {
        const char* wholePtr = reinterpret_cast<const char*>(
            send(SCI_GETRANGEPOINTER, startPos, endPos - startPos));
        if (wholePtr) currentWord = QString::fromUtf8(wholePtr, static_cast<int>(endPos - startPos));
    }

    // 候选表达式：词首 + 前缀 + 非分隔符。
    const std::string expr = "\\<" + prefix
        + "[^ \\t\\n\\r.,;:\"(){}=<>'+!?\\[\\]]+";
    send(SCI_SETSEARCHFLAGS, SCFIND_WORDSTART | SCFIND_REGEXP | SCFIND_POSIX);

    QSet<QString> seen;
    QStringList words;
    sptr_t searchPos = 0;
    // 每次按键最多花 5ms 收集候选：大文件里也不会把输入线程拖住。
    QElapsedTimer budget;
    budget.start();
    while (searchPos < docLength && words.size() < 2000 && budget.elapsed() < 5) {
        send(SCI_SETTARGETSTART, searchPos);
        send(SCI_SETTARGETEND, docLength);
        if (sends(SCI_SEARCHINTARGET, expr.size(), expr.c_str()) < 0) break;

        const sptr_t wordStart = send(SCI_GETTARGETSTART);
        const sptr_t wordEnd = send(SCI_GETTARGETEND);
        if (wordEnd <= wordStart) break;
        if (wordEnd - wordStart < 256) {
            const char* wordPtr = reinterpret_cast<const char*>(
                send(SCI_GETRANGEPOINTER, wordStart, wordEnd - wordStart));
            if (wordPtr) {
                const QString word = QString::fromUtf8(
                    wordPtr, static_cast<int>(wordEnd - wordStart));
                if (!word.isEmpty() && word != currentWord && !seen.contains(word)) {
                    seen.insert(word);
                    words.append(word);
                }
            }
        }
        searchPos = wordEnd;
    }

    // 补全类型 0 = 文档词 + 当前语言关键字；类型 1 = 仅文档词。
    if (_autoCompletionType == 0) {
        const LanguageDefinition* definition =
            StyleManager::instance().getLanguage(_currentLang);
        if (definition) {
            auto matchesPrefix = [&prefix](const char* data, size_t size) {
                if (size < prefix.size()) return false;
                for (size_t i = 0; i < prefix.size(); ++i) {
                    if (std::tolower(static_cast<unsigned char>(data[i]))
                        != std::tolower(static_cast<unsigned char>(prefix[i]))) {
                        return false;
                    }
                }
                return true;
            };

            for (const auto& entry : definition->rawKeywords) {
                const std::string& keywords = entry.second;
                size_t i = 0;
                while (i < keywords.size() && words.size() < 2000
                       && budget.elapsed() < 5) {
                    while (i < keywords.size()
                           && std::isspace(static_cast<unsigned char>(keywords[i]))) {
                        ++i;
                    }
                    const size_t start = i;
                    while (i < keywords.size()
                           && !std::isspace(static_cast<unsigned char>(keywords[i]))) {
                        ++i;
                    }
                    if (i <= start) continue;
                    const size_t length = i - start;
                    if (length >= 256 || !matchesPrefix(keywords.data() + start, length))
                        continue;
                    const QString candidate = QString::fromUtf8(
                        keywords.data() + start, static_cast<int>(length));
                    if (candidate.isEmpty() || candidate == currentWord
                        || seen.contains(candidate)) {
                        continue;
                    }
                    seen.insert(candidate);
                    words.append(candidate);
                }
                if (words.size() >= 2000 || budget.elapsed() >= 5) break;
            }
        }
    }

    if (words.isEmpty()) return;
    std::sort(words.begin(), words.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });

    const QByteArray list = words.join(QLatin1Char(' ')).toUtf8();
    send(SCI_AUTOCSETSEPARATOR, static_cast<uptr_t>(' '));
    send(SCI_AUTOCSETIGNORECASE, 1);
    send(SCI_AUTOCSETCASEINSENSITIVEBEHAVIOUR, 1);
    sends(SCI_AUTOCSHOW, static_cast<uptr_t>(prefixLength),
          const_cast<char*>(list.constData()));
}

bool EditorView::findNext(const QString& text, bool matchCase, bool wholeWord, bool regex) {
    if (text.isEmpty()) return false;

    int flags = 0;
    if (matchCase) flags |= SCFIND_MATCHCASE;
    if (wholeWord) flags |= SCFIND_WHOLEWORD;
    if (regex) flags |= SCFIND_REGEXP;

    send(SCI_SETSEARCHFLAGS, flags);

    auto selEnd = send(SCI_GETSELECTIONEND);
    auto docLength = send(SCI_GETLENGTH);

    send(SCI_SETTARGETSTART, selEnd);
    send(SCI_SETTARGETEND, docLength);

    QByteArray utf8Text = text.toUtf8();
    auto pos = sends(SCI_SEARCHINTARGET, utf8Text.size(), utf8Text.constData());

    if (pos == -1) {
        send(SCI_SETTARGETSTART, 0);
        send(SCI_SETTARGETEND, selEnd);
        pos = sends(SCI_SEARCHINTARGET, utf8Text.size(), utf8Text.constData());
    }

    if (pos != -1) {
        auto targetStart = send(SCI_GETTARGETSTART);
        auto targetEnd = send(SCI_GETTARGETEND);
        send(SCI_SETSEL, targetStart, targetEnd);
        send(SCI_SCROLLCARET);
        return true;
    }

    return false;
}

bool EditorView::replaceOne(const QString& findText, const QString& replaceText, bool matchCase, bool wholeWord, bool regex) {
    if (!findNext(findText, matchCase, wholeWord, regex)) {
        return false;
    }

    QByteArray utf8Replace = replaceText.toUtf8();
    sends(SCI_REPLACETARGET, utf8Replace.size(), utf8Replace.constData());
    return true;
}

int EditorView::replaceAll(const QString& findText, const QString& replaceText, bool matchCase, bool wholeWord, bool regex) {
    if (findText.isEmpty()) return 0;

    int flags = 0;
    if (matchCase) flags |= SCFIND_MATCHCASE;
    if (wholeWord) flags |= SCFIND_WHOLEWORD;
    if (regex) flags |= SCFIND_REGEXP;

    send(SCI_SETSEARCHFLAGS, flags);

    auto docLength = send(SCI_GETLENGTH);
    send(SCI_SETTARGETSTART, 0);
    send(SCI_SETTARGETEND, docLength);

    QByteArray utf8Find = findText.toUtf8();
    QByteArray utf8Replace = replaceText.toUtf8();

    int count = 0;
    send(SCI_BEGINUNDOACTION);

    while (true) {
        auto pos = sends(SCI_SEARCHINTARGET, utf8Find.size(), utf8Find.constData());
        if (pos == -1) break;

        sends(SCI_REPLACETARGET, utf8Replace.size(), utf8Replace.constData());
        count++;

        // 关键：零宽正则匹配（如 \b、^、a*）+ 空替换时，target 不会前进，
        // 必须强制至少前进 1 字节，否则会无限循环卡死 UI。
        auto newStart = send(SCI_GETTARGETSTART);
        auto newEnd = send(SCI_GETTARGETEND);
        auto advance = (newEnd > newStart) ? static_cast<size_t>(newEnd - newStart) : static_cast<size_t>(1);
        auto nextStart = newStart + advance;
        auto docLen = send(SCI_GETLENGTH);
        if (nextStart > docLen) break;
        send(SCI_SETTARGETSTART, nextStart);
        send(SCI_SETTARGETEND, docLen);
        if (nextStart >= docLen) break;
    }

    send(SCI_ENDUNDOACTION);
    return count;
}

bool EditorView::findPrev(const QString& text, bool matchCase, bool wholeWord, bool regex) {
    if (text.isEmpty()) return false;
    int flags = 0;
    if (matchCase) flags |= SCFIND_MATCHCASE;
    if (wholeWord) flags |= SCFIND_WHOLEWORD;
    if (regex) flags |= SCFIND_REGEXP;
    send(SCI_SETSEARCHFLAGS, flags);

    auto selStart = send(SCI_GETSELECTIONSTART);
    auto docLength = send(SCI_GETLENGTH);

    send(SCI_SETTARGETSTART, selStart);
    send(SCI_SETTARGETEND, 0);

    QByteArray utf8Text = text.toUtf8();
    auto pos = sends(SCI_SEARCHINTARGET, utf8Text.size(), utf8Text.constData());
    if (pos == -1) {
        send(SCI_SETTARGETSTART, docLength);
        send(SCI_SETTARGETEND, selStart);
        pos = sends(SCI_SEARCHINTARGET, utf8Text.size(), utf8Text.constData());
    }

    if (pos != -1) {
        auto targetStart = send(SCI_GETTARGETSTART);
        auto targetEnd = send(SCI_GETTARGETEND);
        send(SCI_SETSEL, targetStart, targetEnd);
        send(SCI_SCROLLCARET);
        return true;
    }
    return false;
}

void EditorView::deleteCurrentLine() {
    send(SCI_LINEDELETE);
}

void EditorView::toUpperCase() {
    auto selStart = send(SCI_GETSELECTIONSTART);
    auto selEnd = send(SCI_GETSELECTIONEND);
    if (selStart == selEnd) {
        auto pos = send(SCI_GETCURRENTPOS);
        auto wStart = send(SCI_WORDSTARTPOSITION, pos, 1);
        auto wEnd = send(SCI_WORDENDPOSITION, pos, 1);
        send(SCI_SETSEL, wStart, wEnd);
    }
    send(SCI_UPPERCASE);
}

void EditorView::toLowerCase() {
    auto selStart = send(SCI_GETSELECTIONSTART);
    auto selEnd = send(SCI_GETSELECTIONEND);
    if (selStart == selEnd) {
        auto pos = send(SCI_GETCURRENTPOS);
        auto wStart = send(SCI_WORDSTARTPOSITION, pos, 1);
        auto wEnd = send(SCI_WORDENDPOSITION, pos, 1);
        send(SCI_SETSEL, wStart, wEnd);
    }
    send(SCI_LOWERCASE);
}

void EditorView::toTitleCase() {
    auto selStart = send(SCI_GETSELECTIONSTART);
    auto selEnd = send(SCI_GETSELECTIONEND);
    if (selStart == selEnd) {
        auto pos = send(SCI_GETCURRENTPOS);
        selStart = send(SCI_WORDSTARTPOSITION, pos, 1);
        selEnd = send(SCI_WORDENDPOSITION, pos, 1);
        send(SCI_SETSEL, selStart, selEnd);
    }
    auto len = selEnd - selStart;
    if (len <= 0) return;
    QByteArray buf(len + 1, '\0');
    send(SCI_GETSELTEXT, 0, reinterpret_cast<sptr_t>(buf.data()));
    QString str = QString::fromUtf8(buf.constData());
    bool newWord = true;
    for (int i = 0; i < str.size(); ++i) {
        if (str[i].isSpace() || str[i].isPunct()) {
            newWord = true;
        } else if (newWord) {
            str[i] = str[i].toUpper();
            newWord = false;
        } else {
            str[i] = str[i].toLower();
        }
    }
    QByteArray res = str.toUtf8();
    sends(SCI_REPLACESEL, 0, res.constData());
}

void EditorView::invertCase() {
    auto selStart = send(SCI_GETSELECTIONSTART);
    auto selEnd = send(SCI_GETSELECTIONEND);
    if (selStart == selEnd) {
        auto pos = send(SCI_GETCURRENTPOS);
        selStart = send(SCI_WORDSTARTPOSITION, pos, 1);
        selEnd = send(SCI_WORDENDPOSITION, pos, 1);
        send(SCI_SETSEL, selStart, selEnd);
    }
    auto len = selEnd - selStart;
    if (len <= 0) return;
    QByteArray buf(len + 1, '\0');
    send(SCI_GETSELTEXT, 0, reinterpret_cast<sptr_t>(buf.data()));
    QString str = QString::fromUtf8(buf.constData());
    for (int i = 0; i < str.size(); ++i) {
        if (str[i].isUpper()) str[i] = str[i].toLower();
        else if (str[i].isLower()) str[i] = str[i].toUpper();
    }
    QByteArray res = str.toUtf8();
    sends(SCI_REPLACESEL, 0, res.constData());
}

void EditorView::joinLines() {
    send(SCI_LINESJOIN);
}

void EditorView::splitLines() {
    send(SCI_LINESSPLIT, 0);
}

void EditorView::sortLines(bool ascending, bool ignoreCase) {
    auto selStart = send(SCI_GETSELECTIONSTART);
    auto selEnd = send(SCI_GETSELECTIONEND);
    int lineStart = 0;
    int lineEnd = 0;
    if (selStart != selEnd) {
        lineStart = static_cast<int>(send(SCI_LINEFROMPOSITION, selStart));
        lineEnd = static_cast<int>(send(SCI_LINEFROMPOSITION, selEnd));
        if (lineEnd > lineStart && selEnd == send(SCI_POSITIONFROMLINE, lineEnd)) {
            lineEnd--;
        }
    } else {
        lineStart = 0;
        lineEnd = static_cast<int>(send(SCI_GETLINECOUNT)) - 1;
        if (lineEnd > lineStart && send(SCI_LINELENGTH, lineEnd) == 0) {
            lineEnd--;
        }
    }
    if (lineEnd <= lineStart) return;

    QStringList lines;
    for (int i = lineStart; i <= lineEnd; ++i) {
        auto len = send(SCI_LINELENGTH, i);
        QByteArray buf(len + 1, '\0');
        send(SCI_GETLINE, i, reinterpret_cast<sptr_t>(buf.data()));
        QString line = QString::fromUtf8(buf.constData());
        while (line.endsWith('\r') || line.endsWith('\n')) {
            line.chop(1);
        }
        lines.append(line);
    }

    auto comp = [ignoreCase, ascending](const QString& a, const QString& b) {
        int diff = ignoreCase ? QString::compare(a, b, Qt::CaseInsensitive) : QString::compare(a, b, Qt::CaseSensitive);
        return ascending ? (diff < 0) : (diff > 0);
    };
    std::sort(lines.begin(), lines.end(), comp);

    QString eol = (_buffer->getLineEnding() == LineEnding::CRLF) ? "\r\n"
                : (_buffer->getLineEnding() == LineEnding::CR) ? "\r" : "\n";
    QString joined = lines.join(eol);
    if (lineEnd < send(SCI_GETLINECOUNT) - 1) {
        joined += eol;
    }

    auto startPos = send(SCI_POSITIONFROMLINE, lineStart);
    auto endPos = (lineEnd < send(SCI_GETLINECOUNT) - 1) ? send(SCI_POSITIONFROMLINE, lineEnd + 1) : send(SCI_GETLENGTH);
    send(SCI_BEGINUNDOACTION);
    send(SCI_SETSEL, startPos, endPos);
    sends(SCI_REPLACESEL, 0, joined.toUtf8().constData());
    send(SCI_ENDUNDOACTION);
}

void EditorView::removeDuplicateLines() {
    auto selStart = send(SCI_GETSELECTIONSTART);
    auto selEnd = send(SCI_GETSELECTIONEND);
    int lineStart = 0;
    int lineEnd = 0;
    if (selStart != selEnd) {
        lineStart = static_cast<int>(send(SCI_LINEFROMPOSITION, selStart));
        lineEnd = static_cast<int>(send(SCI_LINEFROMPOSITION, selEnd));
        if (lineEnd > lineStart && selEnd == send(SCI_POSITIONFROMLINE, lineEnd)) {
            lineEnd--;
        }
    } else {
        lineStart = 0;
        lineEnd = static_cast<int>(send(SCI_GETLINECOUNT)) - 1;
        if (lineEnd > lineStart && send(SCI_LINELENGTH, lineEnd) == 0) {
            lineEnd--;
        }
    }
    if (lineEnd < lineStart) return;

    QStringList uniqueLines;
    QSet<QString> seen;
    for (int i = lineStart; i <= lineEnd; ++i) {
        auto len = send(SCI_LINELENGTH, i);
        QByteArray buf(len + 1, '\0');
        send(SCI_GETLINE, i, reinterpret_cast<sptr_t>(buf.data()));
        QString line = QString::fromUtf8(buf.constData());
        while (line.endsWith('\r') || line.endsWith('\n')) {
            line.chop(1);
        }
        if (!seen.contains(line)) {
            seen.insert(line);
            uniqueLines.append(line);
        }
    }

    QString eol = (_buffer->getLineEnding() == LineEnding::CRLF) ? "\r\n"
                : (_buffer->getLineEnding() == LineEnding::CR) ? "\r" : "\n";
    QString joined = uniqueLines.join(eol);
    if (lineEnd < send(SCI_GETLINECOUNT) - 1) {
        joined += eol;
    }

    auto startPos = send(SCI_POSITIONFROMLINE, lineStart);
    auto endPos = (lineEnd < send(SCI_GETLINECOUNT) - 1) ? send(SCI_POSITIONFROMLINE, lineEnd + 1) : send(SCI_GETLENGTH);
    send(SCI_BEGINUNDOACTION);
    send(SCI_SETSEL, startPos, endPos);
    sends(SCI_REPLACESEL, 0, joined.toUtf8().constData());
    send(SCI_ENDUNDOACTION);
}

void EditorView::reverseLines() {
    auto selStart = send(SCI_GETSELECTIONSTART);
    auto selEnd = send(SCI_GETSELECTIONEND);
    int lineStart = 0;
    int lineEnd = 0;
    if (selStart != selEnd) {
        lineStart = static_cast<int>(send(SCI_LINEFROMPOSITION, selStart));
        lineEnd = static_cast<int>(send(SCI_LINEFROMPOSITION, selEnd));
        if (lineEnd > lineStart && selEnd == send(SCI_POSITIONFROMLINE, lineEnd)) {
            lineEnd--;
        }
    } else {
        lineStart = 0;
        lineEnd = static_cast<int>(send(SCI_GETLINECOUNT)) - 1;
        if (lineEnd > lineStart && send(SCI_LINELENGTH, lineEnd) == 0) {
            lineEnd--;
        }
    }
    if (lineEnd <= lineStart) return;

    QStringList lines;
    for (int i = lineStart; i <= lineEnd; ++i) {
        auto len = send(SCI_LINELENGTH, i);
        QByteArray buf(len + 1, '\0');
        send(SCI_GETLINE, i, reinterpret_cast<sptr_t>(buf.data()));
        QString line = QString::fromUtf8(buf.constData());
        while (line.endsWith('\r') || line.endsWith('\n')) {
            line.chop(1);
        }
        lines.append(line);
    }
    std::reverse(lines.begin(), lines.end());

    QString eol = (_buffer->getLineEnding() == LineEnding::CRLF) ? "\r\n"
                : (_buffer->getLineEnding() == LineEnding::CR) ? "\r" : "\n";
    QString joined = lines.join(eol);
    if (lineEnd < send(SCI_GETLINECOUNT) - 1) {
        joined += eol;
    }

    auto startPos = send(SCI_POSITIONFROMLINE, lineStart);
    auto endPos = (lineEnd < send(SCI_GETLINECOUNT) - 1) ? send(SCI_POSITIONFROMLINE, lineEnd + 1) : send(SCI_GETLENGTH);
    send(SCI_BEGINUNDOACTION);
    send(SCI_SETSEL, startPos, endPos);
    sends(SCI_REPLACESEL, 0, joined.toUtf8().constData());
    send(SCI_ENDUNDOACTION);
}

void EditorView::insertEmptyLine(bool above) {
    auto pos = send(SCI_GETCURRENTPOS);
    auto line = send(SCI_LINEFROMPOSITION, pos);
    QString eol = (_buffer->getLineEnding() == LineEnding::CRLF) ? "\r\n" : "\n";
    if (above) {
        auto lineStart = send(SCI_POSITIONFROMLINE, line);
        sends(SCI_INSERTTEXT, lineStart, eol.toUtf8().constData());
        send(SCI_GOTOPOS, lineStart);
    } else {
        auto lineEnd = send(SCI_GETLINEENDPOSITION, line);
        sends(SCI_INSERTTEXT, lineEnd, eol.toUtf8().constData());
        send(SCI_GOTOPOS, lineEnd + eol.size());
    }
}

void EditorView::toggleLineComment() {
    const auto* langDef = StyleManager::instance().getLanguage(_currentLang);
    std::string commentToken = (langDef && !langDef->commentLine.empty()) ? langDef->commentLine : "//";

    auto selStart = send(SCI_GETSELECTIONSTART);
    auto selEnd = send(SCI_GETSELECTIONEND);
    int lineStart = static_cast<int>(send(SCI_LINEFROMPOSITION, selStart));
    int lineEnd = static_cast<int>(send(SCI_LINEFROMPOSITION, selEnd));
    if (selEnd == send(SCI_POSITIONFROMLINE, lineEnd) && lineEnd > lineStart) {
        lineEnd--;
    }

    send(SCI_BEGINUNDOACTION);
    for (int i = lineStart; i <= lineEnd; ++i) {
        auto len = send(SCI_LINELENGTH, i);
        QByteArray buf(len + 1, '\0');
        send(SCI_GETLINE, i, reinterpret_cast<sptr_t>(buf.data()));
        std::string lineStr(buf.constData());

        size_t nonSpace = 0;
        while (nonSpace < lineStr.size() && (lineStr[nonSpace] == ' ' || lineStr[nonSpace] == '\t')) {
            nonSpace++;
        }

        auto lineStartPos = send(SCI_POSITIONFROMLINE, i);
        if (lineStr.substr(nonSpace, commentToken.size()) == commentToken) {
            size_t delLen = commentToken.size();
            if (nonSpace + delLen < lineStr.size() && lineStr[nonSpace + delLen] == ' ') {
                delLen++;
            }
            send(SCI_DELETERANGE, lineStartPos + nonSpace, delLen);
        } else {
            std::string ins = commentToken + " ";
            sends(SCI_INSERTTEXT, lineStartPos + nonSpace, ins.c_str());
        }
    }
    send(SCI_ENDUNDOACTION);
}

void EditorView::blockComment() {
    const auto* langDef = StyleManager::instance().getLanguage(_currentLang);
    std::string cStart = (langDef && !langDef->commentStart.empty()) ? langDef->commentStart : "/*";
    std::string cEnd = (langDef && !langDef->commentEnd.empty()) ? langDef->commentEnd : "*/";

    auto selStart = send(SCI_GETSELECTIONSTART);
    auto selEnd = send(SCI_GETSELECTIONEND);
    if (selStart == selEnd) {
        auto line = send(SCI_LINEFROMPOSITION, selStart);
        selStart = send(SCI_POSITIONFROMLINE, line);
        selEnd = send(SCI_GETLINEENDPOSITION, line);
    }
    send(SCI_BEGINUNDOACTION);
    sends(SCI_INSERTTEXT, selEnd, (" " + cEnd).c_str());
    sends(SCI_INSERTTEXT, selStart, (cStart + " ").c_str());
    send(SCI_SETSEL, selStart, selEnd + cStart.size() + cEnd.size() + 2);
    send(SCI_ENDUNDOACTION);
}

void EditorView::unblockComment() {
    const auto* langDef = StyleManager::instance().getLanguage(_currentLang);
    std::string cStart = (langDef && !langDef->commentStart.empty()) ? langDef->commentStart : "/*";
    std::string cEnd = (langDef && !langDef->commentEnd.empty()) ? langDef->commentEnd : "*/";

    auto selStart = send(SCI_GETSELECTIONSTART);
    auto selEnd = send(SCI_GETSELECTIONEND);
    auto len = selEnd - selStart;
    if (len <= 0) return;

    QByteArray buf(len + 1, '\0');
    send(SCI_GETSELTEXT, 0, reinterpret_cast<sptr_t>(buf.data()));
    std::string text(buf.constData());

    auto p1 = text.find(cStart);
    auto p2 = text.rfind(cEnd);
    if (p1 != std::string::npos && p2 != std::string::npos && p2 > p1) {
        send(SCI_BEGINUNDOACTION);
        size_t endDel = cEnd.size();
        if (p2 > 0 && text[p2 - 1] == ' ') {
            p2--;
            endDel++;
        }
        send(SCI_DELETERANGE, selStart + p2, endDel);

        size_t startDel = cStart.size();
        if (p1 + startDel < text.size() && text[p1 + startDel] == ' ') {
            startDel++;
        }
        send(SCI_DELETERANGE, selStart + p1, startDel);
        send(SCI_ENDUNDOACTION);
    }
}

void EditorView::trimTrailingSpaces() {
    send(SCI_BEGINUNDOACTION);
    int lineCount = static_cast<int>(send(SCI_GETLINECOUNT));
    // 逆序处理，且先探测末尾字符，大幅减少 90% 以上无尾部空格行的字符查询与 Gap 搬移
    for (int i = lineCount - 1; i >= 0; --i) {
        auto lineStart = send(SCI_POSITIONFROMLINE, i);
        auto lineEnd = send(SCI_GETLINEENDPOSITION, i);
        if (lineEnd <= lineStart) continue;

        char lastChar = static_cast<char>(send(SCI_GETCHARAT, lineEnd - 1));
        if (lastChar != ' ' && lastChar != '\t') continue;

        auto p = lineEnd - 1;
        while (p > lineStart) {
            char c = static_cast<char>(send(SCI_GETCHARAT, p - 1));
            if (c == ' ' || c == '\t') {
                --p;
            } else {
                break;
            }
        }
        send(SCI_DELETERANGE, p, lineEnd - p);
    }
    send(SCI_ENDUNDOACTION);
}

void EditorView::trimLeadingSpaces() {
    send(SCI_BEGINUNDOACTION);
    int lineCount = static_cast<int>(send(SCI_GETLINECOUNT));
    // 逆序处理，且先探测首字符
    for (int i = lineCount - 1; i >= 0; --i) {
        auto lineStart = send(SCI_POSITIONFROMLINE, i);
        auto lineEnd = send(SCI_GETLINEENDPOSITION, i);
        if (lineEnd <= lineStart) continue;

        char firstChar = static_cast<char>(send(SCI_GETCHARAT, lineStart));
        if (firstChar != ' ' && firstChar != '\t') continue;

        auto p = lineStart + 1;
        while (p < lineEnd) {
            char c = static_cast<char>(send(SCI_GETCHARAT, p));
            if (c == ' ' || c == '\t') {
                ++p;
            } else {
                break;
            }
        }
        send(SCI_DELETERANGE, lineStart, p - lineStart);
    }
    send(SCI_ENDUNDOACTION);
}

void EditorView::trimBoth() {
    trimLeadingSpaces();
    trimTrailingSpaces();
}

void EditorView::tabsToSpaces(int tabWidth) {
    if (tabWidth <= 0) tabWidth = 4;
    std::string spaces(tabWidth, ' ');
    replaceAll("\t", QString::fromStdString(spaces), true, false, false);
}

void EditorView::spacesToTabs(int tabWidth) {
    if (tabWidth <= 0) tabWidth = 4;
    std::string spaces(tabWidth, ' ');
    replaceAll(QString::fromStdString(spaces), "\t", true, false, false);
}

void EditorView::removeEmptyLines(bool onlyBlank) {
    send(SCI_BEGINUNDOACTION);
    int lineCount = static_cast<int>(send(SCI_GETLINECOUNT));
    for (int i = lineCount - 1; i >= 0; --i) {
        auto lineStart = send(SCI_POSITIONFROMLINE, i);
        auto lineEnd = send(SCI_GETLINEENDPOSITION, i);
        bool isEmpty = false;
        if (lineStart == lineEnd) {
            isEmpty = true;
        } else if (onlyBlank) {
            bool allSpace = true;
            for (auto p = lineStart; p < lineEnd; ++p) {
                char c = static_cast<char>(send(SCI_GETCHARAT, p));
                if (c != ' ' && c != '\t') {
                    allSpace = false;
                    break;
                }
            }
            isEmpty = allSpace;
        }
        if (isEmpty) {
            auto nextLineStart = (i < lineCount - 1) ? send(SCI_POSITIONFROMLINE, i + 1) : send(SCI_GETLENGTH);
            send(SCI_DELETERANGE, lineStart, nextLineStart - lineStart);
        }
    }
    send(SCI_ENDUNDOACTION);
}

void EditorView::goToMatchingBrace() {
    auto pos = send(SCI_GETCURRENTPOS);
    auto match = send(SCI_BRACEMATCH, pos, 0);
    if (match == -1 && pos > 0) {
        match = send(SCI_BRACEMATCH, pos - 1, 0);
    }
    if (match != -1) {
        send(SCI_BRACEHIGHLIGHT, pos, match);
        send(SCI_GOTOPOS, match);
    }
}

void EditorView::selectToMatchingBrace() {
    auto pos = send(SCI_GETCURRENTPOS);
    auto match = send(SCI_BRACEMATCH, pos, 0);
    if (match == -1 && pos > 0) {
        pos = pos - 1;
        match = send(SCI_BRACEMATCH, pos, 0);
    }
    if (match != -1) {
        auto start = std::min(pos, match);
        auto end = std::max(pos, match) + 1;
        send(SCI_SETSEL, start, end);
    }
}

void EditorView::markSelectedText(int styleIndex) {
    if (styleIndex < 1 || styleIndex > 5) styleIndex = 1;
    int indic = 8 + (styleIndex - 1);
    static const int colors[] = {0x00FFFF, 0xFF00FF, 0x00FF00, 0x0080FF, 0xFF8000};
    send(SCI_INDICSETSTYLE, indic, INDIC_ROUNDBOX);
    send(SCI_INDICSETFORE, indic, colors[styleIndex - 1]);
    send(SCI_INDICSETALPHA, indic, 80);
    send(SCI_SETINDICATORCURRENT, indic);

    auto selStart = send(SCI_GETSELECTIONSTART);
    auto selEnd = send(SCI_GETSELECTIONEND);
    if (selStart != selEnd) {
        send(SCI_INDICATORFILLRANGE, selStart, selEnd - selStart);
    }
}

void EditorView::clearMarks() {
    auto len = send(SCI_GETLENGTH);
    for (int k = 8; k <= 12; ++k) {
        send(SCI_SETINDICATORCURRENT, k);
        send(SCI_INDICATORCLEARRANGE, 0, len);
    }
}

// ---------------- URL 可点击链接 ----------------

QString EditorView::textInRange(sptr_t a, sptr_t b) const {
    if (b <= a) return QString();
    QByteArray buf(static_cast<int>(b - a + 1), 0);
    Sci_TextRangeFull tr{};
    tr.chrg.cpMin = static_cast<Sci_Position>(a);
    tr.chrg.cpMax = static_cast<Sci_Position>(b);
    tr.lpstrText = buf.data();
    const_cast<EditorView*>(this)->send(SCI_GETTEXTRANGEFULL, 0,
                                        reinterpret_cast<sptr_t>(&tr));
    return QString::fromUtf8(buf.constData(), static_cast<int>(b - a));
}

void EditorView::updateUrlHighlighting() {
    const sptr_t length = send(SCI_GETLENGTH);

    // 可见区域优先：只扫描视口内的文本，避免大文件全文档扫描（对齐高性能原则）。
    // 未布局（如离屏测试）时退化为全文档扫描，保证测试确定性。
    sptr_t vStart = 0, vEnd = length;
    const sptr_t firstVisible = send(SCI_GETFIRSTVISIBLELINE);
    const sptr_t linesOnScreen = qMax<sptr_t>(1, send(SCI_LINESONSCREEN));
    if (firstVisible >= 0) {
        const sptr_t topLine = send(SCI_DOCLINEFROMVISIBLE, firstVisible);
        const sptr_t bottomLine = send(SCI_DOCLINEFROMVISIBLE, firstVisible + linesOnScreen);
        vStart = send(SCI_POSITIONFROMLINE, topLine);
        vEnd = send(SCI_GETLINEENDPOSITION, bottomLine);
    }
    if (vEnd <= vStart) {
        vStart = 0;
        vEnd = length;
    }

    send(SCI_SETINDICATORCURRENT, kUrlIndicator);
    send(SCI_INDICATORCLEARRANGE, vStart, vEnd - vStart);

    if (!_urlClickable) return;

    const QString text = textInRange(vStart, vEnd);
    const auto ranges = findUrls(text);
    for (const auto& r : ranges) {
        const sptr_t bStart = send(SCI_POSITIONRELATIVE, vStart, r.first);
        const sptr_t bEnd = send(SCI_POSITIONRELATIVE, vStart, r.first + r.second);
        if (bEnd > bStart) {
            send(SCI_SETINDICATORCURRENT, kUrlIndicator);
            send(SCI_INDICATORFILLRANGE, bStart, bEnd - bStart);
        }
    }
}

void EditorView::setUrlClickable(bool enabled) {
    if (_urlClickable == enabled) return;
    _urlClickable = enabled;
    updateUrlHighlighting();
}

void EditorView::refreshUrlHighlighting() {
    updateUrlHighlighting();
}

QString EditorView::urlTextAt(sptr_t pos) const {
    const int mask = static_cast<int>(send(SCI_INDICATORALLONFOR, pos));
    if (!(mask & (1 << kUrlIndicator))) return QString();
    const sptr_t s = send(SCI_INDICATORSTART, kUrlIndicator, pos);
    const sptr_t e = send(SCI_INDICATOREND, kUrlIndicator, pos);
    return const_cast<EditorView*>(this)->textInRange(s, e);
}

bool EditorView::openUrlAt(sptr_t pos) {
    const QString url = urlTextAt(pos);
    if (url.isEmpty()) return false;
    QUrl q(url);
    if (!q.isValid() || q.scheme().isEmpty())
        q = QUrl::fromUserInput(url);
    static const QStringList allowed = {
        QStringLiteral("http"), QStringLiteral("https"),
        QStringLiteral("ftp"), QStringLiteral("file"), QStringLiteral("mailto")};
    if (q.isValid() && allowed.contains(q.scheme().toLower())) {
        QDesktopServices::openUrl(q);
        return true;
    }
    return false;
}

void EditorView::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton
        && (event->modifiers() & Qt::ControlModifier)) {
        const sptr_t pos = send(SCI_CHARPOSITIONFROMPOINT,
                                static_cast<uptr_t>(event->x()),
                                static_cast<sptr_t>(event->y()));
        if (openUrlAt(pos)) {
            event->accept();
            return;
        }
    }
    ScintillaEditBase::mousePressEvent(event);
}

void EditorView::copyBookmarkedLines() {
    const int MARK_BOOKMARK = 24;
    int lineCount = static_cast<int>(send(SCI_GETLINECOUNT));
    QString text;
    for (int i = 0; i < lineCount; ++i) {
        if (send(SCI_MARKERGET, i) & (1 << MARK_BOOKMARK)) {
            auto len = send(SCI_LINELENGTH, i);
            QByteArray buf(len + 1, '\0');
            send(SCI_GETLINE, i, reinterpret_cast<sptr_t>(buf.data()));
            text += QString::fromUtf8(buf.constData());
        }
    }
    if (!text.isEmpty()) {
        QApplication::clipboard()->setText(text);
    }
}

void EditorView::cutBookmarkedLines() {
    copyBookmarkedLines();
    deleteBookmarkedLines();
}

void EditorView::deleteBookmarkedLines() {
    const int MARK_BOOKMARK = 24;
    send(SCI_BEGINUNDOACTION);
    int lineCount = static_cast<int>(send(SCI_GETLINECOUNT));
    for (int i = lineCount - 1; i >= 0; --i) {
        if (send(SCI_MARKERGET, i) & (1 << MARK_BOOKMARK)) {
            auto start = send(SCI_POSITIONFROMLINE, i);
            auto end = (i < lineCount - 1) ? send(SCI_POSITIONFROMLINE, i + 1) : send(SCI_GETLENGTH);
            send(SCI_DELETERANGE, start, end - start);
        }
    }
    send(SCI_ENDUNDOACTION);
}

void EditorView::deleteNonBookmarkedLines() {
    const int MARK_BOOKMARK = 24;
    send(SCI_BEGINUNDOACTION);
    int lineCount = static_cast<int>(send(SCI_GETLINECOUNT));
    for (int i = lineCount - 1; i >= 0; --i) {
        if (!(send(SCI_MARKERGET, i) & (1 << MARK_BOOKMARK))) {
            auto start = send(SCI_POSITIONFROMLINE, i);
            auto end = (i < lineCount - 1) ? send(SCI_POSITIONFROMLINE, i + 1) : send(SCI_GETLENGTH);
            send(SCI_DELETERANGE, start, end - start);
        }
    }
    send(SCI_ENDUNDOACTION);
}

void EditorView::inverseBookmarks() {
    const int MARK_BOOKMARK = 24;
    int lineCount = static_cast<int>(send(SCI_GETLINECOUNT));
    for (int i = 0; i < lineCount; ++i) {
        if (send(SCI_MARKERGET, i) & (1 << MARK_BOOKMARK)) {
            send(SCI_MARKERDELETE, i, MARK_BOOKMARK);
        } else {
            send(SCI_MARKERADD, i, MARK_BOOKMARK);
        }
    }
}

void EditorView::foldAll() {
    send(SCI_FOLDALL, SC_FOLDACTION_CONTRACT);
}

void EditorView::unfoldAll() {
    send(SCI_FOLDALL, SC_FOLDACTION_EXPAND);
}

void EditorView::toggleCurrentFold() {
    auto pos = send(SCI_GETCURRENTPOS);
    auto line = send(SCI_LINEFROMPOSITION, pos);
    send(SCI_TOGGLEFOLD, line);
}

sptr_t EditorView::currentFoldHeader() const {
    const sptr_t pos = send(SCI_GETCURRENTPOS);
    const sptr_t line = send(SCI_LINEFROMPOSITION, pos);
    if (send(SCI_GETFOLDLEVEL, line) & SC_FOLDLEVELHEADERFLAG) return line;
    // 光标在块内普通行时，操作对象是它所属的父块。
    return send(SCI_GETFOLDPARENT, line);
}

void EditorView::foldCurrent() {
    const sptr_t header = currentFoldHeader();
    if (header < 0) return;
    if (send(SCI_GETFOLDEXPANDED, header)) send(SCI_TOGGLEFOLD, header);
}

void EditorView::expandCurrent() {
    const sptr_t header = currentFoldHeader();
    if (header < 0) return;
    if (!send(SCI_GETFOLDEXPANDED, header)) send(SCI_TOGGLEFOLD, header);
}

void EditorView::foldLevel(int level) {
    int lineCount = static_cast<int>(send(SCI_GETLINECOUNT));
    for (int i = 0; i < lineCount; ++i) {
        auto foldLvl = send(SCI_GETFOLDLEVEL, i);
        if (foldLvl & SC_FOLDLEVELHEADERFLAG) {
            int currentLvl = static_cast<int>((foldLvl & SC_FOLDLEVELNUMBERMASK) - SC_FOLDLEVELBASE + 1);
            if (currentLvl == level && send(SCI_GETFOLDEXPANDED, i)) {
                send(SCI_TOGGLEFOLD, i);
            }
        }
    }
}

void EditorView::unfoldLevel(int level) {
    int lineCount = static_cast<int>(send(SCI_GETLINECOUNT));
    for (int i = 0; i < lineCount; ++i) {
        auto foldLvl = send(SCI_GETFOLDLEVEL, i);
        if (foldLvl & SC_FOLDLEVELHEADERFLAG) {
            int currentLvl = static_cast<int>((foldLvl & SC_FOLDLEVELNUMBERMASK) - SC_FOLDLEVELBASE + 1);
            if (currentLvl == level && !send(SCI_GETFOLDEXPANDED, i)) {
                send(SCI_TOGGLEFOLD, i);
            }
        }
    }
}

void EditorView::setShowWhitespace(bool show) {
    send(SCI_SETVIEWWS, show ? SCWS_VISIBLEALWAYS : SCWS_INVISIBLE);
}

void EditorView::setShowEOL(bool show) {
    send(SCI_SETVIEWEOL, show ? 1 : 0);
}

void EditorView::setShowIndentGuides(bool show) {
    send(SCI_SETINDENTATIONGUIDES, show ? SC_IV_LOOKBOTH : SC_IV_NONE);
}

void EditorView::setWrapMode(bool wrap) {
    const sptr_t target = wrap ? SC_WRAP_WORD : SC_WRAP_NONE;
    // 值没变就别再发：SETWRAPMODE 会触发重排并丢掉当前滚动位置。
    // 恢复会话后紧接着 applyPreferences 就会调到这里，不跳过的话
    // 刚还原的 firstVisibleLine/xOffset 会被冲掉。
    if (send(SCI_GETWRAPMODE) == target) return;
    send(SCI_SETWRAPMODE, target);
}

DocSummary EditorView::getSummary() const {
    DocSummary s;
    auto len = send(SCI_GETLENGTH);
    s.bytes = static_cast<int>(len);
    s.lines = static_cast<int>(send(SCI_GETLINECOUNT));

    auto selStart = send(SCI_GETSELECTIONSTART);
    auto selEnd = send(SCI_GETSELECTIONEND);
    if (selStart != selEnd) {
        s.selCharacters = static_cast<int>(selEnd - selStart);
        s.selLines = static_cast<int>(send(SCI_LINEFROMPOSITION, selEnd) - send(SCI_LINEFROMPOSITION, selStart) + 1);
    }

    if (len > 0) {
        QByteArray buf(len + 1, '\0');
        send(SCI_GETTEXT, len + 1, reinterpret_cast<sptr_t>(buf.data()));
        QString text = QString::fromUtf8(buf.constData(), len);
        s.characters = text.length();
        int words = 0;
        int noSpaces = 0;
        bool inWord = false;
        for (QChar c : text) {
            if (!c.isSpace()) {
                noSpaces++;
                if (!inWord) {
                    inWord = true;
                    words++;
                }
            } else {
                inWord = false;
            }
        }
        s.charactersNoSpaces = noSpaces;
        s.words = words;
    }
    return s;
}
