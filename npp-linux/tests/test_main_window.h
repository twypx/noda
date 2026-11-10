#pragma once

#include <QObject>

class TestMainWindow : public QObject {
    Q_OBJECT
private slots:
    // 保存失败必须有一条用户可见、原因明确的提示，且不能把磁盘文件写坏。
    void saveFailureNotifiesUserAndKeepsFile();
    // 周期备份只有在文档内容确实发生新修改时才写盘
    void backupOnlyWhenDirty();
    // 菜单栏必须包含“帮助”菜单与“关于 Noda”动作
    void helpMenuAndAboutActionExist();
    // “关于”对话框必须包含正确的版本号与相关系统信息
    void aboutDialogContentMatchesVersion();
};
