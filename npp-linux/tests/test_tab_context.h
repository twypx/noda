#pragma once

#include <QObject>

// 标签页右键菜单：事件命中、菜单结构与「操作对象必须是被右击的标签」。
class TestTabContext : public QObject {
    Q_OBJECT

private slots:
    // 右键标签：先选中该标签，再且仅再发一次上下文菜单请求。
    void rightClickSelectsTabAndEmitsRequest();

    // 右键标签栏空白处：不改变选中项、不弹菜单（对齐上游 `tabAt == -1` 分支）。
    void rightClickOnBlankAreaDoesNothing();

    // 顶层顺序、分隔符、四个子菜单与上游 tabContextMenu_example.xml 的映射一致。
    void menuMatchesUpstreamStructure();

    // 无磁盘路径的新文档：路径相关动作禁用，其余动作保持可用。
    void untitledTabDisablesPathActions();

    // 「只读」勾选态来自真实编辑器状态，触发后真正切换。
    void readOnlyItemReflectsAndToggles();

    // 菜单动作作用于被右击的标签，而不是构建菜单时的当前标签。
    void closeToRightAppliesToClickedTab();

    // 边界禁用规则与真正的重排效果。
    void moveItemsRespectEdgesAndReorder();
};
