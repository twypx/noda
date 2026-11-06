#include "EditorView.h"
#include "StyleManager.h"
#include "utils/EncodingDetector.h"
#include "utils/EncodingConverter.h"
#include "ILexer.h"
#include "Lexilla.h"
#include <QFile>
#include <QFileInfo>
#include <QByteArray>
#include <QApplication>
#include <QClipboard>
#include <QStringList>
#include <QSet>
#include <algorithm>
#include <vector>
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
        emit documentModified(dirty);
    });

    connect(this, &ScintillaEditBase::updateUi, this, &EditorView::onUpdateUi);

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
    updateLineNumberMarginWidth();
}

void EditorView::updateLineNumberMarginWidth() {
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

    // 定义书签外观 (Notepad++ 经典蓝色圆圈标记)
    send(SCI_MARKERDEFINE, MARK_BOOKMARK, SC_MARK_CIRCLE);
    send(SCI_MARKERSETFORE, MARK_BOOKMARK, scRgb(0, 0, 180));
    send(SCI_MARKERSETBACK, MARK_BOOKMARK, scRgb(0, 165, 255));

    // 3. 代码折叠列 (Margin 2: Notepad++ 经典方块树 Box Tree 风格)
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

    updateEditorChromeStyles();
    updateLineNumberMarginWidth();
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
    size_t bom = Encoding::bomLength(base, len);
    send(SCI_CLEARALL);
    Encoding::decodeToUtf8Chunks(base + bom, len - bom, enc,
        [this](const char* d, size_t n) {
            if (n) sends(SCI_ADDTEXT, n, d);
        });

    // 解码后的 UTF-8 重新判定并规整换行（UTF-16 源文件的 LF/CR 只有在解码后才可见）。
    if (Encoding::isUtf16(enc)) {
        std::string decoded = currentTextUtf8();
        LineEnding decodedLe = DocumentBuffer::detectLineEnding(decoded);
        _buffer->setLineEnding(decodedLe);
        int dmode = (decodedLe == LineEnding::CRLF) ? SC_EOL_CRLF : (decodedLe == LineEnding::CR) ? SC_EOL_CR : SC_EOL_LF;
        send(SCI_SETEOLMODE, dmode);
    }
    // 经典 Mac(CR) 统一规整为内部一致的 CR EOL，保证保存往返一致。
    if (_buffer->getLineEnding() == LineEnding::CR) {
        send(SCI_CONVERTEOLS, SC_EOL_CR);
    }

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

void EditorView::restoreViewState(int caretPos, int selStart, int selEnd, int firstVisibleLine) {
    const sptr_t len = send(SCI_GETLENGTH);
    // session.xml 里的位置来自上一次运行，文件可能已被外部改小，必须先夹到合法区间。
    auto clampPos = [len](int pos) -> sptr_t {
        if (pos < 0) return -1;
        return (pos > len) ? len : static_cast<sptr_t>(pos);
    };

    const sptr_t caret = clampPos(caretPos);
    const sptr_t selFrom = clampPos(selStart);
    const sptr_t selTo = clampPos(selEnd);
    if (selFrom >= 0 && selTo >= 0 && selFrom != selTo) {
        send(SCI_SETSEL, static_cast<uptr_t>(selFrom), selTo);
    } else {
        send(SCI_GOTOPOS, static_cast<uptr_t>(caret < 0 ? 0 : caret));
    }
    send(SCI_SCROLLCARET);
    // 放在 SCROLLCARET 之后：否则滚动位置又会被“把光标带进视野”覆盖掉。
    if (firstVisibleLine > 0) {
        send(SCI_SETFIRSTVISIBLELINE, static_cast<uptr_t>(firstVisibleLine));
    }
}

bool EditorView::saveToFile(const QString& savePath) {
    QString targetPath = savePath.isEmpty() ? QString::fromStdString(_buffer->getFilePath()) : savePath;
    if (targetPath.isEmpty()) {
        return false;
    }

    QFile file(targetPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }

    // 先取得文档当前的 UTF-8 字节，再按缓冲区编码真实转码写盘（含 BOM）。
    const std::string enc = _buffer->getEncoding();
    const std::string bom = Encoding::bomForSave(enc);
    if (!bom.empty()) {
        file.write(bom.data(), static_cast<qint64>(bom.size()));
    }

    std::string utf8 = currentTextUtf8();
    Encoding::encodeFromUtf8Chunks(utf8.data(), utf8.size(), enc,
        [&](const char* d, size_t n) {
            file.write(d, static_cast<qint64>(n));
        });
    file.close();

    send(SCI_SETSAVEPOINT);
    _buffer->setFilePath(targetPath.toStdString());
    _buffer->setModified(false);
    emit documentModified(false);

    return true;
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
        send(SCI_SETILEXER, 0, reinterpret_cast<sptr_t>(pLexer));
    } else {
        send(SCI_SETILEXER, 0, 0);
    }

    // 3. 应用全局部件外观（行号、选区、折叠列等，严格跟随当前深浅色模式）
    updateEditorChromeStyles();
    updateLineNumberMarginWidth();

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
            // 对齐 Notepad++ Cpp/Java/JavaScript 词法器关键字规范
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

    // 启用代码折叠支持
    send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold"), reinterpret_cast<sptr_t>("1"));
    send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold.html"), reinterpret_cast<sptr_t>("1"));
    send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold.hypertext.comment"), reinterpret_cast<sptr_t>("1"));
    send(SCI_SETPROPERTY, reinterpret_cast<uptr_t>("fold.comment"), reinterpret_cast<sptr_t>("1"));

    // 7. 核心：重置整篇文档已有字符的样式字节，并全量强制重新染色
    send(SCI_CLEARDOCUMENTSTYLE);
    send(SCI_COLOURISE, 0, -1);
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

    updateLineNumberMarginWidth();
}

void EditorView::applyThemeStyles(const std::string& langName) {
    const auto* styles = StyleManager::instance().getStyles(langName);
    if (!styles) {
        styles = StyleManager::instance().getStyles("normal");
    }
    if (!styles) return;

    for (const auto& s : *styles) {
        if (s.fgColor != -1) {
            if (_isDarkMode && s.fgColor == 0x000000) {
                send(SCI_STYLESETFORE, s.styleID, scRgb(212, 212, 212));
            } else {
                send(SCI_STYLESETFORE, s.styleID, s.fgColor);
            }
        }
        if (!_isDarkMode && s.bgColor != -1) {
            send(SCI_STYLESETBACK, s.styleID, s.bgColor);
        }
        send(SCI_STYLESETBOLD, s.styleID, s.bold ? 1 : 0);
        send(SCI_STYLESETITALIC, s.styleID, s.italic ? 1 : 0);
    }

    if (_isDarkMode) {
        send(SCI_STYLESETBACK, STYLE_DEFAULT, scRgb(28, 28, 28));
        send(SCI_STYLESETFORE, STYLE_DEFAULT, scRgb(218, 218, 218));
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
    updateLineNumberMarginWidth();

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
// 语义照搬 Notepad++：列选区在 Scintilla 内部是“每行一个 selection”，
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
            for (sptr_t j = 0, k = cl.selLpos; j < cl.virtualCaret; ++j, ++k) {
                send(SCI_INSERTTEXT, k, reinterpret_cast<sptr_t>(" "));
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
    // repeat 至少为 1，否则下面生成序列的循环永不调数（同 Notepad++ 的保护）。
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
    if (static_cast<int>(updated) & static_cast<int>(Scintilla::Update::Selection)) {
        auto pos = send(SCI_GETCURRENTPOS);
        auto line = send(SCI_LINEFROMPOSITION, pos);
        auto col = send(SCI_GETCOLUMN, pos);
        emit cursorPositionChanged(static_cast<int>(line + 1), static_cast<int>(col + 1));
    }
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
    } else {
        lineStart = 0;
        lineEnd = static_cast<int>(send(SCI_GETLINECOUNT)) - 1;
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

    QString eol = (_buffer->getLineEnding() == LineEnding::CRLF) ? "\r\n" : "\n";
    QString joined = lines.join(eol);
    if (lineEnd < send(SCI_GETLINECOUNT) - 1) {
        joined += eol;
    }

    auto startPos = send(SCI_POSITIONFROMLINE, lineStart);
    auto endPos = (lineEnd < send(SCI_GETLINECOUNT) - 1) ? send(SCI_POSITIONFROMLINE, lineEnd + 1) : send(SCI_GETLENGTH);
    send(SCI_SETSEL, startPos, endPos);
    sends(SCI_REPLACESEL, 0, joined.toUtf8().constData());
}

void EditorView::removeDuplicateLines() {
    auto selStart = send(SCI_GETSELECTIONSTART);
    auto selEnd = send(SCI_GETSELECTIONEND);
    int lineStart = 0;
    int lineEnd = 0;
    if (selStart != selEnd) {
        lineStart = static_cast<int>(send(SCI_LINEFROMPOSITION, selStart));
        lineEnd = static_cast<int>(send(SCI_LINEFROMPOSITION, selEnd));
    } else {
        lineStart = 0;
        lineEnd = static_cast<int>(send(SCI_GETLINECOUNT)) - 1;
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

    QString eol = (_buffer->getLineEnding() == LineEnding::CRLF) ? "\r\n" : "\n";
    QString joined = uniqueLines.join(eol);
    if (lineEnd < send(SCI_GETLINECOUNT) - 1) {
        joined += eol;
    }

    auto startPos = send(SCI_POSITIONFROMLINE, lineStart);
    auto endPos = (lineEnd < send(SCI_GETLINECOUNT) - 1) ? send(SCI_POSITIONFROMLINE, lineEnd + 1) : send(SCI_GETLENGTH);
    send(SCI_SETSEL, startPos, endPos);
    sends(SCI_REPLACESEL, 0, joined.toUtf8().constData());
}

void EditorView::reverseLines() {
    auto selStart = send(SCI_GETSELECTIONSTART);
    auto selEnd = send(SCI_GETSELECTIONEND);
    int lineStart = 0;
    int lineEnd = 0;
    if (selStart != selEnd) {
        lineStart = static_cast<int>(send(SCI_LINEFROMPOSITION, selStart));
        lineEnd = static_cast<int>(send(SCI_LINEFROMPOSITION, selEnd));
    } else {
        lineStart = 0;
        lineEnd = static_cast<int>(send(SCI_GETLINECOUNT)) - 1;
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

    QString eol = (_buffer->getLineEnding() == LineEnding::CRLF) ? "\r\n" : "\n";
    QString joined = lines.join(eol);
    if (lineEnd < send(SCI_GETLINECOUNT) - 1) {
        joined += eol;
    }

    auto startPos = send(SCI_POSITIONFROMLINE, lineStart);
    auto endPos = (lineEnd < send(SCI_GETLINECOUNT) - 1) ? send(SCI_POSITIONFROMLINE, lineEnd + 1) : send(SCI_GETLENGTH);
    send(SCI_SETSEL, startPos, endPos);
    sends(SCI_REPLACESEL, 0, joined.toUtf8().constData());
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
    for (int i = 0; i < lineCount; ++i) {
        auto lineStart = send(SCI_POSITIONFROMLINE, i);
        auto lineEnd = send(SCI_GETLINEENDPOSITION, i);
        auto p = lineEnd;
        while (p > lineStart) {
            char c = static_cast<char>(send(SCI_GETCHARAT, p - 1));
            if (c == ' ' || c == '\t') {
                --p;
            } else {
                break;
            }
        }
        if (p < lineEnd) {
            send(SCI_DELETERANGE, p, lineEnd - p);
        }
    }
    send(SCI_ENDUNDOACTION);
}

void EditorView::trimLeadingSpaces() {
    send(SCI_BEGINUNDOACTION);
    int lineCount = static_cast<int>(send(SCI_GETLINECOUNT));
    for (int i = 0; i < lineCount; ++i) {
        auto lineStart = send(SCI_POSITIONFROMLINE, i);
        auto lineEnd = send(SCI_GETLINEENDPOSITION, i);
        auto p = lineStart;
        while (p < lineEnd) {
            char c = static_cast<char>(send(SCI_GETCHARAT, p));
            if (c == ' ' || c == '\t') {
                ++p;
            } else {
                break;
            }
        }
        if (p > lineStart) {
            send(SCI_DELETERANGE, lineStart, p - lineStart);
        }
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
    send(SCI_SETWRAPMODE, wrap ? SC_WRAP_WORD : SC_WRAP_NONE);
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
