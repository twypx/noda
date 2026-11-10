#pragma once

#include <QWidget>
#include <QSplitter>
#include <vector>
#include "utils/DiffEngine.h"

class EditorView;
class QCheckBox;

class CompareWidget : public QWidget {
    Q_OBJECT
public:
    static constexpr int MARK_DIFF_ADDED = 20;
    static constexpr int MARK_DIFF_REMOVED = 21;
    static constexpr int MARK_DIFF_MODIFIED = 22;
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
    int totalDiffCount() const {
        return static_cast<int>(_diffLineIndices.size());
    }
    int currentDiffIndex() const { return _currentDiffIndex; }

    void setDarkMode(bool dark);

signals:
    void diffNavigated(int currentIndex, int totalCount);

private:
    void setupScintillaStyles(EditorView* ed);
    void applyDiffVisuals();
    void clearScintillaVisuals(EditorView* ed);
    void connectScrollSignals();

    EditorView* _editorA{nullptr};
    EditorView* _editorB{nullptr};
    QSplitter* _splitter{nullptr};

    Diff::DiffOptions _options;
    Diff::DiffResult _diffResult;
    Diff::AlignmentList _alignment;
    QCheckBox* _ignoreWhitespaceCheck{nullptr};
    QCheckBox* _ignoreCaseCheck{nullptr};
    std::vector<int> _diffLineIndices; // 差异对齐行索引
    int _currentDiffIndex{-1};
    bool _isDarkMode{false};
    bool _isSyncingScroll{false};
};
