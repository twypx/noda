#include "DiffMiniMap.h"

#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <algorithm>

namespace {

QColor colorFor(Diff::ChangeType type, bool dark) {
    switch (type) {
    case Diff::ChangeType::Added:
        return dark ? QColor(0x3C, 0x8C, 0x3C) : QColor(0x4C, 0xAF, 0x50);
    case Diff::ChangeType::Removed:
        return dark ? QColor(0xB0, 0x3C, 0x3C) : QColor(0xE0, 0x50, 0x50);
    case Diff::ChangeType::Modified:
        return dark ? QColor(0xC0, 0x90, 0x30) : QColor(0xF0, 0xA0, 0x20);
    case Diff::ChangeType::Moved:
        return dark ? QColor(0x7A, 0x5A, 0xC0) : QColor(0x9C, 0x6C, 0xE0);
    case Diff::ChangeType::Unchanged:
        break;
    }
    return QColor();
}

} // namespace

DiffMiniMap::DiffMiniMap(QWidget* parent) : QWidget(parent) {
    setFixedWidth(14);
    setCursor(Qt::PointingHandCursor);
    setToolTip(QStringLiteral("差异分布：点击跳转"));
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void DiffMiniMap::setAlignment(const Diff::AlignmentList& alignment) {
    _alignment = alignment;
    _diffIndices.clear();
    _diffIndices.reserve(alignment.size() / 4 + 1);
    for (size_t i = 0; i < alignment.size(); ++i) {
        if (alignment[i].type != Diff::ChangeType::Unchanged) {
            _diffIndices.push_back(static_cast<int>(i));
        }
    }
    update();
}

void DiffMiniMap::setDarkMode(bool dark) {
    if (_isDarkMode != dark) {
        _isDarkMode = dark;
        update();
    }
}

// y → alignment 索引；无差异时返回 -1。
int DiffMiniMap::indexAtY(int y) const {
    if (_alignment.empty() || _diffIndices.empty() || height() <= 0) return -1;
    const int clamped = std::clamp(y, 0, std::max(0, height() - 1));
    const int target = static_cast<int>(
        static_cast<qint64>(clamped) * static_cast<qint64>(_alignment.size()) / height());
    // 二分找最接近 target 的差异索引
    const auto it = std::lower_bound(_diffIndices.begin(), _diffIndices.end(), target);
    int best = -1;
    if (it != _diffIndices.end()) best = *it;
    if (it != _diffIndices.begin()) {
        const int prev = *(it - 1);
        if (best < 0 || std::abs(prev - target) <= std::abs(best - target)) best = prev;
    }
    return best;
}

void DiffMiniMap::paintEvent(QPaintEvent* /*event*/) {
    QPainter painter(this);
    painter.fillRect(rect(), _isDarkMode ? QColor(0x22, 0x22, 0x22) : QColor(0xF2, 0xF2, 0xF2));
    if (_alignment.empty() || height() <= 0) return;

    // 逐差异绘制最小 2px 高的色块；连续同类差异合并为一段，减少 drawRect 次数。
    QColor currentColor;
    int runStart = -1;
    auto flush = [&](int runEnd) {
        if (runStart < 0 || !currentColor.isValid()) return;
        const int y0 = static_cast<int>(static_cast<qint64>(runStart) * height() / _alignment.size());
        const int y1 = static_cast<int>(static_cast<qint64>(runEnd + 1) * height() / _alignment.size());
        painter.fillRect(1, y0, width() - 2, std::max(2, y1 - y0), currentColor);
        runStart = -1;
        currentColor = QColor();
    };

    for (size_t i = 0; i < _alignment.size(); ++i) {
        const QColor c = colorFor(_alignment[i].type, _isDarkMode);
        if (!c.isValid()) {
            flush(static_cast<int>(i));
            continue;
        }
        if (runStart < 0) {
            runStart = static_cast<int>(i);
            currentColor = c;
        } else if (c != currentColor) {
            flush(static_cast<int>(i) - 1);
            runStart = static_cast<int>(i);
            currentColor = c;
        }
    }
    flush(static_cast<int>(_alignment.size()) - 1);
}

void DiffMiniMap::mousePressEvent(QMouseEvent* event) {
    const int idx = indexAtY(static_cast<int>(event->position().y()));
    if (idx >= 0) {
        emit navigateTo(idx);
    }
    QWidget::mousePressEvent(event);
}
