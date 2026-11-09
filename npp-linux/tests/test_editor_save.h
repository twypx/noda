#pragma once

#include <QObject>

class TestEditorSave : public QObject {
    Q_OBJECT
private slots:
    void saveSuccessUsesConfiguredEncoding();
    void saveSuccessWritesBom();
    // 转码失败不能 SETSAVEPOINT，不能截断原文件，失败原因要可查询。
    void saveFailureKeepsFileAndDirtyFlag();
    // 拷贝另存为必须走与保存相同的编码/BOM 链路，且不改变当前文档状态。
    void saveCopyWritesTargetEncodingAndKeepsState();
    void saveCopyRespectsBom();
    void saveCopyFailureWritesNothing();
};
