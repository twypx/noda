#include "CompareWidget.h"
#include "EditorView.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QVBoxLayout>
#include <Scintilla.h>
#include <algorithm>
#include <string_view>
#include <utility>

namespace {

inline sptr_t scRgb(int r, int g, int b) {
    return static_cast<sptr_t>(r | (g << 8) | (b << 16));
}

} // namespace

CompareWidget::CompareWidget(EditorView* editorA, EditorView* editorB, bool isDark, QWidget* parent)
    : QWidget(parent)
    , _editorA(editorA)
    , _editorB(editorB)
    , _isDarkMode(isDark)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* optionsBar = new QWidget(this);
    auto* optionsLayout = new QHBoxLayout(optionsBar);
    optionsLayout->setContentsMargins(6, 3, 6, 3);
    optionsLayout->setSpacing(12);
    _ignoreWhitespaceCheck = new QCheckBox(tr("忽略空白"), optionsBar);
    _ignoreCaseCheck = new QCheckBox(tr("忽略大小写"), optionsBar);
    optionsLayout->addWidget(_ignoreWhitespaceCheck);
    optionsLayout->addWidget(_ignoreCaseCheck);
    optionsLayout->addStretch(1);
    layout->addWidget(optionsBar);

    _splitter = new QSplitter(Qt::Horizontal, this);
    _splitter->addWidget(_editorA);
    _splitter->addWidget(_editorB);
    _splitter->setSizes({500, 500});
    layout->addWidget(_splitter);

    connect(_ignoreWhitespaceCheck, &QCheckBox::toggled, this, [this](bool checked) {
        _options.ignoreWhitespace = checked;
        refreshDiff();
    });
    connect(_ignoreCaseCheck, &QCheckBox::toggled, this, [this](bool checked) {
        _options.ignoreCase = checked;
        refreshDiff();
    });

    setupScintillaStyles(_editorA);
    setupScintillaStyles(_editorB);

    connectScrollSignals();
    refreshDiff();
}

CompareWidget::~CompareWidget()
{
    clearDiff();
    if (_editorA) {
        _editorA->setParent(nullptr);
    }
    if (_editorB) {
        _editorB->setParent(nullptr);
    }
}

void CompareWidget::setDarkMode(bool dark)
{
    if (_isDarkMode == dark) return;
    _isDarkMode = dark;
    setupScintillaStyles(_editorA);
    setupScintillaStyles(_editorB);
    applyDiffVisuals();
}

void CompareWidget::setupScintillaStyles(EditorView* ed)
{
    if (!ed) return;

    // 确保 Marker 边栏可见
    ed->send(SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
    ed->send(SCI_SETMARGINMASKN, 1, ed->send(SCI_GETMARGINMASKN, 1) |
             (1 << MARK_DIFF_ADDED) | (1 << MARK_DIFF_REMOVED) | (1 << MARK_DIFF_MODIFIED));
    ed->send(SCI_SETMARGINWIDTHN, 1, 16);

    // Marker 定义
    ed->send(SCI_MARKERDEFINE, MARK_DIFF_ADDED, SC_MARK_PLUS);
    ed->send(SCI_MARKERDEFINE, MARK_DIFF_REMOVED, SC_MARK_MINUS);
    ed->send(SCI_MARKERDEFINE, MARK_DIFF_MODIFIED, SC_MARK_ROUNDRECT);

    if (_isDarkMode) {
        ed->send(SCI_MARKERSETFORE, MARK_DIFF_ADDED, scRgb(80, 220, 80));
        ed->send(SCI_MARKERSETBACK, MARK_DIFF_ADDED, scRgb(25, 75, 30));
        ed->send(SCI_MARKERSETBACKSELECTED, MARK_DIFF_ADDED, scRgb(35, 100, 40));

        ed->send(SCI_MARKERSETFORE, MARK_DIFF_REMOVED, scRgb(240, 80, 80));
        ed->send(SCI_MARKERSETBACK, MARK_DIFF_REMOVED, scRgb(85, 25, 25));
        ed->send(SCI_MARKERSETBACKSELECTED, MARK_DIFF_REMOVED, scRgb(110, 35, 35));

        ed->send(SCI_MARKERSETFORE, MARK_DIFF_MODIFIED, scRgb(240, 180, 60));
        ed->send(SCI_MARKERSETBACK, MARK_DIFF_MODIFIED, scRgb(80, 65, 20));
        ed->send(SCI_MARKERSETBACKSELECTED, MARK_DIFF_MODIFIED, scRgb(105, 85, 25));
    } else {
        ed->send(SCI_MARKERSETFORE, MARK_DIFF_ADDED, scRgb(34, 139, 34));
        ed->send(SCI_MARKERSETBACK, MARK_DIFF_ADDED, scRgb(215, 250, 215));
        ed->send(SCI_MARKERSETBACKSELECTED, MARK_DIFF_ADDED, scRgb(190, 240, 190));

        ed->send(SCI_MARKERSETFORE, MARK_DIFF_REMOVED, scRgb(178, 34, 34));
        ed->send(SCI_MARKERSETBACK, MARK_DIFF_REMOVED, scRgb(255, 225, 225));
        ed->send(SCI_MARKERSETBACKSELECTED, MARK_DIFF_REMOVED, scRgb(255, 200, 200));

        ed->send(SCI_MARKERSETFORE, MARK_DIFF_MODIFIED, scRgb(200, 120, 0));
        ed->send(SCI_MARKERSETBACK, MARK_DIFF_MODIFIED, scRgb(255, 245, 200));
        ed->send(SCI_MARKERSETBACKSELECTED, MARK_DIFF_MODIFIED, scRgb(255, 235, 175));
    }

    // Indicator 定义（行内字符高亮）
    ed->send(SCI_INDICSETSTYLE, INDIC_DIFF_CHAR, INDIC_ROUNDBOX);
    ed->send(SCI_INDICSETFORE, INDIC_DIFF_CHAR, _isDarkMode ? scRgb(255, 215, 0) : scRgb(220, 120, 0));
    ed->send(SCI_INDICSETALPHA, INDIC_DIFF_CHAR, 90);
    ed->send(SCI_INDICSETOUTLINEALPHA, INDIC_DIFF_CHAR, 160);

    // Annotation 样式
    ed->send(SCI_ANNOTATIONSETVISIBLE, ANNOTATION_BOXED);
}

void CompareWidget::clearScintillaVisuals(EditorView* ed)
{
    if (!ed) return;

    ed->send(SCI_MARKERDELETEALL, MARK_DIFF_ADDED);
    ed->send(SCI_MARKERDELETEALL, MARK_DIFF_REMOVED);
    ed->send(SCI_MARKERDELETEALL, MARK_DIFF_MODIFIED);

    ed->send(SCI_ANNOTATIONCLEARALL);
    ed->send(SCI_ANNOTATIONSETVISIBLE, ANNOTATION_HIDDEN);

    const auto len = ed->send(SCI_GETLENGTH);
    if (len > 0) {
        ed->send(SCI_SETINDICATORCURRENT, INDIC_DIFF_CHAR);
        ed->send(SCI_INDICATORCLEARRANGE, 0, len);
    }
}

void CompareWidget::clearDiff()
{
    clearScintillaVisuals(_editorA);
    clearScintillaVisuals(_editorB);
    _diffLineIndices.clear();
    _currentDiffIndex = -1;
}

void CompareWidget::refreshDiff()
{
    clearDiff();
    if (!_editorA || !_editorB) return;

    // 零拷贝：直接对 Scintilla 内部 UTF-8 缓冲做 diff，避免整份文本拷贝。
    const std::string_view textA = _editorA->currentTextView();
    const std::string_view textB = _editorB->currentTextView();

    _diffResult = Diff::DiffEngine::computeDiff(textA, textB, _options);

    const int linesA = static_cast<int>(_editorA->send(SCI_GETLINECOUNT));
    const int linesB = static_cast<int>(_editorB->send(SCI_GETLINECOUNT));

    _alignment = Diff::DiffEngine::computeAlignment(_diffResult, linesA, linesB);

    applyDiffVisuals();

    if (!_diffLineIndices.empty()) {
        _currentDiffIndex = 0;
        emit diffNavigated(1, static_cast<int>(_diffLineIndices.size()));
    } else {
        emit diffNavigated(0, 0);
    }
}

void CompareWidget::applyDiffVisuals()
{
    if (!_editorA || !_editorB) return;

    // 幂等：先清掉上一次的 marker / annotation / indicator（主题切换会重入本函数）。
    clearScintillaVisuals(_editorA);
    clearScintillaVisuals(_editorB);

    // 1. 注入 Marker 与 Indicator
    for (const auto& diff : _diffResult.lines) {
        if (diff.type == Diff::ChangeType::Added) {
            if (diff.lineB >= 0) {
                _editorB->send(SCI_MARKERADD, diff.lineB, MARK_DIFF_ADDED);
            }
        } else if (diff.type == Diff::ChangeType::Removed) {
            if (diff.lineA >= 0) {
                _editorA->send(SCI_MARKERADD, diff.lineA, MARK_DIFF_REMOVED);
            }
        } else if (diff.type == Diff::ChangeType::Modified) {
            if (diff.lineA >= 0) {
                _editorA->send(SCI_MARKERADD, diff.lineA, MARK_DIFF_MODIFIED);
                const auto lineStart = _editorA->send(SCI_POSITIONFROMLINE, diff.lineA);
                _editorA->send(SCI_SETINDICATORCURRENT, INDIC_DIFF_CHAR);
                for (const auto& rng : diff.intraA) {
                    _editorA->send(SCI_INDICATORFILLRANGE, lineStart + rng.start, rng.length);
                }
            }
            if (diff.lineB >= 0) {
                _editorB->send(SCI_MARKERADD, diff.lineB, MARK_DIFF_MODIFIED);
                const auto lineStart = _editorB->send(SCI_POSITIONFROMLINE, diff.lineB);
                _editorB->send(SCI_SETINDICATORCURRENT, INDIC_DIFF_CHAR);
                for (const auto& rng : diff.intraB) {
                    _editorB->send(SCI_INDICATORFILLRANGE, lineStart + rng.start, rng.length);
                }
            }
        }
    }

    // 2. Annotation 视觉占位行：alignment 行号单调递增，直接线性累积即可（无需 std::map）。
    //    前导占位（首行之前）无法用「某行下方的 annotation」表达，跳过——不影响后续行对齐。
    std::vector<std::pair<int, int>> blanksA;
    std::vector<std::pair<int, int>> blanksB;
    auto appendBlank = [](std::vector<std::pair<int, int>>& out, int line) {
        if (line < 0) return;
        if (!out.empty() && out.back().first == line) {
            ++out.back().second;
        } else {
            out.push_back({line, 1});
        }
    };

    int lastA = -1;
    int lastB = -1;
    _diffLineIndices.clear();

    for (size_t i = 0; i < _alignment.size(); ++i) {
        const auto& item = _alignment[i];
        if (item.type != Diff::ChangeType::Unchanged) {
            _diffLineIndices.push_back(static_cast<int>(i));
        }
        if (item.lineA >= 0) {
            lastA = item.lineA;
        } else {
            appendBlank(blanksA, lastA);
        }
        if (item.lineB >= 0) {
            lastB = item.lineB;
        } else {
            appendBlank(blanksB, lastB);
        }
    }

    for (const auto& [line, count] : blanksA) {
        std::string anno(static_cast<size_t>(count), '\n');
        _editorA->send(SCI_ANNOTATIONSETTEXT, line, reinterpret_cast<sptr_t>(anno.c_str()));
    }
    for (const auto& [line, count] : blanksB) {
        std::string anno(static_cast<size_t>(count), '\n');
        _editorB->send(SCI_ANNOTATIONSETTEXT, line, reinterpret_cast<sptr_t>(anno.c_str()));
    }

    _editorA->send(SCI_ANNOTATIONSETVISIBLE, ANNOTATION_BOXED);
    _editorB->send(SCI_ANNOTATIONSETVISIBLE, ANNOTATION_BOXED);
}

void CompareWidget::connectScrollSignals()
{
    if (!_editorA || !_editorB) return;

    auto* vsbA = _editorA->verticalScrollBar();
    auto* vsbB = _editorB->verticalScrollBar();
    auto* hsbA = _editorA->horizontalScrollBar();
    auto* hsbB = _editorB->horizontalScrollBar();

    // 垂直同步滚动：使用 QSignalBlocker 彻底阻断回环
    connect(vsbA, &QScrollBar::valueChanged, this, [this](int val) {
        if (!_editorB) return;
        QSignalBlocker blocker(_editorB->verticalScrollBar());
        _editorB->verticalScrollBar()->setValue(val);
    });

    connect(vsbB, &QScrollBar::valueChanged, this, [this](int val) {
        if (!_editorA) return;
        QSignalBlocker blocker(_editorA->verticalScrollBar());
        _editorA->verticalScrollBar()->setValue(val);
    });

    // 水平同步滚动：使用 QSignalBlocker 彻底阻断回环
    connect(hsbA, &QScrollBar::valueChanged, this, [this](int val) {
        if (!_editorB) return;
        QSignalBlocker blocker(_editorB->horizontalScrollBar());
        _editorB->horizontalScrollBar()->setValue(val);
    });

    connect(hsbB, &QScrollBar::valueChanged, this, [this](int val) {
        if (!_editorA) return;
        QSignalBlocker blocker(_editorA->horizontalScrollBar());
        _editorA->horizontalScrollBar()->setValue(val);
    });
}

void CompareWidget::nextDiff()
{
    if (_diffLineIndices.empty()) return;
    _currentDiffIndex = (_currentDiffIndex + 1) % static_cast<int>(_diffLineIndices.size());

    int alignIdx = _diffLineIndices[_currentDiffIndex];
    if (alignIdx >= 0 && alignIdx < static_cast<int>(_alignment.size())) {
        const auto& item = _alignment[alignIdx];
        if (item.lineA >= 0) {
            _editorA->send(SCI_ENSUREVISIBLE, item.lineA);
            _editorA->send(SCI_GOTOLINE, item.lineA);
        }
        if (item.lineB >= 0) {
            _editorB->send(SCI_ENSUREVISIBLE, item.lineB);
            _editorB->send(SCI_GOTOLINE, item.lineB);
        }
    }
    emit diffNavigated(_currentDiffIndex + 1, static_cast<int>(_diffLineIndices.size()));
}

void CompareWidget::prevDiff()
{
    if (_diffLineIndices.empty()) return;
    _currentDiffIndex = (_currentDiffIndex - 1 + static_cast<int>(_diffLineIndices.size())) %
                        static_cast<int>(_diffLineIndices.size());

    int alignIdx = _diffLineIndices[_currentDiffIndex];
    if (alignIdx >= 0 && alignIdx < static_cast<int>(_alignment.size())) {
        const auto& item = _alignment[alignIdx];
        if (item.lineA >= 0) {
            _editorA->send(SCI_ENSUREVISIBLE, item.lineA);
            _editorA->send(SCI_GOTOLINE, item.lineA);
        }
        if (item.lineB >= 0) {
            _editorB->send(SCI_ENSUREVISIBLE, item.lineB);
            _editorB->send(SCI_GOTOLINE, item.lineB);
        }
    }
    emit diffNavigated(_currentDiffIndex + 1, static_cast<int>(_diffLineIndices.size()));
}
