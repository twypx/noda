#pragma once

#include <QWidget>
#include "utils/DiffEngine.h"

// 比对差异分布小地图：竖条按对齐行比例绘制差异块，点击跳转到对应差异。
// 纯自绘、只存一份 alignment 引用拷贝，绘制 O(差异数)，与文档大小无关。
class DiffMiniMap : public QWidget {
    Q_OBJECT
public:
    explicit DiffMiniMap(QWidget* parent = nullptr);

    void setAlignment(const Diff::AlignmentList& alignment);
    void setDarkMode(bool dark);

    // 把纵坐标映射到 alignment 索引后再吸附到最近的差异行（供测试与点击共用）。
    int indexAtY(int y) const;
    int diffCount() const { return static_cast<int>(_diffIndices.size()); }

signals:
    void navigateTo(int alignIndex);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    Diff::AlignmentList _alignment;
    std::vector<int> _diffIndices;
    bool _isDarkMode{false};
};
