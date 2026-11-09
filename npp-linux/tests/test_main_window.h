#pragma once

#include <QObject>

class TestMainWindow : public QObject {
    Q_OBJECT
private slots:
    // 保存失败必须有一条用户可见、原因明确的提示，且不能把磁盘文件写坏。
    void saveFailureNotifiesUserAndKeepsFile();
};
