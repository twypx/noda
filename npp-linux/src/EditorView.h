#pragma once

#include "ScintillaEditBase.h"
#include "DocumentBuffer.h"
#include <functional>
#include <memory>
#include <QString>

struct DocSummary {
    int lines{0};
    int characters{0};
    int charactersNoSpaces{0};
    int words{0};
    int bytes{0};
    int selCharacters{0};
    int selLines{0};
};

// 列编辑器里数字的“开头补齐”方式，与 Notepad++ 的 None / Zeros / Spaces 一一对应。
enum class ColumnPad { none, zeros, spaces };

class EditorView : public ScintillaEditBase {
    Q_OBJECT
public:
    explicit EditorView(QWidget* parent = nullptr);
    ~EditorView() override = default;

    bool loadFromFile(const QString& filePath, const std::string& forcedEnc = std::string());
    bool saveToFile(const QString& filePath = QString());

    // 只写一份按当前编码转码的副本，不改变本缓冲区的路径、savepoint 与修改标记。
    bool saveCopyToFile(const QString& filePath);

    // 最近一次写盘失败的具体原因（编码不支持 / 打不开 / 写盘失败 / 提交失败）；成功时为空。
    QString lastSaveError() const { return _lastSaveError; }

    // 以指定编码重新解码载入当前文件（用于“设定编码”重新解释字节）。
    bool reloadWithEncoding(const std::string& enc);

    // 为新建的空文档套用默认换行格式与编码（首选项 defaultEOL / defaultEncoding）。
    void applyDefaultDocFormat(LineEnding le, const std::string& enc);

    // 会话快照用：取整篇文本的 UTF-8 视图（备份写盘）/ 用备份内容直接填充缓冲区。
    // 不要把 setTextFromUtf8 当成"打开文件"用：它不动文件路径、编码与语言，
    // 这些由调用方按 session.xml 里记录的值回填，才能保证下次保存仍按原编码写回。
    std::string currentTextUtf8() const;
    void setTextFromUtf8(const std::string& utf8);
    // 用格式化结果整篇替换：单步可撤销，并把光标/视口按“非空白字符序号”映射回原逻辑位置。
    // 仅接受只改空白的结果；与当前内容相同则不做任何事。
    void applyFormattedText(const std::string& formattedUtf8);
    // 还原上次的光标/选区/滚动位置。firstDocLine 是折行前的文档行号，
    // xOffset 是横向滚动偏移（非自动换行时生效）。
    void restoreViewState(int caretPos, int selStart, int selEnd, int firstDocLine, int xOffset);

    // 应用“全局样式配置器”里的默认前景/背景色覆盖（-1 表示不覆盖）。
    void applyGlobalStyleOverride(int fg, int bg);

    std::shared_ptr<DocumentBuffer> buffer() const { return _buffer; }
    std::string currentLanguage() const { return _currentLang; }

    void applyLanguage(const std::string& langName);
    void applyThemeStyles(const std::string& langName);
    void setupMarkdownStyles(bool isDark);
    void setDarkMode(bool dark);
    void setEditorFont(const QString& fontName, int fontSize);
    QString getEditorFontName() const { return _fontName; }
    int getEditorFontSize() const { return _fontSize; }

    // 查找替换核心方法
    bool findNext(const QString& text, bool matchCase, bool wholeWord, bool regex);
    bool findPrev(const QString& text, bool matchCase, bool wholeWord, bool regex);
    bool replaceOne(const QString& findText, const QString& replaceText, bool matchCase, bool wholeWord, bool regex);
    int replaceAll(const QString& findText, const QString& replaceText, bool matchCase, bool wholeWord, bool regex);

    // 常用编辑命令
    void doUndo() { send(SCI_UNDO); }
    void doRedo() { send(SCI_REDO); }
    void doCut() { send(SCI_CUT); }
    void doCopy() { send(SCI_COPY); }
    void doPaste() { send(SCI_PASTE); }
    void doDelete() { send(SCI_CLEAR); }
    void doSelectAll() { send(SCI_SELECTALL); }

    // 大小写转换
    void toUpperCase();
    void toLowerCase();
    void toTitleCase();
    void invertCase();

    // 缩进操作
    void indent() { send(SCI_TAB); }
    void unindent() { send(SCI_BACKTAB); }

    // 行操作
    void duplicateCurrentLine();
    void cutCurrentLine();
    void deleteCurrentLine();
    void moveLineUp();
    void moveLineDown();
    void joinLines();
    void splitLines();
    void sortLines(bool ascending, bool ignoreCase);
    void removeDuplicateLines();
    void reverseLines();
    void insertEmptyLine(bool above);
    void goToLine(int line);

    // 列选模式（矩形选区）。三种入口与 Notepad++ 一致：
    //   1. 按住 Alt 拖动鼠标；2. Alt+Shift + 方向键；3. 下面的 beginOrEndSelect 用键盘两点定块。
    // 前两种 Scintilla 原生就支持（rectangularSelectionModifier 默认就是 Alt），无需额外代码；
    // 这里只提供第三种，以及列编辑器需要的逐行改写能力。
    void beginOrEndSelect(bool columnMode);

    // 列编辑器（Alt+C）：往列选区的每一行写入同样的文本 / 递增数字序列。
    // 没有列选区时退化为“从光标行一直写到文档结尾的光标所在列”，同 Notepad++。
    bool hasColumnSelection() const { return send(SCI_SELECTIONISRECTANGLE) != 0 || send(SCI_GETSELECTIONS) > 1; }
    void columnInsertText(const QString& text);
    void columnInsertNumber(qint64 initial, qint64 increment, int repeatTimes, int base,
                           bool hexUpper, ColumnPad pad);

    // 注释操作
    void toggleLineComment();
    void blockComment();
    void unblockComment();

    // 空白字符操作
    void trimTrailingSpaces();
    void trimLeadingSpaces();
    void trimBoth();
    void tabsToSpaces(int tabWidth = 4);
    void spacesToTabs(int tabWidth = 4);
    void removeEmptyLines(bool onlyBlank = false);

    // 括号匹配
    void goToMatchingBrace();
    void selectToMatchingBrace();

    // 格式标记 (Indicators)
    void markSelectedText(int styleIndex); // 1~5
    void clearMarks();

    // 书签操作
    void toggleBookmark();
    void nextBookmark();
    void prevBookmark();
    void clearAllBookmarks();
    void copyBookmarkedLines();
    void cutBookmarkedLines();
    void deleteBookmarkedLines();
    void deleteNonBookmarkedLines();
    void inverseBookmarks();

    // 视图与折叠控制
    void foldAll();
    void unfoldAll();
    void toggleCurrentFold();
    // 「折叠/展开当前层次」是幂等动作，不再共用一个 toggle：
    // 光标在折叠头或块内子行时，都先定位到所属折叠头再按目标状态决定是否切换。
    void foldCurrent();
    void expandCurrent();
    void foldLevel(int level);
    void unfoldLevel(int level);
    void setShowWhitespace(bool show);
    void setShowEOL(bool show);
    void setShowIndentGuides(bool show);
    void setShowLineNumbers(bool show);
    void updateLineNumberMarginWidth(bool force = false);
    int lineNumberMarginDigits() const { return _lastLineDigits; }
    void updateEditorChromeStyles();
    void setWrapMode(bool wrap);
    void zoomIn() { send(SCI_ZOOMIN); }
    void zoomOut() { send(SCI_ZOOMOUT); }
    void zoomReset() { send(SCI_SETZOOM, 0); }

    // 只读属性
    void setReadOnly(bool ro) { send(SCI_SETREADONLY, ro ? 1 : 0); }
    bool isReadOnly() const { return send(SCI_GETREADONLY) != 0; }

    // 换行符与编码转换
    void convertLineEndings(LineEnding targetLe);

    // 统计信息
    DocSummary getSummary() const;

signals:
    void documentModified(bool modified);
    void cursorPositionChanged(int line, int col);
    void lineEndingChanged(LineEnding le);
    void languageChanged(const QString& langId);

private slots:
    void onUpdateUi(Scintilla::Update updated);

private:
    // 一个列选区在 Scintilla 内部被展开成“每行一个选区”，列编辑器要逐行改写，
    // 所以先把这些行的落点收集起来；虚拟空格记录的是超出行尾的那部分列宽。
    struct ColumnLine {
        sptr_t selLpos = 0;        // 该行选区起点（字节位置）
        sptr_t selRpos = 0;        // 该行选区终点
        sptr_t virtualAnchor = 0;  // 起点侧虚拟空格数
        sptr_t virtualCaret = 0;   // 终点侧虚拟空格数
        bool l2r = true;           // 选取方向，还原选区时据此保持锚点/光标不翻转
        int order = -1;            // 原本的第几个 selection
        bool isValid() const { return order >= 0 && selLpos >= 0 && selLpos <= selRpos; }
    };

    // payload(idx) 给出第 idx 行要写入的内容（行序按文档位置排序后的下标）。
    using ColumnPayload = std::function<QString(int)>;

    QVector<ColumnLine> collectColumnLines() const;
    void applyColumnPayload(QVector<ColumnLine>& lines, const ColumnPayload& payload) const;
    void restoreColumnSelections(const QVector<ColumnLine>& lines) const;
    // 无列选区的退化路径：逐行在光标所在列写入，行不够长时先补空格对齐。
    void writeColumnPayloadToDocEnd(const ColumnPayload& payload) const;

    // 事务性写盘：BOM + 按编码真实转码 + commit。失败时 cancelWriting()，
    // 原文件保持完好，原因写进 _lastSaveError。saveToFile / saveCopyToFile 共用。
    bool writeEncodedFile(const QString& path, const std::string& utf8, const std::string& enc);

    sptr_t currentFoldHeader() const;
    void initEditorDefaults();
    std::string getSelectedOrWordText() const;
    void replaceSelectedText(const std::string& replacement);

    std::shared_ptr<DocumentBuffer> _buffer;
    QString _lastSaveError;
    bool _isDarkMode{false};
    bool _showLineNumbers{true};
    int _lastLineCount{-1};
    int _lastLineDigits{-1};
    std::string _currentLang{"normal"};
    QString _fontName{"Monospace"};
    int _fontSize{10};
    // “开始/结束 选取”的第一次调用只记下位置，第二次才真正拉出选区（同 Notepad++）。
    sptr_t _beginSelectPos{-1};
};
