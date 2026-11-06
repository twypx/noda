#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

// 自绘标题栏：用于无边框(FramelessWindowHint)主窗口顶部。
//  - 行内布局：[应用图标][嵌入的菜单栏][可拖动空白区][最小化][最大化(还原)][关闭]。
//    中间不再显示文件标题：任务栏悬停提示用的是窗口标题本身，标题栏里重复一遍没有价值。
//  - 在空白处按住左键拖动调用 Wayland/X11 的 startSystemMove 移动窗口。
//  - 双击标题栏切换最大化/还原。
//  - 通过事件过滤器监听顶层窗口的状态变化，同步最大化按钮字形。
class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget* parent = nullptr);

    // 把菜单栏插到前导图标之后（即图标保持在最左，菜单紧随其右）。
    void insertMenuWidget(QWidget* w);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void toggleMaximize();
    void refreshMaxGlyph();

    QLabel* _iconLabel;
    QToolButton* _minBtn;
    QToolButton* _maxBtn;
    QToolButton* _closeBtn;
};
