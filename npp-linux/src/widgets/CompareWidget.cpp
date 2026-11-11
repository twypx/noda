#include "CompareWidget.h"
#include "EditorView.h"

#include <QCheckBox>
#include <QPushButton>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrent>
#include <Scintilla.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <utility>

namespace {

inline sptr_t scRgb(int r, int g, int b) {
    return static_cast<sptr_t>(r | (g << 8) | (b << 16));
}

// 小于该阈值走同步路径（零延迟、测试确定性）；更大文本交给工作线程，UI 不阻塞。
constexpr size_t kAsyncThreshold = 256 * 1024;

} // namespace

CompareWidget::CompareWidget(EditorView* editorA, EditorView* editorB, bool isDark, QWidget* parent)
    : QWidget(parent)
    , _editorA(editorA)
    , _editorB(editorB)
    , _isDarkMode(isDark)
{
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* mainColumn = new QVBoxLayout();
    mainColumn->setContentsMargins(0, 0, 0, 0);
    mainColumn->setSpacing(0);

    auto* optionsBar = new QWidget(this);
    auto* optionsLayout = new QHBoxLayout(optionsBar);
    optionsLayout->setContentsMargins(6, 3, 6, 3);
    optionsLayout->setSpacing(12);
    _ignoreWhitespaceCheck = new QCheckBox(tr("忽略空白"), optionsBar);
    _ignoreCaseCheck = new QCheckBox(tr("忽略大小写"), optionsBar);
    optionsLayout->addWidget(_ignoreWhitespaceCheck);
    optionsLayout->addWidget(_ignoreCaseCheck);
    optionsLayout->addStretch(1);

    // 差异导航与退出按钮只属于比对视图，随 CompareWidget 一起创建/销毁，
    // 因此「只有进入比对模式后才出现」由控件生命周期天然保证。
    _nextDiffButton = new QPushButton(tr("下一个差异"), optionsBar);
    _nextDiffButton->setObjectName("btnNextDiff");
    _nextDiffButton->setEnabled(false);
    _prevDiffButton = new QPushButton(tr("上一个差异"), optionsBar);
    _prevDiffButton->setObjectName("btnPrevDiff");
    _prevDiffButton->setEnabled(false);
    _closeCompareButton = new QPushButton(tr("关闭对比"), optionsBar);
    _closeCompareButton->setObjectName("btnCloseCompare");
    optionsLayout->addWidget(_nextDiffButton);
    optionsLayout->addWidget(_prevDiffButton);
    optionsLayout->addWidget(_closeCompareButton);
    mainColumn->addWidget(optionsBar);

    _splitter = new QSplitter(Qt::Horizontal, this);
    _splitter->addWidget(_editorA);
    _splitter->addWidget(_editorB);
    _splitter->setSizes({500, 500});
    mainColumn->addWidget(_splitter, 1);

    root->addLayout(mainColumn, 1);
    _miniMap = new DiffMiniMap(this);
    _miniMap->setDarkMode(isDark);
    root->addWidget(_miniMap);

    _watcher = new QFutureWatcher<Diff::DiffResult>(this);
    connect(_watcher, &QFutureWatcher<Diff::DiffResult>::finished,
            this, &CompareWidget::onDiffFinished);
    connect(_miniMap, &DiffMiniMap::navigateTo,
            this, &CompareWidget::navigateToAlignIndex);

    connect(_ignoreWhitespaceCheck, &QCheckBox::toggled, this, [this](bool checked) {
        _options.ignoreWhitespace = checked;
        refreshDiff();
    });
    connect(_ignoreCaseCheck, &QCheckBox::toggled, this, [this](bool checked) {
        _options.ignoreCase = checked;
        refreshDiff();
    });
    connect(_nextDiffButton, &QPushButton::clicked, this, [this]() { nextDiff(); });
    connect(_prevDiffButton, &QPushButton::clicked, this, [this]() { prevDiff(); });
    connect(_closeCompareButton, &QPushButton::clicked, this, [this]() {
        emit closeCompareRequested();
    });

    setupScintillaStyles(_editorA);
    setupScintillaStyles(_editorB);

    connectScrollSignals();
    refreshDiff();
}

CompareWidget::~CompareWidget()
{
    // 异步任务不捕获 this，但 finished 回调会访问成员：析构前必须确保不再有在途回调。
    if (_watcher && _computing) {
        _watcher->cancel();
        _watcher->waitForFinished();
    }
    clearDiff();
    if (_editorA) _editorA->setParent(nullptr);
    if (_editorB) _editorB->setParent(nullptr);
}

void CompareWidget::setDarkMode(bool dark)
{
    if (_isDarkMode == dark) return;
    _isDarkMode = dark;
    setupScintillaStyles(_editorA);
    setupScintillaStyles(_editorB);
    _miniMap->setDarkMode(dark);
    applyDiffVisuals();
}

void CompareWidget::setupScintillaStyles(EditorView* ed)
{
    if (!ed) return;

    // 确保 Marker 边栏可见（保留已有书签掩码，再叠加比对标记）。
    ed->send(SCI_SETMARGINTYPEN, 1, SC_MARGIN_SYMBOL);
    ed->send(SCI_SETMARGINMASKN, 1, ed->send(SCI_GETMARGINMASKN, 1) |
             (1 << MARK_DIFF_ADDED) | (1 << MARK_DIFF_REMOVED) |
             (1 << MARK_DIFF_MODIFIED) | (1 << MARK_DIFF_MOVED));
    ed->send(SCI_SETMARGINWIDTHN, 1, 16);

    ed->send(SCI_MARKERDEFINE, MARK_DIFF_ADDED, SC_MARK_PLUS);
    ed->send(SCI_MARKERDEFINE, MARK_DIFF_REMOVED, SC_MARK_MINUS);
    ed->send(SCI_MARKERDEFINE, MARK_DIFF_MODIFIED, SC_MARK_ROUNDRECT);
    ed->send(SCI_MARKERDEFINE, MARK_DIFF_MOVED, SC_MARK_ARROW);

    if (_isDarkMode) {
        ed->send(SCI_MARKERSETFORE, MARK_DIFF_ADDED, scRgb(80, 220, 80));
        ed->send(SCI_MARKERSETBACK, MARK_DIFF_ADDED, scRgb(25, 75, 30));
        ed->send(SCI_MARKERSETFORE, MARK_DIFF_REMOVED, scRgb(240, 80, 80));
        ed->send(SCI_MARKERSETBACK, MARK_DIFF_REMOVED, scRgb(85, 25, 25));
        ed->send(SCI_MARKERSETFORE, MARK_DIFF_MODIFIED, scRgb(240, 180, 60));
        ed->send(SCI_MARKERSETBACK, MARK_DIFF_MODIFIED, scRgb(80, 65, 20));
        ed->send(SCI_MARKERSETFORE, MARK_DIFF_MOVED, scRgb(170, 130, 240));
        ed->send(SCI_MARKERSETBACK, MARK_DIFF_MOVED, scRgb(60, 45, 95));
    } else {
        ed->send(SCI_MARKERSETFORE, MARK_DIFF_ADDED, scRgb(34, 139, 34));
        ed->send(SCI_MARKERSETBACK, MARK_DIFF_ADDED, scRgb(215, 250, 215));
        ed->send(SCI_MARKERSETFORE, MARK_DIFF_REMOVED, scRgb(178, 34, 34));
        ed->send(SCI_MARKERSETBACK, MARK_DIFF_REMOVED, scRgb(255, 225, 225));
        ed->send(SCI_MARKERSETFORE, MARK_DIFF_MODIFIED, scRgb(200, 120, 0));
        ed->send(SCI_MARKERSETBACK, MARK_DIFF_MODIFIED, scRgb(255, 245, 200));
        ed->send(SCI_MARKERSETFORE, MARK_DIFF_MOVED, scRgb(120, 70, 200));
        ed->send(SCI_MARKERSETBACK, MARK_DIFF_MOVED, scRgb(235, 225, 255));
    }

    ed->send(SCI_INDICSETSTYLE, INDIC_DIFF_CHAR, INDIC_ROUNDBOX);
    ed->send(SCI_INDICSETFORE, INDIC_DIFF_CHAR, _isDarkMode ? scRgb(255, 215, 0) : scRgb(220, 120, 0));
    ed->send(SCI_INDICSETALPHA, INDIC_DIFF_CHAR, 90);
    ed->send(SCI_INDICSETOUTLINEALPHA, INDIC_DIFF_CHAR, 160);

    ed->send(SCI_ANNOTATIONSETVISIBLE, ANNOTATION_BOXED);
}

void CompareWidget::clearScintillaVisuals(EditorView* ed)
{
    if (!ed) return;
    for (int marker : {MARK_DIFF_ADDED, MARK_DIFF_REMOVED, MARK_DIFF_MODIFIED, MARK_DIFF_MOVED}) {
        ed->send(SCI_MARKERDELETEALL, marker);
    }
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
    ++_generation; // 使在途异步结果失效
    _computing = false;
    clearDiff();
    if (!_editorA || !_editorB) return;

    // 工作线程不能读 Scintilla 内部缓冲（主线程可能修改），这里做一次拷贝；
    // 小文本走同步路径，连这次拷贝也省掉（直接 string_view）。
    const std::string_view viewA = _editorA->currentTextView();
    const std::string_view viewB = _editorB->currentTextView();
    const Diff::DiffOptions opt = _options;

    if (viewA.size() + viewB.size() <= kAsyncThreshold) {
        publishResult(Diff::DiffEngine::computeDiff(viewA, viewB, opt));
        return;
    }

    const std::string textA(viewA);
    const std::string textB(viewB);
    _computing = true;
    emit diffComputing(true);
    _watcher->setFuture(QtConcurrent::run(
        [textA = std::move(textA), textB = std::move(textB), opt]() -> Diff::DiffResult {
            return Diff::DiffEngine::computeDiff(textA, textB, opt);
        }));
}

void CompareWidget::onDiffFinished()
{
    if (!_computing || _watcher->isCanceled()) return;
    _computing = false;
    emit diffComputing(false);
    publishResult(_watcher->result());
}

void CompareWidget::publishResult(Diff::DiffResult result)
{
    _diffResult = std::move(result);
    const int linesA = static_cast<int>(_editorA ? _editorA->send(SCI_GETLINECOUNT) : 0);
    const int linesB = static_cast<int>(_editorB ? _editorB->send(SCI_GETLINECOUNT) : 0);
    _alignment = Diff::DiffEngine::computeAlignment(_diffResult, linesA, linesB);

    applyDiffVisuals();
    _miniMap->setAlignment(_alignment);

    // 无差异时禁用导航按钮；异步比对完成后同样会走到这里刷新状态。
    const bool hasDiff = !_diffLineIndices.empty();
    _nextDiffButton->setEnabled(hasDiff);
    _prevDiffButton->setEnabled(hasDiff);

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

    for (const auto& diff : _diffResult.lines) {
        if (diff.consumed) continue;
        if (diff.type == Diff::ChangeType::Added) {
            if (diff.lineB >= 0) _editorB->send(SCI_MARKERADD, diff.lineB, MARK_DIFF_ADDED);
        } else if (diff.type == Diff::ChangeType::Removed) {
            if (diff.lineA >= 0) _editorA->send(SCI_MARKERADD, diff.lineA, MARK_DIFF_REMOVED);
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
        } else if (diff.type == Diff::ChangeType::Moved) {
            if (diff.lineA >= 0) _editorA->send(SCI_MARKERADD, diff.lineA, MARK_DIFF_MOVED);
            if (diff.lineB >= 0) _editorB->send(SCI_MARKERADD, diff.lineB, MARK_DIFF_MOVED);
        }
    }

    // Annotation 视觉占位行：alignment 行号单调递增，线性累积（无需 std::map）。
    // 首行之前的占位无法用「某行下方的 annotation」表达，跳过。
    std::vector<std::pair<int, int>> blanksA;
    std::vector<std::pair<int, int>> blanksB;
    auto appendBlank = [](std::vector<std::pair<int, int>>& out, int line) {
        if (line < 0) return;
        if (!out.empty() && out.back().first == line) ++out.back().second;
        else out.push_back({line, 1});
    };

    int lastA = -1;
    int lastB = -1;
    _diffLineIndices.clear();

    for (size_t i = 0; i < _alignment.size(); ++i) {
        const auto& item = _alignment[i];
        if (item.type != Diff::ChangeType::Unchanged) {
            _diffLineIndices.push_back(static_cast<int>(i));
        }
        if (item.lineA >= 0) lastA = item.lineA;
        else appendBlank(blanksA, lastA);
        if (item.lineB >= 0) lastB = item.lineB;
        else appendBlank(blanksB, lastB);
    }

    for (const auto& [line, count] : blanksA) {
        const std::string anno(static_cast<size_t>(count), '\n');
        _editorA->send(SCI_ANNOTATIONSETTEXT, line, reinterpret_cast<sptr_t>(anno.c_str()));
    }
    for (const auto& [line, count] : blanksB) {
        const std::string anno(static_cast<size_t>(count), '\n');
        _editorB->send(SCI_ANNOTATIONSETTEXT, line, reinterpret_cast<sptr_t>(anno.c_str()));
    }

    _editorA->send(SCI_ANNOTATIONSETVISIBLE, ANNOTATION_BOXED);
    _editorB->send(SCI_ANNOTATIONSETVISIBLE, ANNOTATION_BOXED);
}

// 按 alignment 把文档行号互相映射：占位行不会产生新的文档行号，
// 因此 A 的第 N 行应与「同对齐行」上的 B 行一起滚动，而不是照搬滚动条 value。
int CompareWidget::alignedLineBForLineA(int lineA) const {
    int lastB = -1;
    for (const auto& item : _alignment) {
        if (item.lineB >= 0) lastB = item.lineB;
        if (item.lineA >= lineA) return item.lineB >= 0 ? item.lineB : lastB;
    }
    return lastB;
}

int CompareWidget::alignedLineAForLineB(int lineB) const {
    int lastA = -1;
    for (const auto& item : _alignment) {
        if (item.lineA >= 0) lastA = item.lineA;
        if (item.lineB >= lineB) return item.lineA >= 0 ? item.lineA : lastA;
    }
    return lastA;
}

void CompareWidget::connectScrollSignals()
{
    if (!_editorA || !_editorB) return;

    auto* vsbA = _editorA->verticalScrollBar();
    auto* vsbB = _editorB->verticalScrollBar();
    auto* hsbA = _editorA->horizontalScrollBar();
    auto* hsbB = _editorB->horizontalScrollBar();

    connect(vsbA, &QScrollBar::valueChanged, this, [this](int) {
        if (!_isSyncingScroll) syncVerticalFromA();
    });
    connect(vsbB, &QScrollBar::valueChanged, this, [this](int) {
        if (!_isSyncingScroll) syncVerticalFromB();
    });

    connect(hsbA, &QScrollBar::valueChanged, this, [this](int val) {
        if (_isSyncingScroll || !_editorB) return;
        _isSyncingScroll = true;
        QSignalBlocker blocker(_editorB->horizontalScrollBar());
        _editorB->horizontalScrollBar()->setValue(val);
        _isSyncingScroll = false;
    });
    connect(hsbB, &QScrollBar::valueChanged, this, [this](int val) {
        if (_isSyncingScroll || !_editorA) return;
        _isSyncingScroll = true;
        QSignalBlocker blocker(_editorA->horizontalScrollBar());
        _editorA->horizontalScrollBar()->setValue(val);
        _isSyncingScroll = false;
    });
}

void CompareWidget::syncVerticalFromA()
{
    if (_isSyncingScroll || !_editorA || !_editorB || _alignment.empty()) return;
    const int topA = static_cast<int>(_editorA->send(SCI_GETFIRSTVISIBLELINE));
    const int topB = alignedLineBForLineA(topA);
    if (topB < 0) return;
    _isSyncingScroll = true;
    QSignalBlocker blocker(_editorB->verticalScrollBar());
    _editorB->send(SCI_SETFIRSTVISIBLELINE, topB);
    _isSyncingScroll = false;
}

void CompareWidget::syncVerticalFromB()
{
    if (_isSyncingScroll || !_editorA || !_editorB || _alignment.empty()) return;
    const int topB = static_cast<int>(_editorB->send(SCI_GETFIRSTVISIBLELINE));
    const int topA = alignedLineAForLineB(topB);
    if (topA < 0) return;
    _isSyncingScroll = true;
    QSignalBlocker blocker(_editorA->verticalScrollBar());
    _editorA->send(SCI_SETFIRSTVISIBLELINE, topA);
    _isSyncingScroll = false;
}

void CompareWidget::gotoAlignIndex(int alignIndex)
{
    if (alignIndex < 0 || alignIndex >= static_cast<int>(_alignment.size())) return;
    const auto& item = _alignment[static_cast<size_t>(alignIndex)];
    if (_editorA && item.lineA >= 0) {
        _editorA->send(SCI_ENSUREVISIBLE, item.lineA);
        _editorA->send(SCI_GOTOLINE, item.lineA);
    }
    if (_editorB && item.lineB >= 0) {
        _editorB->send(SCI_ENSUREVISIBLE, item.lineB);
        _editorB->send(SCI_GOTOLINE, item.lineB);
    }
}

void CompareWidget::navigateToAlignIndex(int alignIndex)
{
    if (_diffLineIndices.empty()) return;
    const auto it = std::lower_bound(_diffLineIndices.begin(), _diffLineIndices.end(), alignIndex);
    int pos = static_cast<int>(it - _diffLineIndices.begin());
    if (pos > 0 && (it == _diffLineIndices.end() ||
                    std::abs(_diffLineIndices[static_cast<size_t>(pos - 1)] - alignIndex)
                        <= std::abs(_diffLineIndices[static_cast<size_t>(std::min(pos, static_cast<int>(_diffLineIndices.size()) - 1))] - alignIndex))) {
        --pos;
    }
    _currentDiffIndex = std::clamp(pos, 0, static_cast<int>(_diffLineIndices.size()) - 1);
    gotoAlignIndex(_diffLineIndices[static_cast<size_t>(_currentDiffIndex)]);
    emit diffNavigated(_currentDiffIndex + 1, static_cast<int>(_diffLineIndices.size()));
}

void CompareWidget::nextDiff()
{
    if (_diffLineIndices.empty()) return;
    _currentDiffIndex = (_currentDiffIndex + 1) % static_cast<int>(_diffLineIndices.size());
    gotoAlignIndex(_diffLineIndices[static_cast<size_t>(_currentDiffIndex)]);
    emit diffNavigated(_currentDiffIndex + 1, static_cast<int>(_diffLineIndices.size()));
}

void CompareWidget::prevDiff()
{
    if (_diffLineIndices.empty()) return;
    _currentDiffIndex = (_currentDiffIndex - 1 + static_cast<int>(_diffLineIndices.size())) %
                        static_cast<int>(_diffLineIndices.size());
    gotoAlignIndex(_diffLineIndices[static_cast<size_t>(_currentDiffIndex)]);
    emit diffNavigated(_currentDiffIndex + 1, static_cast<int>(_diffLineIndices.size()));
}
