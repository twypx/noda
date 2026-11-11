#pragma once

#include <QWidget>
#include <QSplitter>
#include <QFutureWatcher>
#include <vector>
#include "utils/DiffEngine.h"
#include "widgets/DiffMiniMap.h"

class EditorView;
class QCheckBox;
class QPushButton;

class CompareWidget : public QWidget {
    Q_OBJECT
public:
    static constexpr int MARK_DIFF_ADDED = 20;
    static constexpr int MARK_DIFF_REMOVED = 21;
    static constexpr int MARK_DIFF_MODIFIED = 22;
    static constexpr int MARK_DIFF_MOVED = 23;
    static constexpr int INDIC_DIFF_CHAR = 25;

    explicit CompareWidget(EditorView* editorA, EditorView* editorB, bool isDark = false, QWidget* parent = nullptr);
    ~CompareWidget() override;

    EditorView* editorA() const { return _editorA; }
    EditorView* editorB() const { return _editorB; }

    void refreshDiff();
    void clearDiff();

    void nextDiff();
    void prevDiff();

    int addedCount() const { return _diffResult.addedCount; }
    int removedCount() const { return _diffResult.removedCount; }
    int modifiedCount() const { return _diffResult.modifiedCount; }
    int movedCount() const { return _diffResult.movedCount; }
    int totalDiffCount() const { return static_cast<int>(_diffLineIndices.size()); }
    int currentDiffIndex() const { return _currentDiffIndex; }

    void setDarkMode(bool dark);

    // P.7：大文本异步比对；小文本保持同步零延迟。
    bool isComputing() const { return _computing; }
    // 按 alignment 把 A 侧文档行映射到 B 侧文档行（消除 annotation 占位导致的滚动漂移）。
    int alignedLineBForLineA(int lineA) const;
    int alignedLineAForLineB(int lineB) const;
    void navigateToAlignIndex(int alignIndex);
    const Diff::AlignmentList& alignment() const { return _alignment; }
    DiffMiniMap* miniMap() const { return _miniMap; }

signals:
    void diffNavigated(int currentIndex, int totalCount);
    void diffComputing(bool computing);
    void closeCompareRequested(); // 工具条「关闭对比」按钮

private:
    void setupScintillaStyles(EditorView* ed);
    void applyDiffVisuals();
    void clearScintillaVisuals(EditorView* ed);
    void connectScrollSignals();
    void publishResult(Diff::DiffResult result);
    void onDiffFinished();
    void gotoAlignIndex(int alignIndex);
    void syncVerticalFromA();
    void syncVerticalFromB();

    EditorView* _editorA{nullptr};
    EditorView* _editorB{nullptr};
    QSplitter* _splitter{nullptr};
    DiffMiniMap* _miniMap{nullptr};

    Diff::DiffOptions _options;
    Diff::DiffResult _diffResult;
    Diff::AlignmentList _alignment;
    QCheckBox* _ignoreWhitespaceCheck{nullptr};
    QCheckBox* _ignoreCaseCheck{nullptr};
    // 仅在比对模式存在的工具条按钮：差异导航与退出比对。
    QPushButton* _nextDiffButton{nullptr};
    QPushButton* _prevDiffButton{nullptr};
    QPushButton* _closeCompareButton{nullptr};
    QFutureWatcher<Diff::DiffResult>* _watcher{nullptr};
    std::vector<int> _diffLineIndices; // 差异对齐行索引
    int _currentDiffIndex{-1};
    int _generation{0};
    bool _computing{false};
    bool _isDarkMode{false};
    bool _isSyncingScroll{false};
};
